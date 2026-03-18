/**
 * @file T03_BM217_005.h
 * @brief BMI270 센서 핸들러: Any/No Motion 인터럽트 기반 동적 절전 구현
 */
#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "T03_Config_005.h"
#include "T03_SDMMC_005.h"

class BMI270Handler {
public:
    bool begin(BMI270_Options opts, SDMMCHandler* sd) {
        _opts = opts; _sd = sd;
        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;

        setupInterrupts(); // 인터럽트 엔진 설정
        return true;
    }

    // [핵심] 절전 모드 진입 로직
    void enterIdleMode() {
        Serial.println(">>> Entering IDLE (Light Sleep)");
        
        // 1. SD 카드 정리
        if (_opts.useSD) _sd->end(); 

        // 2. BMI270을 저전력 모드로 변경
        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_25HZ);
        _imu.enableAdvancedPowerSave();

        // 3. ESP32-S3 Wake-up 설정 (BMI270 INT1 신호가 High가 되면 깨어남)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)Config::BMI_INT1, 1);
        
        // 4. Light Sleep 진입 (RAM 유지, 전류 ~240uA)
        esp_light_sleep_start();

        // --- 깨어난 후 ---
        resumeFromIdle();
    }

private:
    BMI270 _imu; BMI270_Options _opts; SDMMCHandler* _sd;

    void setupInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);

        // Any-Motion 설정: 움직임 감지 시 즉시 인터럽트
        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);
        
        // No-Motion 설정: 정지 상태 지속 시 인터럽트
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);

        // 인터럽트 핀 전기적 설정 (Active High, Push-Pull)
        bmi2_int_pin_config pcfg;
        pcfg.pin_type = BMI2_INT1;
        pcfg.int_latch = BMI2_INT_NON_LATCH;
        pcfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
        pcfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
        _imu.setInterruptPinConfig(pcfg);
    }

    void resumeFromIdle() {
        Serial.println("<<< Resumed (Active Mode)");
        // 1. 센서 복구: 고속 샘플링 및 성능 모드
        _imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.disableAdvancedPowerSave();

        // 2. SD 카드 복구: 지연 최소화를 위해 즉시 재마운트
        if (_opts.useSD) {
            _sd->begin();
            _sd->createLogFile(_opts.logPrefix, "Time,Ax,Ay,Az"); // 새 파일 생성
        }
    }

public:
    // 메인 루프에서 상태 체크
    void checkMotionStatus() {
        uint16_t status = 0;
        _imu.getInterruptStatus(&status);
        
        // No-Motion 발생 시 IDLE 진입
        if (status & BMI270_NO_MOT_STATUS_MASK) {
            enterIdleMode();
        }
    }
};
