/*
 * ------------------------------------------------------
 * 소스명 : SB10_BM217_013.h
 * 모듈약어 : SB10 (BMI270)
 * 모듈명 : Motion Engine with VQF & SD_MMC Integration
 * ------------------------------------------------------
 * 기능 요약
 * - BMI270 FIFO 워터마크(200바이트) 인터럽트 처리
 * - VQF 6-Axis 자세 추정 및 선형 가속도 산출
 * - Any/No Motion 기반 자동 Light Sleep 전환 제어
 * - SD_MMC 1-bit 기록용 데이터 포맷팅
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "C10_Config_013.h"
#include "SD10_SDMMC_013.h"

#ifndef M_PI
    #define M_PI 3.14159265358979323846f
#endif

static constexpr float G_SB10_DEG_TO_RAD = M_PI / 180.0f;
static constexpr float G_SB10_RAD_TO_DEG = 180.0f / M_PI;

struct ST_FullSensorPayload_t {
    uint32_t timestamp;
    float rawAcc[3];
    float linAcc[3];
    float gyro[3];
    float quat[4];
    float euler[3];
    uint32_t stepCount;
    bool motion;
};

extern SemaphoreHandle_t g_SB10_Sem_FIFO;

void IRAM_ATTR SB10_bmi270_fifo_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_SB10_Sem_FIFO, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

class CL_SB10_BMI270_Handler {
private:
    BMI270 _imu;
    ST_BMI270_Options_t _opts;
    CL_SD10_SDMMC_Handler* _sd;
    VQF* _vqf = nullptr;
    bool _isSignificantMoving = false;
    BMI270_SensorData _fifoBuffer[C10_Config::FIFO_WTM_COUNT];

public:
    bool begin(ST_BMI270_Options_t p_opts, CL_SD10_SDMMC_Handler* p_sd) {
        _opts = p_opts;
        _sd = p_sd;

        if (_imu.beginSPI(C10_Config::BMI_CS) != BMI2_OK) return false;

        if (_opts.useVQF) {
            float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;
            _vqf = new VQF(v_dt, v_dt, v_dt);
            _vqf->setFreeParameters(C10_Config::VQF_TAU_ACC, 0.0f);
        }

        configureIMU();
        setupMotionInterrupts();

        pinMode(C10_Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(C10_Config::BMI_INT1), SB10_bmi270_fifo_isr, RISING);

        return true;
    }

    void configureIMU() {
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);
        
        BMI270_FIFOConfig v_FifoConfig;
        v_FifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        v_FifoConfig.watermark = C10_Config::FIFO_WTM;
        _imu.setFIFOConfig(v_FifoConfig);
        _imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);
    }

    void updateProcess(ST_FullSensorPayload_t& p_data) {
        uint16_t v_samplesRead = C10_Config::FIFO_WTM_COUNT;
        if (_imu.getFIFOData(_fifoBuffer, &v_samplesRead) != BMI2_OK || v_samplesRead == 0) return;

        float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;

        for (uint16_t i = 0; i < v_samplesRead; i++) {
            float v_ax = _fifoBuffer[i].accelX;
            float v_ay = _fifoBuffer[i].accelY;
            float v_az = _fifoBuffer[i].accelZ;
            float v_gx = _fifoBuffer[i].gyroX * G_SB10_DEG_TO_RAD;
            float v_gy = _fifoBuffer[i].gyroY * G_SB10_DEG_TO_RAD;
            float v_gz = _fifoBuffer[i].gyroZ * G_SB10_DEG_TO_RAD;

            if (_vqf) {
                xyz_t v_gyr_v = {v_gx, v_gy, v_gz};
                xyz_t v_acc_v = {v_ax, v_ay, v_az};
                Quaternion v_q = _vqf->updateOrientation(v_gyr_v, v_acc_v, v_dt);

                if (i == v_samplesRead - 1) {
                    p_data.rawAcc[0] = v_ax; p_data.rawAcc[1] = v_ay; p_data.rawAcc[2] = v_az;
                    p_data.gyro[0] = v_gx; p_data.gyro[1] = v_gy; p_data.gyro[2] = v_gz;
                    p_data.quat[0] = v_q.w; p_data.quat[1] = v_q.x; p_data.quat[2] = v_q.y; p_data.quat[3] = v_q.z;

                    float gravityX = 2.0f * (v_q.x * v_q.z - v_q.w * v_q.y);
                    float gravityY = 2.0f * (v_q.w * v_q.x + v_q.y * v_q.z);
                    float gravityZ = v_q.w * v_q.w - v_q.x * v_q.x - v_q.y * v_q.y + v_q.z * v_q.z;
                    p_data.linAcc[0] = v_ax - gravityX;
                    p_data.linAcc[1] = v_ay - gravityY;
                    p_data.linAcc[2] = v_az - gravityZ;

                    computeEuler(p_data);
                }
            }
        }

        p_data.timestamp = millis();
        p_data.motion = _isSignificantMoving;
        _imu.getStepCount(&p_data.stepCount);

        if (_opts.useSD && _sd) {
            char v_line[128];
            snprintf(v_line, sizeof(v_line), "%u,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,%.1f,%u,%d\n",
                     p_data.timestamp, p_data.linAcc[0], p_data.linAcc[1], p_data.linAcc[2],
                     p_data.quat[0], p_data.quat[1], p_data.quat[2], p_data.quat[3],
                     p_data.euler[0], p_data.euler[1], p_data.euler[2],
                     p_data.stepCount, p_data.motion);
            _sd->logToBuffer(v_line);
        }
    }

    void checkMotionStatus() {
        uint16_t v_status = 0;
        _imu.getInterruptStatus(&v_status);
        if (v_status & BMI270_SIG_MOT_STATUS_MASK) _isSignificantMoving = true;
        if (v_status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            enterIdleMode();
        }
    }

private:
    void computeEuler(ST_FullSensorPayload_t& d) {
        d.euler[0] = atan2(2.0f * (d.quat[0] * d.quat[1] + d.quat[2] * d.quat[3]), 1.0f - 2.0f * (d.quat[1] * d.quat[1] + d.quat[2] * d.quat[2])) * G_SB10_RAD_TO_DEG;
        d.euler[1] = asin(2.0f * (d.quat[0] * d.quat[2] - d.quat[3] * d.quat[1])) * G_SB10_RAD_TO_DEG;
        d.euler[2] = atan2(2.0f * (d.quat[0] * d.quat[3] + d.quat[1] * d.quat[2]), 1.0f - 2.0f * (d.quat[3] * d.quat[3] + d.quat[2] * d.quat[2])) * G_SB10_RAD_TO_DEG;
    }

    void enterIdleMode() {
        _imu.flushFIFO();
        _imu.setGyroPowerMode(BMI2_POWER_OPT_MODE, BMI2_POWER_OPT_MODE);
        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();
        resumeFromIdle();
    }

    void resumeFromIdle() {
        _imu.disableAdvancedPowerSave();
        _imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
        configureIMU();
        if (_opts.useSD) {
            _sd->begin();
            _sd->createLogFile(_opts.logPrefix, "Time,Lx,Ly,Lz,QW,QX,QY,QZ,R,P,Y,Steps,Mot");
        }
    }

    void setupMotionInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);
        _imu.enableFeature(BMI2_SIG_MOTION);
        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_SIG_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);
    }
};


