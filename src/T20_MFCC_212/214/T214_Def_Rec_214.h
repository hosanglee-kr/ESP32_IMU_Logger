

/* ============================================================================
 * File: T214_Def_Rec_214.h
 * Summary: SD_MMC Data Logging & Storage Backend Definitions
 * ========================================================================== */

#pragma once


#include <stdint.h>
#include <stdbool.h>

#include "T210_Def_Com_214.h"
#include "T212_Def_Sens_214.h"

#define G_T20_RECORDER_BATCH_WATERMARK_LOW    2U
#define G_T20_RECORDER_BATCH_WATERMARK_HIGH   8U
#define G_T20_RECORDER_BATCH_IDLE_FLUSH_MS    250U

#define G_T20_RECORDER_LAST_ERROR_MAX         128U
#define G_T20_ZERO_COPY_DMA_SLOT_COUNT        2U
#define G_T20_ZERO_COPY_DMA_SLOT_BYTES        1024U
#define G_T20_RECORDER_BATCH_VECTOR_MAX       16U



// --- Recorder 전용 상수 ---
inline constexpr uint8_t  G_T20_RECORDER_ROTATE_MAX = 8U;
inline constexpr uint32_t G_T20_RECORDER_FLUSH_MS   = 1000U;
// 한 파일에 기록할 배치 레코드 수 (기본값: 32개)
inline constexpr uint32_t G_T20_RECORDER_BATCH_FLUSH_RECORDS = 32U;
// 배치 플러시 타임아웃 (기본값: 2000ms = 2초)
inline constexpr uint32_t G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS = 2000U;



// --- File Rotation Settings ---
inline constexpr uint8_t G_T20_RECORDER_ROTATE_KEEP_MAX = 8U;
inline constexpr char const* G_T20_RECORDER_ROTATE_PREFIX     = "/rec_";
inline constexpr char const* G_T20_RECORDER_ROTATE_EXT        = ".bin";
// static const char* G_T20_RECORDER_FALLBACK_PATH     = "/rec_fallback.bin";


// --- Recorder 상세 상태 관리 (v210 상태 매핑) ---
typedef struct {
    EM_T20_State_t file_io;      // v210: FILE_WRITE, WRITE_COMMIT 등
    EM_T20_State_t bundle_map;   // v210: BUNDLE_MAP, STORE_BUNDLE 등
    EM_T20_State_t finalize;     // v210: WRITE_FINALIZE, COMMIT_ROUTE 등
    EM_T20_State_t sync_report;  // v210: REPORT_SYNC, MANIFEST_SYNC 등
    EM_T20_State_t audit_trail;  // v210: AUDIT_STATE, ARTIFACT_STATE 등
} ST_T20_RecorderRuntimeState_t;




// --- DMA / Zero-Copy (성능 최적화용) ---
typedef struct {
    uint16_t dma_align_bytes;
    uint16_t zero_copy_slot_max;
    uint32_t slot_bytes;
} ST_T20_DmaConfig_t;

static const ST_T20_DmaConfig_t G_T20_DMA_CONFIG = {
    .dma_align_bytes = 32U,
    .zero_copy_slot_max = 2U,
    .slot_bytes = 1024U
};





/* ============================================================================
 * Global Constants (G_T20_)
 * ========================================================================== */

// --- SDMMC ---
#define G_T20_SDMMC_PIN_UNASSIGNED 0xFFU

inline constexpr uint16_t G_T20_SDMMC_BOARD_HINT_MAX      = 32U;
inline constexpr uint16_t G_T20_SDMMC_PROFILE_PRESET_COUNT= 3U;
inline constexpr uint16_t G_T20_SDMMC_PROFILE_NAME_MAX    = 32U;

inline constexpr char const* G_T20_SDMMC_MOUNT_PATH_DEFAULT = "/sdcard";

// --- Binary ---
inline constexpr uint32_t G_T20_BINARY_MAGIC   = 0x54323042UL;
inline constexpr uint16_t G_T20_BINARY_VERSION = 1U;

inline constexpr uint16_t G_T20_BINARY_HEADER_RESERVED_BYTES = 8U;

// --- Recorder Limits (Magic Number 제거) ---
typedef struct {
    uint16_t batch_count;
    uint16_t watermark_low;
    uint16_t watermark_high;
    uint16_t accum_watermark;      // v210 복구 (8U)
    uint16_t accum_timeout_ms;    // v210 복구 (300ms)
    uint16_t flush_interval_ms;
} ST_T20_RecorderLimits_t;

static const ST_T20_RecorderLimits_t G_T20_RECORDER_LIMITS = {
    .batch_count      = 16U,
    .watermark_low    = 2U,
    .watermark_high   = 8U,
    .accum_watermark  = 8U,
    .accum_timeout_ms = 300U,
    .flush_interval_ms = 1000U
};


// --- File System ---
inline constexpr uint16_t G_T20_RECORDER_FILE_PATH_MAX = 192U;
inline constexpr uint16_t G_T20_RECORDER_SESSION_NAME_MAX = 48U;





/* ============================================================================
 * Recorder State (매크로 → 구조체)
 * ========================================================================== */

// 통합 제안: ST_T20_RecorderState_t
typedef struct {
    EM_T20_State_t storage;    // SD 카드 마운트/준비
    EM_T20_State_t file_io;    // Open/Close 세션
    EM_T20_State_t write;      // 실시간 데이터 Write
    EM_T20_State_t sync;       // v210: COMMIT/MANIFEST 동기화
    EM_T20_State_t audit;      // v210: 파일 무결성 검사
    EM_T20_State_t pipeline;   // 전체 프로세스 상태
} ST_T20_RecorderState_t;


typedef struct {
    ST_T20_RecorderState_t state;
    char last_error[128];
} ST_T20_Recorder_Handle_t;


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
    if (s == nullptr) return;

    printf("[STORAGE:%s IO:%s WRITE:%s SYNC:%s AUDIT:%s PIPE:%s]\n",
        T20_StateToString(s->storage),   // store -> storage
        T20_StateToString(s->file_io),   // file_write -> file_io
        T20_StateToString(s->write),     // write 유지
        T20_StateToString(s->sync),      // sync 유지
        T20_StateToString(s->audit),     // audit 유지
        T20_StateToString(s->pipeline)   // finalize -> pipeline
    );
}
