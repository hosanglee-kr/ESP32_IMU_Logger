// T03_BM217_001.h

#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "T03_SDMMC_001.h"
#include "T03_Config_001.h"

// 데이터 구조체 (Padding 최적화)
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
};

// ISR 신호용 전역 플래그
DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;
void IRAM_ATTR bmi270_global_isr() { g_bmi270_fifo_ready = true; }

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr), _sd(nullptr) {}

    bool begin(BMI270_Options opts, SDMMCHandler* sd = nullptr) {
        _opts = opts;
        _sd = sd;

        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;
        
        configureIMU();
        if (_opts.useVQF) _vqf = new VQF(1.0f / Config::SAMPLE_RATE);

        _liveQueue = xQueueCreate(1, sizeof(FullSensorPayload));

        // 센서 태스크 (Core 1, 최상위 우선순위)
        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, NULL, 1);
        
        // SD 태스크 (필요 시 작동)
        if (_opts.useSD && _sd) {
            _sd->startLogging(sdWriterWorker, this);
        }

        pinMode(Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::BMI_INT1), bmi270_global_isr, RISING);
        return true;
    }

    bool getLatest(FullSensorPayload* out) {
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

        if (_opts.useAnyMotion)   _imu.enableFeature(BMI2_ANY_MOTION);
        if (_opts.useStepCounter) _imu.enableFeature(BMI2_STEP_COUNTER);
        if (_opts.useGestures)    _imu.enableFeature(BMI2_WRIST_GESTURE);

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
                while (self->_imu.getFIFOData() == BMI2_OK) {
                    self->updateData(data);
                    
                    xQueueOverwrite(self->_liveQueue, &data);
                    if (self->_sd) xQueueSend(self->_sd->getQueue(), &data, 0);

                    uint16_t len;
                    self->_imu.getFIFOLength(&len);
                    if (len < 10) break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void updateData(FullSensorPayload& d) {
        d.timestamp = millis();
        d.acc[0] = _imu.data.accelX; d.acc[1] = _imu.data.accelY; d.acc[2] = _imu.data.accelZ;
        // Radian 변환
        d.gyro[0] = _imu.data.gyroX * 0.0174533f;
        d.gyro[1] = _imu.data.gyroY * 0.0174533f;
        d.gyro[2] = _imu.data.gyroZ * 0.0174533f;

        if (_vqf) {
            _vqf->update(d.gyro, d.acc);
            _vqf->getQuaternion(d.quat);
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
