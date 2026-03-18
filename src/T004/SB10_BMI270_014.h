/*
 * ------------------------------------------------------
 * 소스명 : SB10_BMI270_014.h
 * 모듈약어 : SB10 (BMI270)
 * 모듈명 : Advanced Motion Engine with VQF Fusion & Power Management
 * ------------------------------------------------------
 * 기능 요약
 * - FIFO 워터마크(15샘플) 배치 처리로 CPU 오버헤드 최소화 (200Hz 최적화)
 * - VQF 필터를 이용한 실시간 쿼터니언 및 선형 가속도(중력 제거) 추출
 * - Any/No Motion 기반 시스템 Light Sleep 및 하드웨어 인터럽트 복구 제어
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [SB10] 센서 하드웨어 제어 및 6축 데이터 융합(Fusion) 연산 전담
 * - 시스템의 운동 상태를 판별하여 전력 모드를 결정하는 의사결정 책임
 * ------------------------------------------------------
 * [SB10 Tuning Guide]
 * 1. VQF_TAU_ACC: 가속도계 보정 시정수. 진동이 많으면 5.0f(안정), 반응이 중요하면 1.0f(빠름).
 * 2. NO_MOTION_DURATION: 정지 판정 시간. C10_Config에서 설정 (현재 10초).
 * 3. SAMPLE_RATE_ACTIVE: BMI270 ODR과 VQF dt 동기화 필수 (현재 200Hz).
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "C10_Config_014.h"
#include "SD10_SDMMC_014.h"

struct ST_FullSensorPayload_t {
    uint32_t timestamp;
    float rawAcc[3], linAcc[3], gyro[3], quat[4], euler[3];
    uint32_t stepCount;
    bool motion;
};

// 메인 태스크 핸들 및 세마포어 참조
extern TaskHandle_t g_A10_TaskHandle_Sensor;
extern SemaphoreHandle_t g_SB10_Sem_FIFO;

void IRAM_ATTR SB10_bmi270_fifo_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(g_SB10_Sem_FIFO, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

class CL_SB10_BMI270_Handler {
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
        uint16_t v_samples = C10_Config::FIFO_WTM_COUNT;
        if (_imu.getFIFOData(_fifoBuffer, &v_samples) != BMI2_OK || v_samples == 0) return;

        float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;
        
        // 반복 연산 최적화를 위한 임시 쿼터니언
        Quaternion v_q;

        for (uint16_t i = 0; i < v_samples; i++) {
            float v_ax = _fifoBuffer[i].accelX;
            float v_ay = _fifoBuffer[i].accelY;
            float v_az = _fifoBuffer[i].accelZ;
            // Degree to Radian (인라인 상수 사용)
            float v_gx = _fifoBuffer[i].gyroX * 0.01745329f;
            float v_gy = _fifoBuffer[i].gyroY * 0.01745329f;
            float v_gz = _fifoBuffer[i].gyroZ * 0.01745329f;

            if (_vqf) {
                v_q = _vqf->updateOrientation({v_gx, v_gy, v_gz}, {v_ax, v_ay, v_az}, v_dt);
                
                // 배치 처리의 마지막 샘플에서만 물리량 최종 산출 (CPU 최적화)
                if (i == v_samples - 1) {
                    p_data.rawAcc[0] = v_ax; p_data.rawAcc[1] = v_ay; p_data.rawAcc[2] = v_az;
                    p_data.gyro[0] = v_gx; p_data.gyro[1] = v_gy; p_data.gyro[2] = v_gz;
                    p_data.quat[0] = v_q.w; p_data.quat[1] = v_q.x; p_data.quat[2] = v_q.y; p_data.quat[3] = v_q.z;

                    // [최적화] 선형 가속도 계산 (중력 제거)
                    float v_grav_x = 2.0f * (v_q.x * v_q.z - v_q.w * v_q.y);
                    float v_grav_y = 2.0f * (v_q.w * v_q.x + v_q.y * v_q.z);
                    float v_grav_z = v_q.w * v_q.w - v_q.x * v_q.x - v_q.y * v_q.y + v_q.z * v_q.z;

                    p_data.linAcc[0] = v_ax - v_grav_x;
                    p_data.linAcc[1] = v_ay - v_grav_y;
                    p_data.linAcc[2] = v_az - v_grav_z;

                    computeEuler(p_data);
                }
            }
        }
        
        p_data.timestamp = millis();
        p_data.motion = _isSignificantMoving;
        _imu.getStepCount(&p_data.stepCount);

        // 로깅 데이터 구성
        if (_opts.useSD && _sd) {
            char v_line[160];
            snprintf(v_line, sizeof(v_line), "%u,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,%.1f,%u,%d\n",
                     p_data.timestamp, 
                     p_data.linAcc[0], p_data.linAcc[1], p_data.linAcc[2],
                     p_data.quat[0], p_data.quat[1], p_data.quat[2], p_data.quat[3],
                     p_data.euler[0], p_data.euler[1], p_data.euler[2],
                     p_data.stepCount, p_data.motion);
            _sd->logToBuffer(v_line);
        }
    }

    void checkMotionStatus() {
        uint16_t v_status = 0;
        _imu.getInterruptStatus(&v_status);
        
        if (v_status & BMI270_SIG_MOT_STATUS_MASK) {
            _isSignificantMoving = true;
        }
        if (v_status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            if (_opts.dynamicPowerSave) enterIdleMode();
        }
    }

    void enterIdleMode() {
        Serial.println(F(">>> System: Enter Light Sleep"));
        
        if (g_A10_TaskHandle_Sensor != NULL) vTaskSuspend(g_A10_TaskHandle_Sensor);
        
        _imu.flushFIFO();
        if (_sd) _sd->end(); // SD_MMC 세션 종료 (전력 절감)

        // 인터럽트 핀을 통한 복귀 설정 (EXT0)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();

        // --- 슬립 복구 시점 ---
        resumeFromIdle();
        if (g_A10_TaskHandle_Sensor != NULL) vTaskResume(g_A10_TaskHandle_Sensor);
        Serial.println(F("<<< System: Resumed"));
    }

    void resumeFromIdle() {
        if (_sd) {
            _sd->begin();
            _sd->createLogFile(_opts.logPrefix, "Time,Lx,Ly,Lz,QW,QX,QY,QZ,R,P,Y,Steps,Mot");
        }
        configureIMU(); // 센서 재설정
    }

private:
    BMI270 _imu;
    ST_BMI270_Options_t _opts;
    CL_SD10_SDMMC_Handler* _sd;
    VQF* _vqf = nullptr;
    bool _isSignificantMoving = false;
    BMI270_SensorData _fifoBuffer[C10_Config::FIFO_WTM_COUNT];

    void computeEuler(ST_FullSensorPayload_t& d) {
        // Rad to Deg 변환 (57.29577f) 적용
        d.euler[0] = atan2(2.0f*(d.quat[0]*d.quat[1]+d.quat[2]*d.quat[3]), 1.0f-2.0f*(d.quat[1]*d.quat[1]+d.quat[2]*d.quat[2])) * 57.29577f;
        d.euler[1] = asin(2.0f*(d.quat[0]*d.quat[2]-d.quat[3]*d.quat[1])) * 57.29577f;
        d.euler[2] = atan2(2.0f*(d.quat[0]*d.quat[3]+d.quat[1]*d.quat[2]), 1.0f-2.0f*(d.quat[3]*d.quat[3]+d.quat[2]*d.quat[2])) * 57.29577f;
    }

    void setupMotionInterrupts() {
        _imu.enableFeature(BMI2_SIG_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);
        _imu.mapInterruptToPin(BMI2_SIG_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);
    }
};

