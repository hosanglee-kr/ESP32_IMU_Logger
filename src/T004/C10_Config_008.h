/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_008.h
 * 모듈약어 : C10 (Config)
 * 모듈명 : Global System Configuration & RTOS Parameters
 * ------------------------------------------------------
 * 기능 요약
 * - 하드웨어 핀맵 및 SPI 클럭 설정
 * - FreeRTOS Queue 크기 및 태스크 우선순위 정의
 * - BMI270 FIFO 워터마크 및 모션 감지 파라미터 관리
 * - VQF 필터 튜닝 변수 (자이로 드리프트 학습 속도 등) 정의
 * ------------------------------------------------------
 */
#pragma once
#include <Arduino.h>

namespace C10_Config {
    // [Hardware] 핀맵 정의
    static constexpr int BMI_CS    = 10;
    static constexpr int BMI_INT1  = 9;
    static constexpr const char* SD_MOUNT = "/sdcard";
    static constexpr const char* CONFIG_PATH = "/config.json";

    // [Sensor] FIFO 및 샘플링 설정
    static constexpr uint16_t FIFO_WTM = 200; // FIFO 데이터 임계값 (바이트 단위)
    static constexpr float SAMPLE_RATE_ACTIVE = 200.0f; // 200Hz

    // [VQF Tuning]
    static constexpr float VQF_TAU_ACC = 3.0f;  // 가속도계 보정 시정수 (초)
    static constexpr float VQF_TAU_MAG = 0.0f;  // 지자기 미사용 시 0

    // [RTOS] Queue & Task 설정
    static constexpr int QUEUE_LEN_SD    = 50;  // SD 기록 대기 큐 크기
    static constexpr int QUEUE_LEN_DEBUG = 30;  // 시리얼 출력 대기 큐 크기
    static constexpr int TASK_STACK_SIZE = 4096;

    // [Motion] 전력 관리 파라미터
    static constexpr uint16_t NO_MOTION_DURATION = 500; // 10초 (500 * 20ms)
    static constexpr uint16_t NO_MOTION_THRESHOLD = 80; // 40mg
}

struct ST_BMI270_Options_t {
    bool useVQF = true;
    bool useSD = true;
    bool autoCalibrate = true;
    bool dynamicPowerSave = true;
    bool recordOnlySignificant = true;
    char logPrefix[16] = "T03";
};
