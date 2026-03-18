/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_014.h
 * 모듈약어 : C10 (Config)
 * 모듈명 : Global System Configuration & Hardware Mapping
 * ------------------------------------------------------
 * 기능 요약
 * - ESP32-S3 전용 SD_MMC(1-bit) 및 BMI270 SPI 핀맵 정의
 * - 센서 샘플링(200Hz) 및 FIFO 워터마크 파라미터 관리
 * - 시스템 전력 관리(Light Sleep) 및 모션 감지 임계치 설정
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [C10] 시스템의 모든 하드웨어 의존적 상수를 독점 관리
 * - 타 모듈의 로직 수정 없이 오직 상수 변경만으로 성능 튜닝 수행
 * ------------------------------------------------------
 * [System Tuning Guide]
 * 1. 데이터 정밀도 vs 저장 속도 (SB10 / SD10)
 * - 샘플링 200Hz 유지 시 SD10_BUF_SIZE는 8192(8KB) 이상 권장.
 * - SD_MMC 클럭(SD_MMC_FREQ)은 배선 길이에 따라 20MHz ~ 40MHz 조절.
 * 2. 자세 추정 반응성 (VQF)
 * - VQF_TAU_ACC: 낮을수록(1.0) 반응 빠름, 높을수록(5.0) 진동에 강함.
 * 3. 모션 감지 및 전력 (Motion)
 * - NO_MOTION_THRESHOLD: 환경 진동이 크면 값을 높여(120 이상) 오작동 방지.
 * - ENABLE_LIGHT_SLEEP: 정지 상태에서 소모 전류를 최소화하려면 true 설정.
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
    
    // SPI 통신 속도 (연산량과 전력 소모의 균형)
    static constexpr uint32_t    BMI_SPI_FREQ        = 8000000;  // 8MHz

    // [Hardware] SD_MMC 1-bit 모드 핀 (ESP32-S3 Default Slot 1)
    static constexpr uint8_t     SD_MMC_CLK          = 39;
    static constexpr uint8_t     SD_MMC_CMD          = 38;
    static constexpr uint8_t     SD_MMC_D0           = 40;
    
    // SD_MMC 통신 속도 (1-bit 모드 안정성 위주)
    static constexpr uint32_t    SD_MMC_FREQ         = 20000000; // 20MHz

    // [Filesystem] 경로 및 마운트 설정
    static constexpr const char* SD_MOUNT            = "/sdcard";
    static constexpr const char* CONFIG_PATH         = "/config.json";

    // [Sensor] FIFO 및 샘플링 설정 (200Hz 최적화)
    static constexpr uint16_t    FIFO_WTM            = 200;    
    static constexpr float       SAMPLE_RATE_ACTIVE  = 200.0f; 
    static constexpr uint16_t    FIFO_WTM_COUNT      = 15;     

    // [VQF Tuning] 중력 보정 및 자세 융합 파라미터
    static constexpr float       VQF_TAU_ACC         = 3.0f;  
    static constexpr float       VQF_TAU_MAG         = 0.0f;  // 지자계 미사용 시 0.0

    // [RTOS] Queue & Task 관리 (메모리 안정성)
    static constexpr uint16_t    QUEUE_LEN_DEBUG     = 30;  
    static constexpr uint32_t    TASK_STACK_SIZE     = 5120; // 5KB로 상향 (VQF/JSON 대응)
    
    // [Power Management] ESP32-S3 저전력 설정
    static constexpr bool        ENABLE_LIGHT_SLEEP  = true;
    
    // [Motion] 모션 감지 파라미터
    static constexpr uint16_t    NO_MOTION_DURATION  = 500;  // 약 10초 (20ms * 500)
    static constexpr uint16_t    NO_MOTION_THRESHOLD = 80;   // 40mg (환경에 따라 조절)
}

// 모듈 간 옵션 전달용 구조체
struct ST_BMI270_Options_t {
    bool useVQF                  = true;
    bool useSD                   = true;
    bool autoCalibrate           = true;
    bool dynamicPowerSave        = true;
    bool recordOnlySignificant    = true;
    char logPrefix[16]           = "T04";
};
