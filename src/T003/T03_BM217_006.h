
/**
 * @file T03_BM217_006.h
 * @brief BMI270 센서 핸들러: Any(깨우기) / Significant(기록) / No(절전) 통합 구현
 */
#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "T03_Config_006.h"
#include "T03_SDMMC_006.h"

class BMI270Handler {
public:
    bool begin(BMI270_Options opts, SDMMCHandler* sd) {
        _opts = opts; 
        _sd   = sd;
        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;

        setupInterrupts(); 
        configureFeatures(); // [추가] Significant Motion 세부 파라미터 설정
        return true;
    }

    void enterIdleMode() {
        Serial.println(">>> Entering IDLE (Light Sleep)");
        if (_opts.useSD) _sd->end(); 

        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_25HZ);
        _imu.enableAdvancedPowerSave();

        // Any-Motion 인터럽트로 ESP32-S3를 깨우도록 설정 (반응성 극대화)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)Config::BMI_INT1, 1);
        esp_light_sleep_start();

        resumeFromIdle();
    }

private:
    BMI270         _imu; 
    BMI270_Options _opts; 
    SDMMCHandler* _sd;
    bool           _isSignificantMoving = false; // Significant Motion 발생 여부

    void setupInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);
        _imu.enableFeature(BMI2_SIG_MOTION);

        // 모든 인터럽트 소스를 INT1 한 곳으로 모음
        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1); // Wake-up 용
        _imu.mapInterruptToPin(BMI2_SIG_MOTION_INT, BMI2_INT1); // 기록 트리거 용
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);  // 절전 진입 용

        bmi2_int_pin_config pcfg;
        pcfg.pin_type = BMI2_INT1;
        pcfg.int_latch = BMI2_INT_NON_LATCH;
        pcfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
        pcfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
        _imu.setInterruptPinConfig(pcfg);
    }

    // [추가] 유의미한 동작 판단 시간 튜닝
    void configureFeatures() {
        bmi2_sens_config config;
        config.type = BMI2_SIG_MOTION;
        // block_size: 250 = 5초, 100 = 2초. 
        // 너무 짧으면 오작동, 너무 길면 기록 시작이 늦어짐.
        config.cfg.sig_motion.block_size = 150; // 약 3초로 튜닝
        _imu.setConfig(config);
    }

    void resumeFromIdle() {
        Serial.println("<<< Resumed (Active Mode)");
        _imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.disableAdvancedPowerSave();

        if (_opts.useSD) {
            _sd->begin();
            // 재포맷팅 지연 최소화를 위해 파일 생성만 수행
            _sd->createLogFile(_opts.logPrefix, "Time,Ax,Ay,Az");
        }
    }

public:
    void checkMotionStatus() {
        uint16_t status = 0;
        _imu.getInterruptStatus(&status);
        
        // Significant Motion: "진짜로 이동 중이구나!" -> SD 기록 시작
        if (status & BMI270_SIG_MOT_STATUS_MASK) {
            if (!_isSignificantMoving) {
                Serial.println("Motion: SIGNIFICANT - Start Logging");
                _isSignificantMoving = true;
            }
        }
    
        // No-Motion: "다시 멈췄구나" -> 기록 중단 및 절전 모드
        if (status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            enterIdleMode();
        }
    }
    
    bool shouldRecord() {
        // recordOnlySignificant 옵션이 꺼져있으면 깨어나자마자 기록, 켜져있으면 판단 완료 후 기록
        if (!_opts.recordOnlySignificant) return true; 
        return _isSignificantMoving; 
    }
};
