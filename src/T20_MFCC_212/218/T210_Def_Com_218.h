/* ============================================================================
 * File: T210_Def_Com_218.h
 * Summary: 시스템 전역 상수 (v217 Full)
 * ========================================================================== */
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

namespace T20 {
    namespace C10_Sys {
        inline constexpr char const* VERSION_STR           = "T20_Mfcc_v217";
        inline constexpr uint16_t QUEUE_LEN                = 8U;
        inline constexpr uint16_t CFG_PROFILE_COUNT        = 4U;
        inline constexpr uint16_t RAW_FRAME_BUFFERS        = 4U;
        inline constexpr uint16_t SEQUENCE_FRAMES_MAX      = 16U;
        inline constexpr uint8_t  PIN_NOT_SET              = 0xFFU;
    }

    namespace C10_Pin {
        inline constexpr uint8_t BTN_CONTROL = 0U;
        inline constexpr uint8_t RGB_LED     = 21U;
        inline constexpr uint8_t BMI_SCK     = 12U;
        inline constexpr uint8_t BMI_MISO    = 13U;
        inline constexpr uint8_t BMI_MOSI    = 11U;
        inline constexpr uint8_t BMI_CS      = 10U;
        inline constexpr uint8_t BMI_INT1    = 14U;
        inline constexpr uint8_t SDMMC_CLK   = 39U;
        inline constexpr uint8_t SDMMC_CMD   = 38U;
        inline constexpr uint8_t SDMMC_D0    = 40U;
        inline constexpr uint8_t SDMMC_D1    = C10_Sys::PIN_NOT_SET;
        inline constexpr uint8_t SDMMC_D2    = C10_Sys::PIN_NOT_SET;
        inline constexpr uint8_t SDMMC_D3    = C10_Sys::PIN_NOT_SET;
    }

    namespace C10_Task {
        inline constexpr uint32_t SENSOR_STACK   = 6144U;
        inline constexpr uint32_t PROCESS_STACK  = 12288U;
        inline constexpr uint32_t RECORDER_STACK = 8192U;
        inline constexpr uint8_t  SENSOR_PRIO    = 4U;
        inline constexpr uint8_t  PROCESS_PRIO   = 3U;
        inline constexpr uint8_t  RECORDER_PRIO  = 2U;
    }

    namespace C10_DSP {
        inline constexpr uint16_t FFT_SIZE         = 256U;
        inline constexpr uint16_t FFT_BINS         = (FFT_SIZE / 2U) + 1U;
        inline constexpr float    SAMPLE_RATE_HZ   = 1600.0f;
        inline constexpr uint16_t MEL_FILTERS      = 26U;
        inline constexpr uint16_t MFCC_COEFFS_MAX  = 32U;
        inline constexpr uint16_t MFCC_COEFFS_DEF  = 13U;
        inline constexpr uint16_t MFCC_HISTORY_LEN = 5U;
    }

    namespace C10_BMI {
        inline constexpr uint32_t SPI_FREQ_HZ      = 10000000UL;
        inline constexpr uint8_t  REG_CALIB_OFFSET_START = 0x71U;
    }

    namespace C10_Rec {
        inline constexpr uint32_t BINARY_MAGIC     = 0x54323042UL;
        inline constexpr uint16_t BINARY_VERSION   = 1U;
        inline constexpr uint16_t BATCH_WMARK_HIGH = 8U;
        inline constexpr uint32_t BATCH_IDLE_FLUSH_MS = 250U;
        inline constexpr uint16_t ROTATE_KEEP_MAX  = 8U;
    }

    namespace C10_Web {
        inline constexpr char const* WS_URI        = "/api/t20/ws";
        inline constexpr uint16_t JSON_BUF_SIZE    = 2048U;
        inline constexpr uint16_t LARGE_JSON_BUF_SIZE = 8192U;
        inline constexpr uint32_t BTN_DEBOUNCE_MS  = 500U;
    }

    namespace C10_Net {
        inline constexpr uint8_t WIFI_MULTI_MAX    = 3U;
    }

    namespace C10_Path {
        inline constexpr char const* MOUNT_SD      = "/sdcard";
        inline constexpr char const* DIR_SYS       = "/sys";
        inline constexpr char const* DIR_WEB       = "/www";
        inline constexpr char const* FILE_CFG_JSON = "/sys/t20_runtime_cfg_217_006.json";
        inline constexpr char const* FILE_REC_IDX  = "/sys/recorder_index.json";
        inline constexpr char const* FILE_BMI_CALIB= "/sys/bmi_calib.json";
        inline constexpr char const* WEB_INDEX     = "index_217_006.html";
        inline constexpr char const* SD_DIR_BIN    = "/t20_data/bin";
        inline constexpr char const* SD_PREFIX_BIN = "/t20_data/bin/rec_";
    }
}
