/*
 * ------------------------------------------------------
 * 소스명 : T03_BM217_006.h
 * 모듈약어 : T03 (BMI270)
 * 모듈명 : Integrated Motion Sensor Handler with Power Logic
 * ------------------------------------------------------
 * 기능 요약
 * - BMI270 SPI 통신 제어 및 센서 특징 설정
 * - VQF 필터 기반 6축 데이터 융합 및 쿼터니언/오일러 각 산출
 * - 자이로 바이어스(Bias) 보정 및 데이터 정규화
 * - 이중 동작 감지 로직: Any(즉시 깨우기) / Significant(이동 기록)
 * - ESP32-S3 Light Sleep 진입 및 하드웨어 인터럽트 기반 복구 제어
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "VQF.h"  // 기존 VQF 필터 라이브러리
#include "T03_Config_006.h"
#include "T03_SDMMC_006.h"

// 센서 데이터를 담을 구조체
struct FullSensorPayload {
    uint32_t timestamp;
    float acc[3], gyro[3], quat[4], euler[3];
    uint32_t stepCount;
    uint8_t gesture;
    bool motion;
};

// ISR(Interrupt Service Routine) 최적화: RAM에 상주하여 속도 향상
DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;
void IRAM_ATTR bmi270_global_isr() { 
    g_bmi270_fifo_ready = true; 
}

class BMI270Handler {
public:
    bool begin(BMI270_Options opts, SDMMCHandler* sd) {
        _opts = opts; _sd = sd;
        
        // SPI 통신 시작
        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;

        // 1. VQF 필터 초기화
        if (_opts.useVQF) {
            _vqf = new VQF(1.0f / Config::SAMPLE_RATE_ACTIVE);
        }

        // 2. 인터럽트 및 특징 설정
        setupInterrupts();
        configureFeatures();
        
        // 3. 자이로 바이어스 초기화
        for(int i=0; i<3; i++) _gyroBias[i] = 0;

        // 하드웨어 인터럽트 핀 연결
        pinMode(Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::BMI_INT1), bmi270_global_isr, RISING);

        return true;
    }

    // 자이로 바이어스(오프셋) 수동 설정
    void setGyroBias(float gx, float gy, float gz) {
        _gyroBias[0] = gx; _gyroBias[1] = gy; _gyroBias[2] = gz;
    }

    // 메인 루프에서 호출되는 데이터 업데이트 함수
    void updateData(FullSensorPayload& d) {
        _imu.getSensorData(); 
        
        d.timestamp = millis();
        d.acc[0] = _imu.data.accelX; 
        d.acc[1] = _imu.data.accelY; 
        d.acc[2] = _imu.data.accelZ;
        
        // 바이어스 보정 및 라디안 변환
        d.gyro[0] = (_imu.data.gyroX * 0.0174533f) - _gyroBias[0];
        d.gyro[1] = (_imu.data.gyroY * 0.0174533f) - _gyroBias[1];
        d.gyro[2] = (_imu.data.gyroZ * 0.0174533f) - _gyroBias[2];

        // VQF 센서 융합 연산
        if (_opts.useVQF && _vqf) {
            _vqf->update(d.gyro, d.acc);
            _vqf->getQuaternion(d.quat);
            computeEuler(d);
        }

        d.motion = _isSignificantMoving;
        _imu.getStepCount(&d.stepCount);
        _imu.getWristGesture(&d.gesture);
    }

    // 초절전 Idle 모드 진입
    void enterIdleMode() {
        Serial.println(">>> Entering IDLE (Light Sleep)");
        if (_opts.useSD) _sd->end(); 

        // 센서 저전력 설정
        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_25HZ);
        _imu.enableAdvancedPowerSave();
        
        // 인터럽트 기반 깨우기 설정 (Any-motion 인터럽트 대기)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)Config::BMI_INT1, 1);
        esp_light_sleep_start();
        
        // --- 슬립 복구 후 지점 ---
        resumeFromIdle();
    }

    // 동작 상태 모니터링 (Significant/No Motion 판별)
    void checkMotionStatus() {
        uint16_t status = 0;
        _imu.getInterruptStatus(&status);
        
        if (status & BMI270_SIG_MOT_STATUS_MASK) {
            if (!_isSignificantMoving) {
                Serial.println("Motion: SIGNIFICANT - Record Started");
                _isSignificantMoving = true;
            }
        }
    
        if (status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            enterIdleMode();
        }
    }
    
    bool shouldRecord() { 
        return (!_opts.recordOnlySignificant) || _isSignificantMoving; 
    }

private:
    BMI270 _imu;
    BMI270_Options _opts;
    SDMMCHandler* _sd;
    VQF* _vqf = nullptr;
    float _gyroBias[3];
    bool _isSignificantMoving = false;

    // 쿼터니언 -> 오일러 각 변환
    void computeEuler(FullSensorPayload& d) {
        float q0 = d.quat[0], q1 = d.quat[1], q2 = d.quat[2], q3 = d.quat[3];
        d.euler[0] = atan2(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * 57.29578f;
        d.euler[1] = asin(2.0f * (q0 * q2 - q3 * q1)) * 57.29578f;
        d.euler[2] = atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q3 * q3 + q2 * q2)) * 57.29578f;
    }

    // 슬립 종료 후 장치 복구 시퀀스
    void resumeFromIdle() {
        Serial.println("<<< Resumed (Active Mode)");
        _imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.disableAdvancedPowerSave();

        if (_vqf) _vqf->setSamplingTime(1.0f / Config::SAMPLE_RATE_ACTIVE);

        if (_opts.useSD) {
            _sd->begin();
            _sd->createLogFile(_opts.logPrefix, "Time,Ax,Ay,Az,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Mot");
        }
    }

    void setupInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);
        _imu.enableFeature(BMI2_SIG_MOTION);

        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_SIG_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);

        bmi2_int_pin_config pcfg;
        pcfg.pin_type = BMI2_INT1;
        pcfg.int_latch = BMI2_INT_NON_LATCH;
        pcfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
        pcfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
        _imu.setInterruptPinConfig(pcfg);
    }

    void configureFeatures() {
        bmi2_sens_config config;
        config.type = BMI2_SIG_MOTION;
        config.cfg.sig_motion.block_size = 150; // 판단 주기 약 3초
        _imu.setConfig(config);
    }
};
