#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "T03_SDMMC_002.h"

struct FullSensorPayload {
    uint32_t timestamp;
    float acc[3], gyro[3], rpy[3], quat[4];
    uint32_t stepCount;
    uint8_t gesture;
    bool motion;
};

DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;
void IRAM_ATTR bmi270_global_isr() { g_bmi270_fifo_ready = true; }

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr), _sd(nullptr) {
        for(int i=0; i<3; i++) _gyroBias[i] = 0;
    }

    bool begin(BMI270_Options opts, SDMMCHandler* sd = nullptr) {
        _opts = opts; _sd = sd;
        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;

        configureIMU();
        if (_opts.autoCalibrate) performCalibration(); // [신규] 캘리브레이션

        if (_opts.useVQF) _vqf = new VQF(1.0f / Config::SAMPLE_RATE);
        _liveQueue = xQueueCreate(1, sizeof(FullSensorPayload));

        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, NULL, 1);
        if (_opts.useSD && _sd) _sd->startLogging(sdWriterWorker, this);

        pinMode(Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::BMI_INT1), bmi270_global_isr, RISING);
        return true;
    }

    bool getLatest(FullSensorPayload* out) { return xQueueReceive(_liveQueue, out, 0) == pdTRUE; }

private:
    BMI270 _imu; VQF* _vqf; SDMMCHandler* _sd; BMI270_Options _opts;
    QueueHandle_t _liveQueue;
    float _gyroBias[3];

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

    // [신규] 자동 영점 조정
    void performCalibration() {
        float sum[3] = {0,0,0};
        int count = 0;
        while(count < Config::CALIB_SAMPLES) {
            if(_imu.getSensorData() == BMI2_OK) {
                sum[0] += _imu.data.gyroX; sum[1] += _imu.data.gyroY; sum[2] += _imu.data.gyroZ;
                count++;
            }
            delay(5);
        }
        for(int i=0; i<3; i++) _gyroBias[i] = (sum[i] / Config::CALIB_SAMPLES) * 0.0174533f;
    }

    void updateData(FullSensorPayload& d) {
        d.timestamp = millis();
        d.acc[0] = _imu.data.accelX; d.acc[1] = _imu.data.accelY; d.acc[2] = _imu.data.accelZ;
        // [신규] 오프셋 적용
        d.gyro[0] = (_imu.data.gyroX * 0.0174533f) - _gyroBias[0];
        d.gyro[1] = (_imu.data.gyroY * 0.0174533f) - _gyroBias[1];
        d.gyro[2] = (_imu.data.gyroZ * 0.0174533f) - _gyroBias[2];

        if (_vqf) {
            xyz_t gyr = {d.gyro[0], d.gyro[1], d.gyro[2]}, acc = {d.acc[0], d.acc[1], d.acc[2]};
            Quaternion q = _vqf->updateOrientation(gyr, acc, 1.0f / Config::SAMPLE_RATE);
            d.quat[0] = q.w; d.quat[1] = q.x; d.quat[2] = q.y; d.quat[3] = q.z;
            computeEuler(d);
        }

        // [신규] 제스처 및 동작 상태 읽기
        if (_opts.useGestures) d.gesture = _imu.getWristGesture();
        if (_opts.useAnyMotion) d.motion = _imu.getMotionStatus();
        if (_opts.useStepCounter) _imu.getStepCount(&d.stepCount);
    }

    static void sensorTask(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload data;
        while (true) {
            if (g_bmi270_fifo_ready) {
                g_bmi270_fifo_ready = false;
                uint16_t numRead = 20;
                while (self->_imu.getFIFOData(&self->_imu.data, &numRead) == BMI2_OK && numRead > 0) {
                    self->updateData(data);
                    xQueueOverwrite(self->_liveQueue, &data);
                    if (self->_sd) xQueueSend(self->_sd->getQueue(), &data, 0);
                    uint16_t len; self->_imu.getFIFOLength(&len);
                    if (len < 10) break;
                    numRead = 20;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    static void sdWriterWorker(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload entry;
        File file = SD_MMC.open(self->_sd->getPath(), FILE_APPEND);
        if (!file) vTaskDelete(NULL);
        uint32_t lastFlush = millis();
        while (true) {
            if (xQueueReceive(self->_sd->getQueue(), &entry, portMAX_DELAY)) {
                file.printf("%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u,%u,%d\n",
                            entry.timestamp, entry.acc[0], entry.acc[1], entry.acc[2],
                            entry.rpy[0], entry.rpy[1], entry.rpy[2], 
                            entry.stepCount, entry.gesture, entry.motion);
                if (millis() - lastFlush > 2000) { file.flush(); lastFlush = millis(); }
            }
        }
    }

    void computeEuler(FullSensorPayload& d) {
        float qw = d.quat[0], qx = d.quat[1], qy = d.quat[2], qz = d.quat[3];
        d.rpy[0] = atan2(2.0f*(qw*qx + qy*qz), 1.0f-2.0f*(qx*qx + qy*qy)) * 57.29578f;
        d.rpy[1] = asin(2.0f*(qw*qy - qz*qx)) * 57.29578f;
        d.rpy[2] = atan2(2.0f*(qw*qz + qx*qy), 1.0f-2.0f*(qy*qy + qz*qz)) * 57.29578f;
    }
};
