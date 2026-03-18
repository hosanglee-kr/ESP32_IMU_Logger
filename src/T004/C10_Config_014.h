/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_014.h
 * 모듈약어 : C10 (Config)
 * 모듈명 : Global System Configuration & Hardware Mapping
 * ------------------------------------------------------
 * 기능 요약
 * - ESP32-S3 기본 SD_MMC(1-bit) 및 BMI270 SPI 핀맵 정의
 * - 센서 샘플링(200Hz) 및 FIFO 워터마크 파라미터 관리
 * - FreeRTOS 태스크 우선순위 및 스택 할당량 정의
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [C10] 시스템의 모든 하드웨어 의존적 상수를 한곳에서 관리
 * - 타 모듈은 이 상수를 참조하여 동작하며, 하드웨어 변경 시 이 파일만 수정
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>

namespace C10_Config {
    // [Hardware] BMI270 SPI 핀 (ESP32-S3 Standard)
    static constexpr uint8_t     BMI_CS              = 10;
    static constexpr uint8_t     BMI_SCK             = 12;
    static constexpr uint8_t     BMI_MISO            = 13;
    static constexpr uint8_t     BMI_MOSI            = 11;
    static constexpr uint8_t     BMI_INT1            = 9;

    // [Hardware] SD_MMC 1-bit 모드 핀 (ESP32-S3 Default)
    // S3의 경우 Slot 1(GPIO 38, 39, 40)이 기본 핀입니다.
    static constexpr uint8_t     SD_MMC_CLK          = 39;
    static constexpr uint8_t     SD_MMC_CMD          = 38;
    static constexpr uint8_t     SD_MMC_D0           = 40;

    // [Filesystem] 경로 설정
    static constexpr const char* SD_MOUNT            = "/sdcard";
    static constexpr const char* CONFIG_PATH         = "/config.json";

    // [Sensor] FIFO 및 샘플링 설정 (200Hz)
    static constexpr uint16_t    FIFO_WTM            = 200;    
    static constexpr float       SAMPLE_RATE_ACTIVE  = 200.0f; 
    static constexpr uint16_t    FIFO_WTM_COUNT      = 15;     

    // [VQF Tuning]
    static constexpr float       VQF_TAU_ACC         = 3.0f;  
    static constexpr float       VQF_TAU_MAG         = 0.0f;  

    // [RTOS] Queue & Task 설정
    static constexpr uint16_t    QUEUE_LEN_DEBUG     = 30;  
    static constexpr uint32_t    TASK_STACK_SIZE     = 4096;
    
    // [Motion] 전력 관리 파라미터
    static constexpr uint16_t    NO_MOTION_DURATION  = 500; // 10초
    static constexpr uint16_t    NO_MOTION_THRESHOLD = 80;  // 40mg
}

struct ST_BMI270_Options_t {
    bool useVQF                 = true;
    bool useSD                  = true;
    bool autoCalibrate          = true;
    bool dynamicPowerSave       = true;
    bool recordOnlySignificant   = true;
    char logPrefix[16]          = "T04";
};


