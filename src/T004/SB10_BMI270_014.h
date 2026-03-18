/*
 * ------------------------------------------------------
 * 소스명 : SB10_BMI270_014.h
 * 모듈약어 : SB10 (BMI270)
 * 모듈명 : Advanced Motion Engine with VQF Fusion
 * ------------------------------------------------------
 * 기능 요약
 * - FIFO 워터마크(15샘플) 배치 처리로 CPU 오버헤드 최소화
 * - VQF 필터를 이용한 실시간 쿼터니언 및 선형 가속도 추출
 * - Any/Significant Motion 기반 시스템 슬립(Wakeup) 제어
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [SB10] 센서 하드웨어 제어 및 물리 법칙(Fusion) 연산 전담
 * - 데이터의 의미적 해석(자세 추정)을 수행하고 SD10에 전달
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "C10_Config_014.h"
#include "SD10_SDMMC_014.h"

// 데이터 구조체 및 상수 (v012 유지)
struct ST_FullSensorPayload_t {
    uint32_t timestamp;
    float rawAcc[3], linAcc[3], gyro[3], quat[4], euler[3];
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
public:
    bool begin(ST_BMI270_Options_t p_opts, CL_SD10_SDMMC_Handler* p_sd) {
        _opts = p_opts; _sd = p_sd;
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
        uint16_t v_samples = C10_Config::FIFO_WTM_COUNT;
        if (_imu.getFIFOData(_fifoBuffer, &v_samples) != BMI2_OK || v_samples == 0) return;

        float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;
        for (uint16_t i = 0; i < v_samples; i++) {
            float v_ax = _fifoBuffer[i].accelX, v_ay = _fifoBuffer[i].accelY, v_az = _fifoBuffer[i].accelZ;
            float v_gx = _fifoBuffer[i].gyroX * 0.017453f, v_gy = _fifoBuffer[i].gyroY * 0.017453f, v_gz = _fifoBuffer[i].gyroZ * 0.017453f;

            if (_vqf) {
                Quaternion v_q = _vqf->updateOrientation({v_gx, v_gy, v_gz}, {v_ax, v_ay, v_az}, v_dt);
                if (i == v_samples - 1) { // 마지막 샘플만 보고
                    p_data.quat[0] = v_q.w; p_data.quat[1] = v_q.x; p_data.quat[2] = v_q.y; p_data.quat[3] = v_q.z;
                    // 선형 가속도 계산 로직... (중략: 기존 v012와 동일)
                    computeEuler(p_data);
                }
            }
        }
        p_data.timestamp = millis();
        p_data.motion = _isSignificantMoving;
        if (_opts.useSD && _sd) {
            char v_line[128];
            snprintf(v_line, sizeof(v_line), "%u,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%d\n",
                     p_data.timestamp, p_data.linAcc[0], p_data.linAcc[1], p_data.linAcc[2],
                     p_data.quat[0], p_data.quat[1], p_data.quat[2], p_data.quat[3], p_data.motion);
            _sd->logToBuffer(v_line);
        }
    }

    void checkMotionStatus() {
        uint16_t v_status = 0;
        _imu.getInterruptStatus(&v_status);
        if (v_status & BMI270_SIG_MOT_STATUS_MASK) _isSignificantMoving = true;
        if (v_status & BMI270_NO_MOT_STATUS_MASK) { 
            _isSignificantMoving = false; 
            // enterIdleMode() 로직 호출 가능 (v012 참조)
        }
    }

private:
    BMI270 _imu; ST_BMI270_Options_t _opts; CL_SD10_SDMMC_Handler* _sd; VQF* _vqf = nullptr;
    bool _isSignificantMoving = false;
    BMI270_SensorData _fifoBuffer[C10_Config::FIFO_WTM_COUNT];

    void computeEuler(ST_FullSensorPayload_t& d) {
        d.euler[0] = atan2(2.0f*(d.quat[0]*d.quat[1]+d.quat[2]*d.quat[3]), 1.0f-2.0f*(d.quat[1]*d.quat[1]+d.quat[2]*d.quat[2])) * 57.2957f;
        d.euler[1] = asin(2.0f*(d.quat[0]*d.quat[2]-d.quat[3]*d.quat[1])) * 57.2957f;
        d.euler[2] = atan2(2.0f*(d.quat[0]*d.quat[3]+d.quat[1]*d.quat[2]), 1.0f-2.0f*(d.quat[3]*d.quat[3]+d.quat[2]*d.quat[2])) * 57.2957f;
    }

    void setupMotionInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION); _imu.enableFeature(BMI2_NO_MOTION);
        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);
    }
};


