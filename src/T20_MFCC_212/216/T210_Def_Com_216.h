/* ============================================================================
 * File: T210_Def_Com_216_03.h
 * Summary: 시스템 전역 상수 (v217: LittleFS/SDMMC 폴더 아키텍처 및 Path 분리 반영)
 * Compiler: gnu++17 기준 최적화
 * ========================================================================== */
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

namespace T20 {

    // --- 1. 시스템 및 RTOS 공통 ---
    namespace C10_Sys {
        inline constexpr char const* VERSION_STR           = "T20_Mfcc_v217"; // 217 업데이트
        inline constexpr uint16_t QUEUE_LEN                = 8U;
        inline constexpr uint16_t CFG_PROFILE_COUNT        = 4U;
        inline constexpr uint16_t RUNTIME_CFG_PROFILE_NAME_MAX = 32U;
        inline constexpr uint16_t RAW_FRAME_BUFFERS        = 4U;
        inline constexpr uint16_t SEQUENCE_FRAMES_MAX      = 16U;
        inline constexpr uint16_t SEQUENCE_FRAMES_DEFAULT  = 16U;
    }

    // --- 2. 하드웨어 핀 맵 (ESP32-S3) ---
    namespace C10_Pin {
        inline constexpr uint8_t SPI_SCK     = 12U;
        inline constexpr uint8_t SPI_MISO    = 13U;
        inline constexpr uint8_t SPI_MOSI    = 11U;

        inline constexpr uint8_t BMI_CS      = 10U;
        inline constexpr uint8_t BMI_INT1    = 14U;

		inline constexpr uint8_t BTN_CONTROL = 0U;
    }

    // --- 3. RTOS 태스크 파라미터 ---
    namespace C10_Task {
        inline constexpr uint32_t SENSOR_STACK   = 6144U;
        inline constexpr uint32_t PROCESS_STACK  = 12288U;
        inline constexpr uint32_t RECORDER_STACK = 8192U;
        inline constexpr uint8_t  SENSOR_PRIO    = 4U;
        inline constexpr uint8_t  PROCESS_PRIO   = 3U;
        inline constexpr uint8_t  RECORDER_PRIO  = 2U;
    }

    // --- 4. DSP & 수학 연산 한계치 ---
    namespace C10_DSP {
        inline constexpr float    MATH_PI          = 3.14159265358979323846f;
        inline constexpr float    EPSILON          = 1.0e-12f;

        inline constexpr uint16_t FFT_SIZE         = 256U;
		// [계산식] FFT 절반(Nyquist) + 1 (DC 성분)
        inline constexpr uint16_t FFT_BINS         = (FFT_SIZE / 2U) + 1U;

        inline constexpr float    SAMPLE_RATE_HZ   = 1600.0f;
        inline constexpr uint16_t MEL_FILTERS      = 26U;

        inline constexpr uint16_t MFCC_COEFFS_DEF  = 13U;
        inline constexpr uint16_t MFCC_COEFFS_MAX  = 32U;

		// [계산식] 특징 벡터 파생 공식 (MFCC + Delta + Delta2 = x3)
        inline constexpr uint16_t FEATURE_MULTIPLIER = 3U;
        inline constexpr uint16_t FEATURE_DIM_MAX  = MFCC_COEFFS_MAX * FEATURE_MULTIPLIER; // 96
        inline constexpr uint16_t FEATURE_DIM_DEF  = MFCC_COEFFS_DEF * FEATURE_MULTIPLIER; // 39

        inline constexpr uint16_t MFCC_HISTORY     = 5U;
        inline constexpr uint8_t  PREPROCESS_STAGE_MAX = 8U;
        inline constexpr uint16_t NOISE_MIN_FRAMES = 8U;
    }

    // --- 5. BMI270 센서 드라이버 ---
    namespace C10_BMI {
        inline constexpr uint32_t SPI_FREQ_HZ      = 10000000UL;
        inline constexpr uint8_t  CHIP_ID_EXPECTED = 0x24U;
        inline constexpr uint8_t  BURST_AXIS_COUNT = 3U;
        inline constexpr uint8_t  STATUS_TEXT_MAX  = 48U;

        inline constexpr uint8_t AXIS_MODE_GYRO_Z    = 0U;
        inline constexpr uint8_t AXIS_MODE_ACC_Z     = 1U;
        inline constexpr uint8_t AXIS_MODE_GYRO_NORM = 2U;

        inline constexpr uint8_t REG_CHIP_ID      = 0x00U;
        inline constexpr uint8_t REG_STATUS       = 0x03U;
        inline constexpr uint8_t REG_GYR_X_LSB    = 0x0CU;
        inline constexpr uint8_t REG_ACC_X_LSB    = 0x12U;
        inline constexpr uint8_t REG_INT_STATUS_1 = 0x1DU;
        inline constexpr uint8_t REG_READ_FLAG    = 0x80U;
    }

    // --- 6. SDMMC 레코더 & 파일 시스템 상태 ---
    namespace C10_Rec {
        inline constexpr uint32_t BINARY_MAGIC       = 0x54323042UL;
        inline constexpr uint16_t BINARY_VERSION     = 1U;
        inline constexpr uint16_t BIN_RESERVED_BYTES = 8U;

        // [계산식] 바이너리 헤더 구조체의 record_count 위치를 바이트 크기 합산으로 명확히 지정 (20U)
        inline constexpr uint32_t HEADER_RECORD_OFFSET =
            		  sizeof(uint32_t)  			// magic
            		+ sizeof(uint16_t)  			// version
            		+ sizeof(uint16_t)  			// header_size
            		+ sizeof(uint32_t)  			// sample_rate_hz
            		+ sizeof(uint16_t)  			// fft_size
            		+ sizeof(uint16_t)  			// mfcc_dim
            		+ sizeof(uint16_t)  			// mel_filters
            		+ sizeof(uint16_t);  			// sequence_frames

        inline constexpr uint8_t  DMA_SLOT_COUNT     = 3U;
        inline constexpr uint32_t DMA_SLOT_BYTES     = 1024U;

        inline constexpr uint16_t BATCH_VECTOR_MAX   = 16U;
        inline constexpr uint16_t BATCH_WMARK_LOW    = 2U;
        inline constexpr uint16_t BATCH_WMARK_HIGH   = 8U;
        inline constexpr uint32_t BATCH_IDLE_FLUSH_MS= 250U;
        inline constexpr uint32_t FLUSH_MS           = 1000U;

        inline constexpr uint8_t  ROTATE_MAX         = 8U;
        inline constexpr uint8_t  ROTATE_KEEP_MAX    = 8U;
        inline constexpr uint16_t MAX_ROTATE_LIST    = 16U;

        inline constexpr uint16_t FILE_PATH_MAX      = 192U;
        inline constexpr uint16_t SESSION_NAME_MAX   = 48U;
        inline constexpr uint16_t LAST_ERROR_MAX     = 128U;

        inline constexpr uint8_t  SDMMC_PIN_UNASSIGNED   = 0xFFU;
        inline constexpr uint16_t SDMMC_PROFILE_COUNT    = 3U;
        inline constexpr uint16_t SDMMC_PROFILE_NAME_MAX = 32U;
    }

    // --- 7. 뷰어 및 CSV 관리 ---
    namespace C10_View {
        inline constexpr uint16_t EVENT_MAX            = 16U;
        inline constexpr uint16_t RECENT_WAVE_COUNT    = 4U;
        inline constexpr uint16_t SELECTION_POINTS_MAX = 128U;
        inline constexpr uint16_t SYNC_SERIES_MAX      = 4U;
        inline constexpr uint16_t SYNC_NAME_MAX        = 32U;

        inline constexpr uint16_t META_KIND_MAX        = 24U;
        inline constexpr uint16_t META_AUTO_TEXT_MAX   = 64U;
        inline constexpr uint16_t META_NAME_MAX        = 32U;
        inline constexpr uint16_t META_TEXT_MAX        = 96U;

        inline constexpr uint16_t CSV_MAX_ROWS         = 128U;
        inline constexpr uint16_t CSV_PAGE_SIZE_DEF    = 20U;
        inline constexpr uint16_t CSV_PAGE_SIZE_MAX    = 100U;
        inline constexpr uint8_t  CSV_SORT_ASC         = 0U;
        inline constexpr uint8_t  CSV_SORT_DESC        = 1U;
        inline constexpr uint16_t CSV_MAX_COL_FILTERS  = 8U;
    }

    // --- 8. Web UI & WebSocket ---
    namespace C10_Web {
        inline constexpr size_t   WAVEFORM_LEN         = C10_DSP::FFT_SIZE;
        inline constexpr size_t   SPECTRUM_LEN         = C10_DSP::FFT_BINS;
        inline constexpr size_t   MFCC_LEN             = C10_DSP::FEATURE_DIM_DEF;
        inline constexpr size_t   BINARY_BUF_LEN       = WAVEFORM_LEN + SPECTRUM_LEN + MFCC_LEN;

        inline constexpr uint32_t HASH_OFFSET_BASIS    = 2166136261UL;
        inline constexpr uint32_t HASH_PRIME           = 16777619UL;
        inline constexpr uint32_t MEASURE_FLAG_BIT     = 0x80000000UL;

        inline constexpr char const* MIME_JSON         = "application/json; charset=utf-8";
        inline constexpr char const* MIME_OCTET        = "application/octet-stream";
        inline constexpr char const* MIME_HTML         = "text/html";
        inline constexpr char const* MIME_TEXT         = "text/plain";

        inline constexpr char const* JSON_OK           = "{\"ok\":true}";
        inline constexpr char const* JSON_FAIL         = "{\"ok\":false}";

        inline constexpr char const* INDEX_FILE        = "index_214_003.html";
        inline constexpr char const* CACHE_CTRL        = "max-age=3600";
        inline constexpr char const* WS_URI            = "/api/t20/ws";

        inline constexpr uint16_t JSON_BUF_SIZE        = 2048U;
        inline constexpr uint16_t LARGE_JSON_BUF_SIZE  = 8192U;
        inline constexpr uint16_t PATH_BUF_SIZE        = 256U;
        inline constexpr uint32_t BTN_DEBOUNCE_MS      = 500U;

    }


    // --- 9. [NEW] 폴더 아키텍처 및 파일 경로 관리 ---
    namespace C10_Path {
        // [9.1] 마운트 포인트
        inline constexpr char const* MOUNT_SDMMC      = "/sdcard";

        // [9.2] LittleFS 경로 (시스템, 웹, 안전 백업)
        inline constexpr char const* LFS_DIR_SYS      = "/sys";
        inline constexpr char const* LFS_DIR_WEB      = "/www";
        inline constexpr char const* LFS_DIR_FALLBACK = "/fallback";

        inline constexpr char const* LFS_FILE_CFG     = "/sys/runtime_cfg.json";
        inline constexpr char const* LFS_FILE_IDX     = "/sys/recorder_index.json";
        inline constexpr char const* LFS_FILE_WEB_IDX = "index_214_003.html"; // ServeStatic용
        inline constexpr char const* LFS_FILE_FALLBACK= "/fallback/rec_fallback.bin";
        inline constexpr char const* LFS_FILE_DEFAULT = "/fallback/default_rec.bin";

        // [9.3] SD_MMC 경로 (대용량 로깅 및 내보내기)
        inline constexpr char const* SD_DIR_ROOT      = "/t20_data";
        inline constexpr char const* SD_DIR_BIN       = "/t20_data/bin";
        inline constexpr char const* SD_DIR_CSV       = "/t20_data/csv";
        inline constexpr char const* SD_DIR_LOG       = "/t20_data/log";

        inline constexpr char const* SD_PREFIX_BIN    = "/t20_data/bin/rec_";
        inline constexpr char const* SD_EXT_BIN       = ".bin";
        inline constexpr char const* SD_PREFIX_CSV    = "/t20_data/csv/exp_";
        inline constexpr char const* SD_EXT_CSV       = ".csv";
    }
}

/* ----------------------------------------------------------------------------
 * 공통 상태 및 결과 정의
 * ---------------------------------------------------------------------------- */
typedef enum {
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,
    EN_T20_STATE_RUNNING,
    EN_T20_STATE_DONE,
    EN_T20_STATE_ERROR,
    EN_T20_STATE_BUSY,
    EN_T20_STATE_TIMEOUT
} EM_T20_State_t;

typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

static inline const char* T20_StateToString(EM_T20_State_t s) {
    switch (s) {
        case EN_T20_STATE_IDLE:    return "IDLE";
        case EN_T20_STATE_READY:   return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_DONE:    return "DONE";
        case EN_T20_STATE_ERROR:   return "ERROR";
        case EN_T20_STATE_BUSY:    return "BUSY";
        case EN_T20_STATE_TIMEOUT: return "TIMEOUT";
        default:                   return "UNKNOWN";
    }
}

