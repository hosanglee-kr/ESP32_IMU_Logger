/*
 * ------------------------------------------------------
 * 소스명 : T03_Config_006.h
 * 모듈약어 : T03 (Config)
 * 모듈명 : Global System Configuration & Power Parameters
 * ------------------------------------------------------
 * 기능 요약
 * - 하드웨어 핀맵 정의 (BMI270 SPI CS, INT1)
 * - SD 카드 마운트 경로 및 설정 파일 경로 관리
 * - 동작 모드별(Active/Idle) 샘플링 레이트 상수 정의
 * - 전력 관리용 정지 감지(No-Motion) 시간 및 임계값 설정
 * - 시스템 전체 옵션 구조체(BMI270_Options) 정의
 * ------------------------------------------------------
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
