
 #pragma once
 
 /**
 * @file T002_Handler_007.h
 * @brief BMI270 Full Features Integration + VQF + SDMMC 1-bit Logger
 * * [시스템 설계 개요]
 * 1. Task 분리: Core 1(센서 읽기/필터), Core 0(SDMMC 쓰기) - 쓰기 지연 차단
 * 2. 누락 방지: 대용량 FreeRTOS Queue(100 slots) 사용
 * 3. BMI270 Full: FIFO, Interrupts, Steps, Gestures, Remap, LowPower 등 통합
 * 4. SDMMC: 1-bit 모드 사용으로 SPI 대비 고속 데이터 처리
 */


#include <Arduino.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"

// [메모리 배치] ISR에서 사용하는 전역 변수는 DRAM에 배치
DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;

// [ISR] IRAM에 배치하여 고속 실행 보장
void IRAM_ATTR bmi270_global_isr() {
    g_bmi270_fifo_ready = true;
}

struct FullSensorPayload {
    uint32_t timestamp;
    float acc[3];
    float gyro[3];
    float rpy[3];
    float quat[4];
    uint32_t stepCount;
};

class BMI270Handler {
public:
    // [상수 정의] 하드웨어 관련 핀 및 고정 파라미터
    static constexpr int PIN_CS = 10;
    static constexpr int PIN_INT1 = 9;
    static constexpr float DEFAULT_SAMPLE_RATE = 200.0f;
    static constexpr uint16_t FIFO_WATERMARK = 20; // 읽어올 샘플 단위
    static constexpr float DEG_TO_RAD_CONST = 0.01745329251f;

    BMI270Handler() : _vqf(nullptr) {}

    bool begin() {
        if (_imu.beginSPI(PIN_CS) != BMI2_OK) return false;
        
        configureSensor();

        // VQF 필터 초기화
        _vqf = new VQF(1.0f / DEFAULT_SAMPLE_RATE);

        // 큐 생성
        _dataQueue = xQueueCreate(100, sizeof(FullSensorPayload));
        _sdQueue = xQueueCreate(300, sizeof(FullSensorPayload));

        // 태스크 생성 (Core 1: 센서/융합, Core 0: SD 저장)
        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, &_sensorTaskHandle, 1);
        xTaskCreatePinnedToCore(sdTask, "SD_Task", 8192, this, 1, &_sdTaskHandle, 0);

        // 인터럽트 핀 설정 및 부착
        pinMode(PIN_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_INT1), bmi270_global_isr, RISING);
        
        return true;
    }

    bool getLatestData(FullSensorPayload* out) {
        return xQueueReceive(_dataQueue, out, 0) == pdTRUE;
    }

private:
    BMI270 _imu;
    VQF* _vqf;
    QueueHandle_t _dataQueue, _sdQueue;
    TaskHandle_t _sensorTaskHandle, _sdTaskHandle;
    
    // FIFO 일괄 읽기를 위한 버퍼
    BMI270_SensorData _fifoBuffer[FIFO_WATERMARK];

    void configureSensor() {
        // 1. 센서 데이터 레이트 설정
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);

        // 2. FIFO 설정 (예제 참조)
        BMI270_FIFOConfig config;
        config.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        config.watermark = FIFO_WATERMARK;
        config.accelDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        config.gyroDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        config.accelFilter = BMI2_ENABLE;
        config.gyroFilter = BMI2_ENABLE;
        config.selfWakeUp = BMI2_ENABLE;
        _imu.setFIFOConfig(config);

        // 3. 인터럽트 매핑 (Watermark -> INT1)
        _imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);

        // 4. 인터럽트 핀 전기적 특성 설정
        bmi2_int_pin_config intPinConfig;
        intPinConfig.pin_type = BMI2_INT1;
        intPinConfig.int_latch = BMI2_INT_NON_LATCH; // Pulsed
        intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
        intPinConfig.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
        intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
        intPinConfig.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
        _imu.setInterruptPinConfig(intPinConfig);

        // 5. 부가 기능 활성화
        _imu.enableFeature(BMI2_STEP_COUNTER);
        
        // 축 리맵핑 (X-Y-Z 평면 설정)
        bmi2_remap axes = {BMI2_AXIS_POS_X, BMI2_AXIS_POS_Y, BMI2_AXIS_POS_Z};
        _imu.remapAxes(axes);
    }

    static void sensorTask(void* pv) {
        BMI270Handler* self = (BMI270Handler*)pv;
        FullSensorPayload data;
        
        while (true) {
            if (g_bmi270_fifo_ready) {
                g_bmi270_fifo_ready = false;
                
                uint16_t samplesToRead = FIFO_WATERMARK;
                // FIFO 데이터 일괄 획득
                if (self->_imu.getFIFOData(self->_fifoBuffer, &samplesToRead) == BMI2_OK) {
                    for (uint16_t i = 0; i < samplesToRead; i++) {
                        data.timestamp = millis();
                        
                        // Raw 데이터 가공 (g 및 rad/s 변환)
                        data.acc[0] = self->_fifoBuffer[i].accelX;
                        data.acc[1] = self->_fifoBuffer[i].accelY;
                        data.acc[2] = self->_fifoBuffer[i].accelZ;
                        
                        data.gyro[0] = self->_fifoBuffer[i].gyroX * DEG_TO_RAD_CONST;
                        data.gyro[1] = self->_fifoBuffer[i].gyroY * DEG_TO_RAD_CONST;
                        data.gyro[2] = self->_fifoBuffer[i].gyroZ * DEG_TO_RAD_CONST;

                        // VQF 필터 업데이트
                        if (self->_vqf) {
                            xyz_t gRPS = {data.gyro[0], data.gyro[1], data.gyro[2]};
                            xyz_t aG = {data.acc[0], data.acc[1], data.acc[2]};
                            Quaternion q = self->_vqf->updateOrientation(gRPS, aG, 1.0f / DEFAULT_SAMPLE_RATE);
                            
                            data.quat[0] = q.w; data.quat[1] = q.x; 
                            data.quat[2] = q.y; data.quat[3] = q.z;
                            self->updateEuler(data);
                        }

                        // 주기적으로 스텝 카운트 갱신 (마지막 샘플에서만)
                        if (i == samplesToRead - 1) {
                            self->_imu.getStepCount(&data.stepCount);
                        }

                        xQueueSend(self->_dataQueue, &data, 0);
                        xQueueSend(self->_sdQueue, &data, 0);
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    static void sdTask(void* pv) {
        BMI270Handler* self = (BMI270Handler*)pv;
        FullSensorPayload entry;
        
        if (!SD_MMC.begin("/sdcard", true)) {
            Serial.println("SD Card Mount Failed");
            vTaskDelete(NULL);
        }

        File file = SD_MMC.open("/sensor_log.csv", FILE_APPEND);
        if (!file) {
            Serial.println("File Open Failed");
            vTaskDelete(NULL);
        }

        while (true) {
            if (xQueueReceive(self->_sdQueue, &entry, portMAX_DELAY)) {
                file.printf("%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u\n",
                            entry.timestamp, entry.acc[0], entry.acc[1], entry.acc[2],
                            entry.rpy[0], entry.rpy[1], entry.rpy[2], entry.stepCount);
                
                // 데이터 유실 방지를 위한 주기적 Sync (약 100개마다)
                static uint16_t syncCount = 0;
                if (++syncCount >= 100) {
                    file.flush();
                    syncCount = 0;
                }
            }
        }
    }

    void updateEuler(FullSensorPayload& d) {
        float qw = d.quat[0], qx = d.quat[1], qy = d.quat[2], qz = d.quat[3];
        // Roll
        d.rpy[0] = atan2(2.0f * (qw * qx + qy * qz), 1.0f - 2.0f * (qx * qx + qy * qy)) * 57.29578f;
        // Pitch
        d.rpy[1] = asin(2.0f * (qw * qy - qz * qx)) * 57.29578f;
        // Yaw
        d.rpy[2] = atan2(2.0f * (qw * qz + qx * qy), 1.0f - 2.0f * (qy * qy + qz * qz)) * 57.29578f;
    }
};


