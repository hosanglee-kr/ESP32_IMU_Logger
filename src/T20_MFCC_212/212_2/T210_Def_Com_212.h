
/* ============================================================================
 * File: T210_Def_Com_212.h

 * ========================================================================== */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>



// --- v210 대비 누락된 시스템 경로 보강 --- 
static const char* G_T20_RECORDER_DEFAULT_FILE_PATH  = "/t20_rec.bin";
static const char* G_T20_RECORDER_INDEX_FILE_PATH    = "/t20_rec_index.json";
static const char* G_T20_RECORDER_RUNTIME_CFG_PATH   = "/t20_runtime_cfg.json";

// 시뮬레이션 및 실제 소스 모드 정의 
typedef enum {
    EN_T20_SOURCE_OFF       = 255,
    EN_T20_SOURCE_SYNTHETIC = 0,
    EN_T20_SOURCE_BMI270    = 1
} EM_T20_SourceMode_t;



/* ============================================================================
 * Global Constants (G_T20_)
 * ========================================================================== */

/* --- Version --- */
static const char* G_T20_VERSION_STR = "T20_Mfcc_212";

/* --- Hardware Pin Map (ESP32-S3) --- */
typedef struct {
    uint8_t spi_sck;
    uint8_t spi_miso;
    uint8_t spi_mosi;
    uint8_t bmi_cs;
    uint8_t bmi_int1;
} ST_T20_PinMap_t;

static const ST_T20_PinMap_t G_T20_PINMAP = {
    .spi_sck  = 12,
    .spi_miso = 13,
    .spi_mosi = 11,
    .bmi_cs   = 10,
    .bmi_int1 = 14
};

/* ============================================================================
 * RTOS Configuration (Magic Number 그룹화)
 * ========================================================================== */

typedef struct {
    uint16_t queue_len;

    uint16_t sensor_stack;
    uint16_t process_stack;
    uint16_t recorder_stack;

    uint8_t sensor_prio;
    uint8_t process_prio;
    uint8_t recorder_prio;
} ST_T20_RTOSConfig_t;

static const ST_T20_RTOSConfig_t G_T20_RTOS_CONFIG = {
    .queue_len      = 4U,
    .sensor_stack   = 6144U,
    .process_stack  = 12288U,
    .recorder_stack = 8192U,
    .sensor_prio    = 4U,
    .process_prio   = 3U,
    .recorder_prio  = 2U
};

/* ============================================================================
 * Math Constants
 * ========================================================================== */

static const float G_T20_PI      = 3.14159265358979323846f;
static const float G_T20_EPSILON = 1.0e-12f;

/* ============================================================================
 * System Limits / Buffers
 * ========================================================================== */

typedef struct {
    uint16_t runtime_json_buf;
    uint16_t profile_count;
    uint16_t raw_frame_buffers;

    uint16_t selection_sync_name_max;
    uint16_t runtime_profile_name_max;
    uint16_t render_selection_sync_max;

    uint16_t preview_text_default;
    uint16_t preview_text_max;

    uint16_t csv_page_size_max;
    
    
    uint16_t noise_min_frames;
    uint8_t  csv_sort_asc;
    uint8_t  csv_sort_desc;
    
    
} ST_T20_SystemLimits_t;

static const ST_T20_SystemLimits_t G_T20_SYSTEM_LIMITS = {
    .runtime_json_buf          = 1536U,
    .profile_count             = 4U,
    .raw_frame_buffers         = 4U,
    .selection_sync_name_max   = 32U,
    .runtime_profile_name_max  = 32U,
    .render_selection_sync_max = 4U,
    .preview_text_default      = 4096U,
    .preview_text_max          = 16384U,
    .csv_page_size_max         = 100U,
    
    .noise_min_frames = 8U,
    .csv_sort_asc = 0, 
    .csv_sort_desc = 1
};

/* ============================================================================
 * Common State / Result (전 시스템 공통)
 * ========================================================================== */

typedef enum {
    
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,     // 준비 완료 (v210의 READY)
    EN_T20_STATE_RUNNING,   // 실행 중 (v210의 EXEC/PENDING)
    EN_T20_STATE_DONE,      // 정상 종료 (v210의 DONE/SUCCESS)
    EN_T20_STATE_ERROR,     // 오류 발생 (v210의 FAIL)
    EN_T20_STATE_BUSY,      // 작업 중 대기
    EN_T20_STATE_TIMEOUT    // 시간 초과
    
} EM_T20_State_t;

typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

/* ============================================================================
 * Debug Helpers
 * ========================================================================== */

static inline const char* T20_StateToString(EM_T20_State_t s)
{
    switch (s) {
        case EN_T20_STATE_IDLE: return "IDLE";
        case EN_T20_STATE_READY: return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_DONE: return "DONE";
        case EN_T20_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/* ============================================================================
 * Utility Helpers
 * ========================================================================== */

static inline float T20_ClampFloat(float v, float min, float max)
{
    return (v < min) ? min : (v > max) ? max : v;
}

static inline float T20_AbsFloat(float v)
{
    return (v < 0.0f) ? -v : v;
}
