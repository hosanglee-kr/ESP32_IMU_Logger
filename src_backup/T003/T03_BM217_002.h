#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "T03_SDMMC_001.h"
#include "T03_Config_001.h"

struct FullSensorPayload {
    uint32_t timestamp;
    float acc[3];
    float gyro[3];
    float rpy[3];
    float quat[4];
    uint32_t stepCount;
};

struct BMI270_Options {
    bool useVQF = true;
    bool useSD = true;
    bool useStepCounter = true;
};

DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;
void IRAM_ATTR bmi270_global_isr() { g_bmi270_fifo_ready = true; }

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr), _sd(nullptr) {}
    ~BMI270Handler() { if(_vqf) delete _vqf; }

    bool begin(BMI270_Options opts, SDMMCHandler* sd = nullptr) {
        _opts = opts;
        _sd = sd;

        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;
        
        configureIMU();
        // VQF 생성자 인자 확인 (보통 dt 전달)
        if (_opts.useVQF) _vqf = new VQF(1.0f / Config::SAMPLE_RATE);

        _liveQueue = xQueueCreate(1, sizeof(FullSensorPayload));

        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, NULL, 1);
        
        if (_opts.useSD && _sd) {
            _sd->startLogging(sdWriterWorker, this);
        }

        pinMode(Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::BMI_INT1), bmi270_global_isr, RISING);
        return true;
    }

    bool getLatest(FullSensorPayload* out) {
        if (!_liveQueue) return false;
        return xQueueReceive(_liveQueue, out, 0) == pdTRUE;
    }

private:
    BMI270 _imu;
    VQF* _vqf;
    SDMMCHandler* _sd;
    BMI270_Options _opts;
    QueueHandle_t _liveQueue;

    void configureIMU() {
        BMI270_FIFOConfig fcfg;
        fcfg.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        fcfg.watermark = Config::FIFO_WTM;
        _imu.setFIFOConfig(fcfg);

        if (_opts.useStepCounter) _imu.enableFeature(BMI2_STEP_COUNTER);
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);
        _imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);
    }

    static void sensorTask(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload data;
        
        while (true) {
            if (g_bmi270_fifo_ready) {
                g_bmi270_fifo_ready = false;
                
                uint16_t numRead = 20; // 한 번에 읽을 최대 프레임 수
                // SparkFun 라이브러리의 getFIFOData(data_struct, num_ptr) 형식 준수
                while (self->_imu.getFIFOData(&self->_imu.data, &numRead) == BMI2_OK && numRead > 0) {
                    self->updateData(data);
                    
                    xQueueOverwrite(self->_liveQueue, &data);
                    if (self->_sd) xQueueSend(self->_sd->getQueue(), &data, 0);

                    uint16_t len;
                    self->_imu.getFIFOLength(&len);
                    if (len < 10) break;
                    numRead = 20; // 다음 루프를 위해 초기화
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void updateData(FullSensorPayload& d) {
        d.timestamp = millis();
        // SparkFun BMI270 라이브러리는 getFIFOData 호출 시 _imu.data에 값을 채움
        d.acc[0] = _imu.data.accelX; 
        d.acc[1] = _imu.data.accelY; 
        d.acc[2] = _imu.data.accelZ;
        
        d.gyro[0] = _imu.data.gyroX * 0.0174533f; // rad/s 변환
        d.gyro[1] = _imu.data.gyroY * 0.0174533f;
        d.gyro[2] = _imu.data.gyroZ * 0.0174533f;

        if (_opts.useVQF && _vqf) {
            // VQF 라이브러리의 updateOrientation은 Quaternion 구조체를 반환함
            // xyz_t 구조체 호환성을 위해 배열 전달 혹은 구조체 변환 필요
            xyz_t gyr = {d.gyro[0], d.gyro[1], d.gyro[2]};
            xyz_t acc = {d.acc[0], d.acc[1], d.acc[2]};
            
            Quaternion q = _vqf->updateOrientation(gyr, acc, 1.0f / Config::SAMPLE_RATE);
            
            d.quat[0] = q.w; d.quat[1] = q.x; d.quat[2] = q.y; d.quat[3] = q.z;
            computeEuler(d);
        }
        
        if (_opts.useStepCounter) _imu.getStepCount(&d.stepCount);
    }

    static void sdWriterWorker(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload entry;
        File file = SD_MMC.open(self->_sd->getPath(), FILE_APPEND);
        if (!file) vTaskDelete(NULL);

        uint32_t lastFlush = millis();
        while (true) {
            if (xQueueReceive(self->_sd->getQueue(), &entry, portMAX_DELAY)) {
                file.printf("%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u\n",
                            entry.timestamp, entry.acc[0], entry.acc[1], entry.acc[2],
                            entry.rpy[0], entry.rpy[1], entry.rpy[2], entry.stepCount);
                
                if (millis() - lastFlush > 2000) {
                    file.flush();
                    lastFlush = millis();
                }
            }
        }
    }

    void computeEuler(FullSensorPayload& d) {
        float qw = d.quat[0], qx = d.quat[1], qy = d.quat[2], qz = d.quat[3];
        d.rpy[0] = atan2(2.0f * (qw * qx + qy * qz), 1.0f - 2.0f * (qx * qx + qy * qy)) * 57.29578f;
        d.rpy[1] = asin(2.0f * (qw * qy - qz * qx)) * 57.29578f;
        d.rpy[2] = atan2(2.0f * (qw * qz + qx * qy), 1.0f - 2.0f * (qy * qy + qz * qz)) * 57.29578f;
    }
};


