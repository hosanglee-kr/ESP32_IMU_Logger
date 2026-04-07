/* ============================================================================
 * File: T210_Def_Com_217.h
 * Summary: 시스템 전역 상수 및 경로 최적화 (v217)
 * ========================================================================== */
#pragma once

#include <Arduino.h>

namespace T20 {

    // --- [1] 시스템 공통 (Common) ---
    namespace C10_Sys {
        inline constexpr char const* VERSION             = "T20_MFCC_v217";
        inline constexpr uint8_t  PIN_NOT_SET            = 0xFFU; // 통합된 미할당 핀 표기
        inline constexpr uint16_t QUEUE_LEN              = 8U;
        inline constexpr uint16_t PROFILE_COUNT          = 4U;
        inline constexpr uint16_t RAW_FRAME_BUFFERS      = 4U;
    }

    // --- [2] 하드웨어 핀 맵 (ESP32-S3) ---
    namespace C10_Pin {
        inline constexpr uint8_t BTN_CONTROL             = 0U;
        inline constexpr uint8_t RGB_LED                 = 21U;

        // BMI270 SPI
        inline constexpr uint8_t BMI_SCK                 = 12U;
        inline constexpr uint8_t BMI_MISO                = 13U;
        inline constexpr uint8_t BMI_MOSI                = 11U;
        inline constexpr uint8_t BMI_CS                  = 10U;
        inline constexpr uint8_t BMI_INT1                = 14U;

        // SD_MMC (기본 1-bit 설정)
        inline constexpr uint8_t SDMMC_CLK               = 39U;
        inline constexpr uint8_t SDMMC_CMD               = 38U;
        inline constexpr uint8_t SDMMC_D0                = 40U;
        inline constexpr uint8_t SDMMC_D1                = C10_Sys::PIN_NOT_SET;
        inline constexpr uint8_t SDMMC_D2                = C10_Sys::PIN_NOT_SET;
        inline constexpr uint8_t SDMMC_D3                = C10_Sys::PIN_NOT_SET;
    }

    // --- [3] DSP 파라미터 ---
    namespace C10_DSP {
        inline constexpr uint16_t FFT_SIZE               = 256U;
        inline constexpr uint16_t FFT_BINS               = (FFT_SIZE / 2U) + 1U;
        inline constexpr float    SAMPLE_RATE_HZ         = 1600.0f;
        
        inline constexpr uint16_t MEL_FILTERS            = 26U;
        inline constexpr uint16_t MFCC_COEFFS_MAX        = 32U;
        inline constexpr uint16_t MFCC_COEFFS_DEF        = 13U;
        
        inline constexpr uint16_t FEATURE_DIM_DEF        = MFCC_COEFFS_DEF * 3U; // 39
        inline constexpr uint16_t MFCC_HISTORY_LEN       = 5U;
    }

    // --- [4] 파일 시스템 경로 (Path) ---
    namespace C10_Path {
        inline constexpr char const* MOUNT_SD            = "/sdcard";
        inline constexpr char const* DIR_SYS             = "/sys";
        inline constexpr char const* DIR_WEB             = "/www";
        
        // 주요 파일명
        inline constexpr char const* FILE_CFG_JSON       = "/sys/runtime_cfg.json";
        inline constexpr char const* FILE_REC_IDX        = "/sys/recorder_index.json";
        inline constexpr char const* FILE_BMI_CALIB      = "/sys/bmi_calib.json";
        inline constexpr char const* WEB_INDEX           = "index_214_003.html"; // 실제 LittleFS 내 파일명
        
        // SD 데이터 접두사
        inline constexpr char const* SD_DIR_BIN          = "/t20_data/bin";
        inline constexpr char const* SD_PREFIX_BIN       = "/t20_data/bin/rec_";
    }
}

// 공통 열거형 (State/Result)
typedef enum {
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,
    EN_T20_STATE_RUNNING,
    EN_T20_STATE_ERROR,
    EN_T20_STATE_BUSY
} EM_T20_State_t;
