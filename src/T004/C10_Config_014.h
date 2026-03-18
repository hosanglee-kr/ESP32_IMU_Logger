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
 * [System Tuning Guide]
 * 1. 데이터 정밀도 vs 저장 속도 (SB10 / SD10)
 * - 샘플링 200Hz 유지 시 SD10_BUF_SIZE는 최소 8KB(8192) 권장.
 * - SD_MMC 1-bit 클럭은 전선 길이에 따라 20MHz(안정) ~ 40MHz(고속) 조절.
 * 2. 자세 추정 반응성 (VQF)
 * - VQF_TAU_ACC: 낮을수록(1.0) 가속도계에 빠르게 반응(기울기 보정 빠름), 
 * 높을수록(5.0) 진동 노이즈에 강함. 드론/모터 기기엔 3.0~5.0 권장.
 * 3. 모션 감지 감도 (Motion)
 * - NO_MOTION_THRESHOLD: 80(40mg) ~ 120(60mg). 환경 진동이 크면 값을 높임.
 * - NO_MOTION_DURATION: 500(10초). 불필요한 슬립 진입 방지를 위해 조절.
 * ------------------------------------------------------
 
 * [C10 Tuning Guide]
 * 1. SAMPLE_RATE_ACTIVE: 200Hz 초과 설정 시 SPI 클럭(BMI_SPI_FREQ)을 10MHz 이상으로 검토.
 * 2. FIFO_WTM_COUNT: 값이 작을수록 지연시간 감소하나 CPU 오버헤드 증가. 15~25 권장.
 * 3. SD_MMC_FREQ: 1-bit 모드에서 20MHz~40MHz 사이 튜닝 (배선 길이에 따라 결정).
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
    
    static constexpr uint32_t    BMI_SPI_FREQ        = 8000000;  // 8MHz SPI

    // [Hardware] SD_MMC 1-bit 모드 핀 (ESP32-S3 Default)
    // S3의 경우 Slot 1(GPIO 38, 39, 40)이 기본 핀입니다.
    static constexpr uint8_t     SD_MMC_CLK          = 39;
    static constexpr uint8_t     SD_MMC_CMD          = 38;
    static constexpr uint8_t     SD_MMC_D0           = 40;
    
    static constexpr uint32_t    SD_MMC_FREQ         = 20000000; // 20MHz (안정적 1-bit)


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
    
    // ESP32-S3 Power Management
    static constexpr bool        ENABLE_LIGHT_SLEEP  = true;
    
    
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


