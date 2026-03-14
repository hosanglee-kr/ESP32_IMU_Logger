/**
 * @file T03_Config_006.h
 * @brief 시스템 전역 설정 및 전력 관리 파라미터 정의
 * @details BMI270 핀맵, SD 마운트 경로, 샘플링 레이트 및 절전 모드 관련 상수 관리
 */
#pragma once
#include <Arduino.h>

namespace Config {
    static constexpr int BMI_CS    = 10;
    static constexpr int BMI_INT1  = 9;  // Any-Motion 인터럽트 핀
    static constexpr const char* SD_MOUNT = "/sdcard";
    static constexpr const char* CONFIG_PATH = "/config.json";

    // 동작 상태별 샘플링 레이트
    static constexpr float SAMPLE_RATE_ACTIVE = 200.0f;
    static constexpr float SAMPLE_RATE_IDLE   = 25.0f;
    
    // 전력 관리 파라미터
    static constexpr uint16_t NO_MOTION_DURATION = 500; // 500 * 20ms = 10초간 정지 시 IDLE 진입
    static constexpr uint16_t NO_MOTION_THRESHOLD = 80; // 약 40mg (민감도)
}

struct BMI270_Options {
    bool useVQF = true;
    bool useSD = true;
    bool autoCalibrate = true;
    bool dynamicPowerSave = true; // [신규] 동적 절전 활성화
    // [추가] 로깅 트리거 설정
    bool recordOnlySignificant = true; // [신규] 유의미한 동작일 때만 SD 기록
    char logPrefix[16] = "log";
};
