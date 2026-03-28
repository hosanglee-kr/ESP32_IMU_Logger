#pragma once

#include <Arduino.h>

/* ============================================================================
 * File: T20_Mfcc_Def_062.h
 * Summary:
 *   T20 MFCC / Recorder / Viewer / Web 모듈의 공개 상수/열거형/구조체 정의
 *
 * [이번 버전 목적]
 * - 056 컴파일 안정 베이스를 유지
 * - 다음 단계로 DSP 경로를 올리되, 구조체/매크로/시그니처는 다시 흔들리지 않게 유지
 * ========================================================================== */

#define G_T20_VERSION_STR                         "T20_Mfcc_062"

#define G_T20_FFT_SIZE                            256U
#define G_T20_SAMPLE_RATE_HZ                      1600.0f
#define G_T20_MEL_FILTERS                         26U
#define G_T20_MFCC_COEFFS_MAX                     32U
#define G_T20_MFCC_COEFFS_DEFAULT                 13U
#define G_T20_FEATURE_DIM_MAX                     (G_T20_MFCC_COEFFS_MAX * 3U)
#define G_T20_FEATURE_DIM_DEFAULT                 (G_T20_MFCC_COEFFS_DEFAULT * 3U)
#define G_T20_SEQUENCE_FRAMES_MAX                 16U
#define G_T20_SEQUENCE_FRAMES_DEFAULT             8U
#define G_T20_PREPROCESS_STAGE_MAX                8U

#define G_T20_QUEUE_LEN                           4U
#define G_T20_SENSOR_TASK_STACK                   6144U
#define G_T20_PROCESS_TASK_STACK                  12288U
#define G_T20_RECORDER_TASK_STACK                 8192U
#define G_T20_SENSOR_TASK_PRIO                    4U
#define G_T20_PROCESS_TASK_PRIO                   3U
#define G_T20_RECORDER_TASK_PRIO                  2U

#define G_T20_RAW_FRAME_BUFFERS                   4U
#define G_T20_MFCC_HISTORY                        5U
#define G_T20_NOISE_MIN_FRAMES                    8U

#define G_T20_SPI_FREQ_HZ                         10000000UL
#define G_T20_PI                                  3.14159265358979323846f
#define G_T20_EPSILON                             1.0e-12f

#define G_T20_WEB_JSON_BUF_SIZE                   2048U
#define G_T20_WEB_LARGE_JSON_BUF_SIZE             8192U
#define G_T20_WEB_PATH_BUF_SIZE                   256U
#define G_T20_TEXT_PREVIEW_LINE_BUF_SIZE          256U
#define G_T20_PREVIEW_TEXT_BYTES_DEFAULT          4096U
#define G_T20_PREVIEW_TEXT_BYTES_MAX              16384U
#define G_T20_CSV_SERVER_MAX_ROWS                 128U
#define G_T20_CSV_SERVER_MAX_COL_FILTERS          8U
#define G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT         20U
#define G_T20_CSV_TABLE_PAGE_SIZE_MAX             100U
#define G_T20_CSV_SORT_ASC                        0U
#define G_T20_CSV_SORT_DESC                       1U
#define G_T20_RECORDER_RENDER_SYNC_SERIES_MAX     4U
#define G_T20_RENDER_SELECTION_SYNC_MAX           4U
#define G_T20_TYPE_META_PREVIEW_LINK_MAX          16U

#define G_T20_RECORDER_FILE_PATH_MAX              192U
#define G_T20_RECORDER_LAST_ERROR_MAX             128U
#define G_T20_RECORDER_MAX_ROTATE_LIST            16U
#define G_T20_RECORDER_EVENT_TEXT_MAX             80U
#define G_T20_RECORDER_BATCH_COUNT                16U
#define G_T20_RECORDER_BATCH_BUFFER_MAX           G_T20_RECORDER_BATCH_COUNT
#define G_T20_RECORDER_BATCH_ACCUM_WATERMARK      8U
#define G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS     300U
#define G_T20_RECORDER_FLUSH_INTERVAL_MS          1000U
#define G_T20_RECORDER_DMA_ALIGN_BYTES            32U
#define G_T20_RECORDER_ZERO_COPY_SLOT_MAX         2U
#define G_T20_ZERO_COPY_STAGE_BUFFER_BYTES        4096U

#define G_T20_BINARY_MAGIC                        0x54323042UL
#define G_T20_BINARY_VERSION                      1U
#define G_T20_BINARY_HEADER_MAGIC_LEN             4U
#define G_T20_BINARY_HEADER_RESERVED_BYTES        8U

#define G_T20_SDMMC_BOARD_HINT_MAX                32U
#define G_T20_SDMMC_MOUNT_PATH_DEFAULT            "/sdcard"
#define G_T20_SDMMC_PROFILE_NAME_MAX              32U

#define G_T20_VIEWER_EVENT_MAX                    16U
#define G_T20_VIEWER_RECENT_WAVE_COUNT            4U

#define G_T20_CFG_PROFILE_COUNT                   4U

#define G_T20_PIN_SPI_SCK                         12
#define G_T20_PIN_SPI_MISO                        13
#define G_T20_PIN_SPI_MOSI                        11
#define G_T20_PIN_BMI_CS                          10
#define G_T20_PIN_BMI_INT1                        14

#define G_T20_DSP_ENABLE_RUNTIME_FILTER           1U
#define G_T20_DSP_ENABLE_RUNTIME_NOISE_PROFILE    1U
#define G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS       160U
#define G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT       0.20f
#define G_T20_RECORDER_DEFAULT_FILE_PATH          "/t20_rec.bin"
#define G_T20_RECORDER_INDEX_FILE_PATH            "/t20_rec_index.json"
#define G_T20_RECORDER_RUNTIME_CFG_FILE_PATH      "/t20_runtime_cfg.json"
#define G_T20_SDMMC_PROFILE_PRESET_COUNT         3U
#define G_T20_RECORDER_BATCH_VECTOR_MAX          16U
#define G_T20_RECORDER_BATCH_FLUSH_RECORDS       4U
#define G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS    1000U
#define G_T20_RUNTIME_CFG_JSON_BUF_SIZE          1536U
#define G_T20_ZERO_COPY_DMA_SLOT_BYTES           1024U
#define G_T20_ZERO_COPY_DMA_SLOT_COUNT           2U
#define G_T20_RUNTIME_CFG_PROFILE_NAME_MAX       32U

enum EM_T20_FilterType_t
{
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
};

enum EM_T20_AxisType_t
{
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
};

enum EM_T20_OutputMode_t
{
    EN_T20_OUTPUT_VECTOR = 0,
    EN_T20_OUTPUT_SEQUENCE
};

enum EM_T20_StorageBackend_t
{
    EN_T20_STORAGE_LITTLEFS = 0,
    EN_T20_STORAGE_SDMMC
};

enum EM_T20_PreprocessStageType_t
{
    EN_T20_STAGE_NONE = 0,
    EN_T20_STAGE_DC_REMOVE,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_GATE,
    EN_T20_STAGE_FILTER
};

typedef struct
{
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

typedef struct
{
    bool                enable;
    EM_T20_FilterType_t type;
    float               cutoff_hz_1;
    float               cutoff_hz_2;
    float               q_factor;
} ST_T20_FilterConfig_t;

typedef struct
{
    bool     enable_gate;
    float    gate_threshold_abs;
    bool     enable_spectral_subtract;
    float    spectral_subtract_strength;
    uint16_t noise_learn_frames;
} ST_T20_NoiseConfig_t;

typedef struct
{
    bool                       enable;
    EM_T20_PreprocessStageType_t stage_type;
    float                      param_1;
    float                      param_2;
    float                      q_factor;
    float                      reserved_1;
    float                      reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;

typedef struct
{
    EM_T20_AxisType_t              axis;
    bool                           remove_dc;
    ST_T20_PreEmphasisConfig_t     preemphasis;
    ST_T20_FilterConfig_t          filter;
    ST_T20_NoiseConfig_t           noise;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct
{
    uint16_t fft_size;
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

typedef struct
{
    EM_T20_OutputMode_t output_mode;
    uint16_t            sequence_frames;
    bool                sequence_flatten;
} ST_T20_OutputConfig_t;

typedef struct
{
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
} ST_T20_Config_t;

typedef struct
{
    uint16_t log_mel_len;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len;

    float log_mel[G_T20_MEL_FILTERS];
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

typedef struct
{
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;

typedef struct
{
    char     path[G_T20_RECORDER_FILE_PATH_MAX];
    uint32_t size_bytes;
    uint32_t created_ms;
    uint32_t record_count;
} ST_T20_RecorderIndexItem_t;

typedef struct
{
    uint32_t frame_id;
    char     kind[16];
    char     text[G_T20_RECORDER_EVENT_TEXT_MAX];
} ST_T20_ViewerEvent_t;

typedef struct
{
    char     profile_name[G_T20_SDMMC_PROFILE_NAME_MAX];
    bool     use_1bit_mode;
    bool     enabled;
    uint8_t  clk_pin;
    uint8_t  cmd_pin;
    uint8_t  d0_pin;
    uint8_t  d1_pin;
    uint8_t  d2_pin;
    uint8_t  d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint16_t mel_filters;
    uint16_t sequence_frames;
    uint32_t record_count;
    char     reserved[G_T20_BINARY_HEADER_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;

typedef struct
{
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

typedef struct
{
    uint32_t frame_id;
    uint16_t vector_len;
    float    vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_RecorderVectorMessage_t;

typedef struct
{
    char     name[32];
    bool     used;
} ST_T20_ProfileInfo_t;

static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.preprocess.axis = EN_T20_AXIS_Z;
    cfg.preprocess.remove_dc = true;
    cfg.preprocess.preemphasis.enable = true;
    cfg.preprocess.preemphasis.alpha = 0.97f;
    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    cfg.preprocess.filter.cutoff_hz_2 = 250.0f;
    cfg.preprocess.filter.q_factor = 0.707f;
    cfg.preprocess.noise.enable_gate = true;
    cfg.preprocess.noise.gate_threshold_abs = 0.002f;
    cfg.preprocess.noise.enable_spectral_subtract = true;
    cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    cfg.preprocess.noise.noise_learn_frames = G_T20_NOISE_MIN_FRAMES;

    cfg.preprocess.pipeline.stage_count = 4;
    cfg.preprocess.pipeline.stages[0].enable = true;
    cfg.preprocess.pipeline.stages[0].stage_type = EN_T20_STAGE_DC_REMOVE;
    cfg.preprocess.pipeline.stages[1].enable = true;
    cfg.preprocess.pipeline.stages[1].stage_type = EN_T20_STAGE_PREEMPHASIS;
    cfg.preprocess.pipeline.stages[1].param_1 = 0.97f;
    cfg.preprocess.pipeline.stages[2].enable = true;
    cfg.preprocess.pipeline.stages[2].stage_type = EN_T20_STAGE_GATE;
    cfg.preprocess.pipeline.stages[2].param_1 = 0.002f;
    cfg.preprocess.pipeline.stages[3].enable = true;
    cfg.preprocess.pipeline.stages[3].stage_type = EN_T20_STAGE_FILTER;
    cfg.preprocess.pipeline.stages[3].param_1 = 15.0f;
    cfg.preprocess.pipeline.stages[3].param_2 = 250.0f;
    cfg.preprocess.pipeline.stages[3].q_factor = 0.707f;

    cfg.feature.fft_size = G_T20_FFT_SIZE;
    cfg.feature.frame_size = G_T20_FFT_SIZE;
    cfg.feature.hop_size = G_T20_FFT_SIZE;
    cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window = 2;

    cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;
    cfg.output.sequence_frames = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten = true;

    return cfg;
}
