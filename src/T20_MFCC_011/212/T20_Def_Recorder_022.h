

/* ============================================================================
 * File: T20_Def_Recorder_022.h
 * Summary: SD_MMC Data Logging & Storage Backend Definitions (v210)
 * ========================================================================== */

#pragma once


#include <stdint.h>
#include <stdbool.h>

#include "T20_Def_Comm_021.h"
#include "T20_Def_SensDsp_021.h"

/* ============================================================================
 * Global Constants (G_T20_)
 * ========================================================================== */

/* --- SDMMC --- */
#define G_T20_SDMMC_PIN_UNASSIGNED 0xFFU

static const uint16_t G_T20_SDMMC_BOARD_HINT_MAX      = 32U;
static const uint16_t G_T20_SDMMC_PROFILE_PRESET_COUNT= 3U;
static const uint16_t G_T20_SDMMC_PROFILE_NAME_MAX    = 32U;

static const char* G_T20_SDMMC_MOUNT_PATH_DEFAULT = "/sdcard";

/* --- Binary --- */
static const uint32_t G_T20_BINARY_MAGIC   = 0x54323042UL;
static const uint16_t G_T20_BINARY_VERSION = 1U;

static const uint16_t G_T20_BINARY_HEADER_RESERVED_BYTES = 8U;

/* --- Recorder Limits (Magic Number 제거) --- */
typedef struct {
    uint16_t batch_count;
    uint16_t batch_watermark_low;
    uint16_t batch_watermark_high;
    uint16_t flush_interval_ms;
    uint16_t idle_flush_ms;
} ST_T20_RecorderLimits_t;

static const ST_T20_RecorderLimits_t G_T20_RECORDER_LIMITS = {
    .batch_count = 16U,
    .batch_watermark_low = 2U,
    .batch_watermark_high = 8U,
    .flush_interval_ms = 1000U,
    .idle_flush_ms = 250U
};

/* --- File System --- */
static const uint16_t G_T20_RECORDER_FILE_PATH_MAX = 192U;
static const uint16_t G_T20_RECORDER_SESSION_NAME_MAX = 48U;

/* ============================================================================
 * Common State / Result (공통화)
 * ========================================================================== */

typedef enum {
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,
    EN_T20_STATE_RUNNING,
    EN_T20_STATE_DONE,
    EN_T20_STATE_ERROR
} EM_T20_State_t;

typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

/* ============================================================================
 * Debug String
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
 * Recorder State (매크로 → 구조체)
 * ========================================================================== */

typedef struct {
    EM_T20_State_t file_write;
    EM_T20_State_t store;
    EM_T20_State_t finalize;
    EM_T20_State_t pipeline;
    EM_T20_State_t sync;
    EM_T20_State_t runtime;
} ST_T20_RecorderState_t;

/* ============================================================================
 * State Machine
 * ========================================================================== */

typedef struct {
    ST_T20_RecorderState_t state;
} ST_T20_RecorderHandle_t;

static inline void T20_Recorder_StateMachine(ST_T20_RecorderHandle_t* h)
{
    switch (h->state.pipeline)
    {
        case EN_T20_STATE_IDLE:
            h->state.pipeline = EN_T20_STATE_READY;
            break;

        case EN_T20_STATE_READY:
            h->state.pipeline = EN_T20_STATE_RUNNING;
            break;

        case EN_T20_STATE_RUNNING:
            h->state.pipeline = EN_T20_STATE_DONE;
            break;

        default:
            break;
    }
}

/* ============================================================================
 * Output / Storage Config
 * ========================================================================== */

typedef enum {
    EN_T20_OUTPUT_VECTOR = 0,
    EN_T20_OUTPUT_SEQUENCE
} EM_T20_OutputMode_t;

typedef enum {
    EN_T20_STORAGE_LITTLEFS = 0,
    EN_T20_STORAGE_SDMMC
} EM_T20_StorageBackend_t;

typedef struct {
    EM_T20_OutputMode_t output_mode;
    uint16_t sequence_frames;
    bool sequence_flatten;
} ST_T20_OutputConfig_t;

/* ============================================================================
 * Unified Config (핵심 유지)
 * ========================================================================== */

typedef struct {
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t feature;
    ST_T20_OutputConfig_t output;
} ST_T20_Config_t;

/* ============================================================================
 * SDMMC Profile
 * ========================================================================== */

typedef struct {
    char profile_name[G_T20_SDMMC_PROFILE_NAME_MAX];
    bool use_1bit_mode;
    bool enabled;

    uint8_t clk_pin;
    uint8_t cmd_pin;
    uint8_t d0_pin;
    uint8_t d1_pin;
    uint8_t d2_pin;
    uint8_t d3_pin;
} ST_T20_SdmmcProfile_t;

/* ============================================================================
 * Recorder Index
 * ========================================================================== */

typedef struct {
    char path[G_T20_RECORDER_FILE_PATH_MAX];
    uint32_t size_bytes;
    uint32_t created_ms;
    uint32_t record_count;
} ST_T20_RecorderIndexItem_t;

/* ============================================================================
 * Binary Header
 * ========================================================================== */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;

    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint16_t mel_filters;
    uint16_t sequence_frames;

    uint32_t record_count;
    char reserved[G_T20_BINARY_HEADER_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;

/* ============================================================================
 * Data Message
 * ========================================================================== */

typedef struct {
    uint32_t frame_id;
    uint16_t vector_len;
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_RecorderVectorMessage_t;

/* ============================================================================
 * Debug Helper
 * ========================================================================== */

static inline void T20_Recorder_DebugState(ST_T20_RecorderState_t* s)
{
    printf("[WRITE:%s STORE:%s FINAL:%s PIPE:%s SYNC:%s RUN:%s]\n",
        T20_StateToString(s->file_write),
        T20_StateToString(s->store),
        T20_StateToString(s->finalize),
        T20_StateToString(s->pipeline),
        T20_StateToString(s->sync),
        T20_StateToString(s->runtime));
}
