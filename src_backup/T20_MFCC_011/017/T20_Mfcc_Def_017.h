#pragma once

#include <Arduino.h>

/* ============================================================================
 * File: T20_Mfcc_Def_017.h
 * Summary:
 *   T20 MFCC / vibration feature module 공개 정의 헤더
 *
 * [이번 단계 반영]
 * - 실제 컴파일 오류 로그 대응 라운드 3 성격의 정의 정리
 * - AsyncWeb 제어/다운로드 대묶음용 공개 상수 추가
 * - 013 기준 실제 컴파일 오류 대응용 정의 정리
 * - recorder 전용 queue/task 지원 상수 추가
 * - binary raw header / record type 정의 추가
 *
 * [향후 단계 TODO]
 * - AsyncWeb 설정/API 연동
 * - live waveform / live feature stream
 * - snapshot별 FFT / mel_bank cache 분리
 * - zero-copy / DMA / cache aligned 고도화
 * ========================================================================== */

#define G_T20_VERSION_STR                   "T20_Mfcc_017"

#define G_T20_RAW_FRAME_BUFFERS             4
#define G_T20_SAMPLE_RING_SIZE              2048
#define G_T20_FFT_SIZE                      256
#define G_T20_SAMPLE_RATE_HZ                1600.0f
#define G_T20_MEL_FILTERS                   26

#define G_T20_MFCC_COEFFS_DEFAULT           13
#define G_T20_MFCC_COEFFS_MAX               32
#define G_T20_DELTA_WINDOW_DEFAULT          2

#define G_T20_PREPROCESS_STAGE_MAX          8

#define G_T20_QUEUE_LEN                     8
#define G_T20_SENSOR_TASK_STACK             6144
#define G_T20_PROCESS_TASK_STACK            12288
#define G_T20_SENSOR_TASK_PRIO              4
#define G_T20_PROCESS_TASK_PRIO             3

#define G_T20_RECORDER_QUEUE_LEN            16
#define G_T20_RECORDER_TASK_STACK           8192
#define G_T20_RECORDER_TASK_PRIO            2
#define G_T20_RECORDER_RAW_MAX_SAMPLES      G_T20_FFT_SIZE
#define G_T20_RECORDER_EVENT_TEXT_MAX       80
#define G_T20_RECORDER_BATCH_COUNT            4
#define G_T20_RECORDER_FLUSH_INTERVAL_MS      1000U
#define G_T20_RECORDER_METADATA_INTERVAL_MS   3000U

#define G_T20_WEB_BASE_PATH_MAX              32
#define G_T20_WEB_DEFAULT_BASE_PATH          "/api/t20"
#define G_T20_WEB_FS_MOUNT_PATH              "/t20"
#define G_T20_WEB_STATIC_INDEX_PATH          "/t20/index.html"
#define G_T20_WEB_STATIC_STYLE_PATH          "/t20/t20.css"
#define G_T20_WEB_STATIC_APP_PATH            "/t20/t20.js"
#define G_T20_WEB_STATIC_CACHE_CONTROL       "no-cache"
#define G_T20_WEB_JSON_DOC_HINT              1024
#define G_T20_WEB_ENABLE_DEFAULT             1

#define G_T20_MFCC_HISTORY                  5
#define G_T20_NOISE_MIN_FRAMES              8

#define G_T20_SEQUENCE_FRAMES_MAX           16
#define G_T20_SEQUENCE_FRAMES_DEFAULT       8

#define G_T20_FEATURE_DIM_DEFAULT           (G_T20_MFCC_COEFFS_DEFAULT * 3)
#define G_T20_FEATURE_DIM_MAX               (G_T20_MFCC_COEFFS_MAX * 3)

#define G_T20_BUTTON_DEBOUNCE_MS            30U
#define G_T20_BUTTON_LONG_PRESS_MS          1000U

#define G_T20_RECORDER_MAX_SESSION_NAME     48
#define G_T20_RECORDER_MAX_PATH_LEN         128

#define G_T20_RAW_BINARY_MAGIC              0x54323052UL
#define G_T20_RAW_BINARY_HEADER_VERSION     1U
#define G_T20_RECORDER_RECORD_MAGIC         0x54323044UL

#define G_T20_SPI_FREQ_HZ                   10000000UL

#define G_T20_PI                            3.14159265358979323846f
#define G_T20_EPSILON                       1.0e-12f

#define G_T20_PIN_SPI_SCK                   12
#define G_T20_PIN_SPI_MISO                  13
#define G_T20_PIN_SPI_MOSI                  11
#define G_T20_PIN_BMI_CS                    10
#define G_T20_PIN_BMI_INT1                  14
#define G_T20_PIN_BUTTON                    0

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

enum EM_T20_PreprocessStageType_t
{
    EN_T20_STAGE_NONE = 0,
    EN_T20_STAGE_DC_REMOVE,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_NOISE_GATE,
    EN_T20_STAGE_BIQUAD_LPF,
    EN_T20_STAGE_BIQUAD_HPF,
    EN_T20_STAGE_BIQUAD_BPF
};

enum EM_T20_SessionState_t
{
    EN_T20_SESSION_IDLE = 0,
    EN_T20_SESSION_READY,
    EN_T20_SESSION_RECORDING,
    EN_T20_SESSION_STOPPING,
    EN_T20_SESSION_ERROR
};

enum EM_T20_ButtonEvent_t
{
    EN_T20_BUTTON_EVENT_NONE = 0,
    EN_T20_BUTTON_EVENT_SHORT_PRESS,
    EN_T20_BUTTON_EVENT_LONG_PRESS
};

enum EM_T20_RecorderRawFormat_t
{
    EN_T20_REC_RAW_NONE = 0,
    EN_T20_REC_RAW_CSV,
    EN_T20_REC_RAW_BINARY
};

enum EM_T20_RecorderMetaFormat_t
{
    EN_T20_REC_META_NONE = 0,
    EN_T20_REC_META_JSONL,
    EN_T20_REC_META_CSV
};

enum EM_T20_RecorderEventFormat_t
{
    EN_T20_REC_EVENT_NONE = 0,
    EN_T20_REC_EVENT_JSONL,
    EN_T20_REC_EVENT_CSV
};

enum EM_T20_RecorderRecordType_t
{
    EN_T20_REC_RECORD_NONE = 0,
    EN_T20_REC_RECORD_EVENT,
    EN_T20_REC_RECORD_RAW_FRAME,
    EN_T20_REC_RECORD_FEATURE
};

typedef struct
{
    bool  enable;
    EM_T20_PreprocessStageType_t stage_type;
    float param_1;
    float param_2;
    float q_factor;
    float reserved_1;
    float reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;

typedef struct
{
    EM_T20_AxisType_t axis;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct
{
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
    uint8_t  button_pin;
    bool     active_low;
    uint16_t debounce_ms;
    uint16_t long_press_ms;
    bool     short_press_toggle_measurement;
    bool     long_press_reserved;
} ST_T20_ButtonConfig_t;

typedef struct
{
    bool                         enable;
    bool                         save_config_json;
    EM_T20_RecorderRawFormat_t   raw_format;
    EM_T20_RecorderMetaFormat_t  metadata_format;
    EM_T20_RecorderEventFormat_t event_format;
    bool                         write_feature_vector_csv;
    bool                         auto_session_folder;
    uint16_t                     batch_count;
    uint32_t                     flush_interval_ms;
    uint32_t                     metadata_interval_ms;
    char                         root_dir[G_T20_RECORDER_MAX_PATH_LEN];
    char                         session_prefix[G_T20_RECORDER_MAX_SESSION_NAME];
} ST_T20_RecorderConfig_t;

typedef struct
{
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
    ST_T20_ButtonConfig_t     button;
    ST_T20_RecorderConfig_t   recorder;
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
    uint32_t magic;
    uint16_t header_version;
    uint16_t reserved_0;
    char     module_version[16];
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;
    uint16_t axis;
    uint16_t mfcc_coeffs;
    uint16_t mel_filters;
    uint16_t delta_window;
    uint16_t reserved_1;
    uint32_t session_start_ms;
} ST_T20_RawBinaryHeader_t;

typedef struct
{
    uint32_t magic;
    uint16_t record_type;
    uint16_t payload_bytes;
    uint32_t timestamp_ms;
} ST_T20_RecorderRecordHeader_t;

static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.preprocess.axis = EN_T20_AXIS_Z;
    cfg.preprocess.pipeline.stage_count = 4;

    cfg.preprocess.pipeline.stages[0].enable = true;
    cfg.preprocess.pipeline.stages[0].stage_type = EN_T20_STAGE_DC_REMOVE;

    cfg.preprocess.pipeline.stages[1].enable = true;
    cfg.preprocess.pipeline.stages[1].stage_type = EN_T20_STAGE_BIQUAD_HPF;
    cfg.preprocess.pipeline.stages[1].param_1 = 15.0f;
    cfg.preprocess.pipeline.stages[1].q_factor = 0.707f;

    cfg.preprocess.pipeline.stages[2].enable = true;
    cfg.preprocess.pipeline.stages[2].stage_type = EN_T20_STAGE_NOISE_GATE;
    cfg.preprocess.pipeline.stages[2].param_1 = 0.002f;

    cfg.preprocess.pipeline.stages[3].enable = true;
    cfg.preprocess.pipeline.stages[3].stage_type = EN_T20_STAGE_PREEMPHASIS;
    cfg.preprocess.pipeline.stages[3].param_1 = 0.97f;

    cfg.feature.frame_size       = G_T20_FFT_SIZE;
    cfg.feature.hop_size         = G_T20_FFT_SIZE;
    cfg.feature.sample_rate_hz   = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters      = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs      = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window     = G_T20_DELTA_WINDOW_DEFAULT;

    cfg.output.output_mode       = EN_T20_OUTPUT_VECTOR;
    cfg.output.sequence_frames   = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten  = true;

    cfg.button.button_pin                     = G_T20_PIN_BUTTON;
    cfg.button.active_low                     = true;
    cfg.button.debounce_ms                    = G_T20_BUTTON_DEBOUNCE_MS;
    cfg.button.long_press_ms                  = G_T20_BUTTON_LONG_PRESS_MS;
    cfg.button.short_press_toggle_measurement = true;
    cfg.button.long_press_reserved            = true;

    cfg.recorder.enable                   = true;
    cfg.recorder.save_config_json         = true;
    cfg.recorder.raw_format               = EN_T20_REC_RAW_BINARY;
    cfg.recorder.metadata_format          = EN_T20_REC_META_JSONL;
    cfg.recorder.event_format             = EN_T20_REC_EVENT_JSONL;
    cfg.recorder.write_feature_vector_csv = false;
    cfg.recorder.auto_session_folder      = true;
    cfg.recorder.batch_count              = G_T20_RECORDER_BATCH_COUNT;
    cfg.recorder.flush_interval_ms        = G_T20_RECORDER_FLUSH_INTERVAL_MS;
    cfg.recorder.metadata_interval_ms     = G_T20_RECORDER_METADATA_INTERVAL_MS;
    strncpy(cfg.recorder.root_dir, "/t20", sizeof(cfg.recorder.root_dir) - 1);
    strncpy(cfg.recorder.session_prefix, "session", sizeof(cfg.recorder.session_prefix) - 1);

    return cfg;
}
