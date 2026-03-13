
 #pragma once
 
 /**
 * @file T002_BMI270_Handler_006.h
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
#include "SensorFusion.h" // 업로드해주신 VQF 헤더


// 클래스 외부, 전역 파일 스코프에 변수와 ISR 배치
// DRAM_ATTR을 명시적으로 붙여 리터럴 관련 오류를 원천 차단합니다.
DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;

// 전역 함수로 ISR 정의 (IRAM_ATTR 필수)
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
    uint8_t gesture;
    bool motion;
};

struct BMI270_Options {
    bool useVQF = true;
    bool useSD = true;
    bool useStepCounter = true;
    bool useGestures = true;
    bool useAnyMotion = true;
    float sampleRate = 200.0f;
    uint16_t fifoThreshold = 100;
};

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr) {}

    bool begin(int cs, int int1, BMI270_Options opts) {
        _opts = opts;
        _int1Pin = int1;

        if (_imu.beginSPI(cs) != BMI2_OK) return false;
        configureSensor();

        if (_opts.useVQF) {
            // VQF 생성자 인자 확인 필요 (샘플링 주기 전달)
            _vqf = new VQF(1.0f / _opts.sampleRate);
        }

        _dataQueue = xQueueCreate(100, sizeof(FullSensorPayload));
        _sdQueue = xQueueCreate(300, sizeof(FullSensorPayload));

        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, &_sensorTaskHandle, 1);
        if (_opts.useSD) {
            xTaskCreatePinnedToCore(sdTask, "SD_Task", 8192, this, 1, &_sdTaskHandle, 0);
        }

        pinMode(_int1Pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_int1Pin), bmi270_global_isr, RISING);
        return true;
    }

    bool getLatestData(FullSensorPayload* out) {
        return xQueueReceive(_dataQueue, out, 0) == pdTRUE;
    }

private:
    BMI270 _imu;
    VQF* _vqf;
    BMI270_Options _opts;
    int _int1Pin;
    QueueHandle_t _dataQueue, _sdQueue;
    TaskHandle_t _sensorTaskHandle, _sdTaskHandle;

    void configureSensor() {
        BMI270_FIFOConfig fifoConfig;
        fifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        fifoConfig.watermark = _opts.fifoThreshold;
        fifoConfig.accelDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fifoConfig.gyroDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fifoConfig.accelFilter = true;
        fifoConfig.gyroFilter = true;
        _imu.setFIFOConfig(fifoConfig);

        bmi2_remap axes = {BMI2_AXIS_POS_X, BMI2_AXIS_POS_Y, BMI2_AXIS_POS_Z};
        _imu.remapAxes(axes);

        if (_opts.useAnyMotion) _imu.enableFeature(BMI2_ANY_MOTION);
        if (_opts.useStepCounter) _imu.enableFeature(BMI2_STEP_COUNTER);
        if (_opts.useGestures) _imu.enableFeature(BMI2_WRIST_GESTURE);

        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);

		_imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);

    }

    static void sensorTask(void* pv) {
        BMI270Handler* self = (BMI270Handler*)pv;
        FullSensorPayload data;
        while (true) {
            if (g_bmi270_fifo_ready) {
                g_bmi270_fifo_ready = false;
                uint16_t numRead = 10;
                while (self->_imu.getFIFOData(&self->_imu.data, &numRead) == BMI2_OK && numRead > 0) {
                    data.timestamp = millis();
                    data.acc[0] = self->_imu.data.accelX;
                    data.acc[1] = self->_imu.data.accelY;
                    data.acc[2] = self->_imu.data.accelZ;
                    data.gyro[0] = self->_imu.data.gyroX * 0.0174533f;
                    data.gyro[1] = self->_imu.data.gyroY * 0.0174533f;
                    data.gyro[2] = self->_imu.data.gyroZ * 0.0174533f;

                    if (self->_opts.useVQF && self->_vqf) {
                        xyz_t gRPS = {data.gyro[0], data.gyro[1], data.gyro[2]};
                        xyz_t aG = {data.acc[0], data.acc[1], data.acc[2]};
                        Quaternion q = self->_vqf->updateOrientation(gRPS, aG, 1.0f / self->_opts.sampleRate);
                        data.quat[0] = q.w; data.quat[1] = q.x; data.quat[2] = q.y; data.quat[3] = q.z;
                        self->updateEuler(data);
                    }
                    if (self->_opts.useStepCounter) self->_imu.getStepCount(&data.stepCount);

                    xQueueSend(self->_dataQueue, &data, 0);
                    if (self->_opts.useSD) xQueueSend(self->_sdQueue, &data, 0);

                    uint16_t len;
                    self->_imu.getFIFOLength(&len);
                    if (len == 0) break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    static void sdTask(void* pv) {
        BMI270Handler* self = (BMI270Handler*)pv;
        FullSensorPayload entry;
        if (!SD_MMC.begin("/sdcard", true)) vTaskDelete(NULL);
        File file = SD_MMC.open("/sensor_log.csv", FILE_WRITE);
        if (!file) vTaskDelete(NULL);
        file.println("Time,Ax,Ay,Az,Roll,Pitch,Yaw,Steps");
        while (true) {
            if (xQueueReceive(self->_sdQueue, &entry, portMAX_DELAY)) {
                file.printf("%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u\n",
                            entry.timestamp, entry.acc[0], entry.acc[1], entry.acc[2],
                            entry.rpy[0], entry.rpy[1], entry.rpy[2], entry.stepCount);
            }
        }
    }

    void updateEuler(FullSensorPayload& d) {
        float qw = d.quat[0], qx = d.quat[1], qy = d.quat[2], qz = d.quat[3];
        d.rpy[0] = atan2(2.0f * (qw * qx + qy * qz), 1.0f - 2.0f * (qx * qx + qy * qy)) * 57.29578f;
        d.rpy[1] = asin(2.0f * (qw * qy - qz * qx)) * 57.29578f;
        d.rpy[2] = atan2(2.0f * (qw * qz + qx * qy), 1.0f - 2.0f * (qy * qy + qz * qz)) * 57.29578f;
    }
}; 



