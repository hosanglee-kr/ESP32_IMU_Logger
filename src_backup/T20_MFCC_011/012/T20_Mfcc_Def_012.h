#pragma once

#include <Arduino.h>

/* ============================================================================
 * File: T20_Mfcc_Def_012.h
 * Summary:
 *   T20 MFCC / vibration feature module의 공개 정의 헤더
 *
 * [이번 단계 반영]
 * 1) 전처리 stage 배열화 구조 반영
 * 2) frame_size / hop_size 기반 sliding window 구조 반영
 * 3) 세션 상태기계(session state) 반영
 * 4) 버튼 기반 측정 시작/종료 확장 포인트 반영
 *
 * [향후 단계 TODO]
 * - SD_MMC 기반 raw/config/meta/event 저장 계층 추가
 * - raw 저장 포맷: CSV / Binary 선택
 * - metadata / event 포맷: JSONL / CSV 선택
 * - AsyncWeb 기반 설정 변경 / 측정 시작·종료 / 상태 조회 / 시각화
 * - live waveform / live log-mel / live MFCC endpoint
 * - multi-config FFT / mel bank cache 최적화
 * - DMA / zero-copy / cache aligned 고도화
 * ========================================================================== */

// ============================================================================
// [기본 상수]
// ============================================================================

#define G_T20_VERSION_STR                   "T20_Mfcc_012"

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

#define G_T20_MFCC_HISTORY                  5
#define G_T20_NOISE_MIN_FRAMES              8

#define G_T20_SEQUENCE_FRAMES_MAX           16
#define G_T20_SEQUENCE_FRAMES_DEFAULT       8

#define G_T20_FEATURE_DIM_DEFAULT           (G_T20_MFCC_COEFFS_DEFAULT * 3)
#define G_T20_FEATURE_DIM_MAX               (G_T20_MFCC_COEFFS_MAX * 3)

#define G_T20_BUTTON_DEBOUNCE_MS            30U
#define G_T20_BUTTON_LONG_PRESS_MS          1000U

#define G_T20_SPI_FREQ_HZ                   10000000UL

#define G_T20_PI                            3.14159265358979323846f
#define G_T20_EPSILON                       1.0e-12f

// ============================================================================
// [핀 설정]
// ============================================================================

#define G_T20_PIN_SPI_SCK                   12
#define G_T20_PIN_SPI_MISO                  13
#define G_T20_PIN_SPI_MOSI                  11
#define G_T20_PIN_BMI_CS                    10
#define G_T20_PIN_BMI_INT1                  14

#define G_T20_PIN_BUTTON                    0

// ============================================================================
// [열거형]
// ============================================================================

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

// ============================================================================
// [설정 구조체]
// ============================================================================

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
    uint16_t hop_size;        // frame_size와 같으면 overlap 없음
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
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
    ST_T20_ButtonConfig_t     button;
} ST_T20_Config_t;

// ============================================================================
// [출력 구조체]
// ============================================================================

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

// ============================================================================
// [시퀀스 ring buffer]
// ============================================================================

typedef struct
{
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;

// ============================================================================
// [기본 설정 생성 함수]
// ============================================================================

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
    cfg.feature.hop_size         = G_T20_FFT_SIZE;   // overlap 없음
    cfg.feature.sample_rate_hz   = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters      = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs      = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window     = G_T20_DELTA_WINDOW_DEFAULT;

    cfg.output.output_mode       = EN_T20_OUTPUT_VECTOR;
    cfg.output.sequence_frames   = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten  = true;

    cfg.button.button_pin                    = G_T20_PIN_BUTTON;
    cfg.button.active_low                    = true;
    cfg.button.debounce_ms                   = G_T20_BUTTON_DEBOUNCE_MS;
    cfg.button.long_press_ms                 = G_T20_BUTTON_LONG_PRESS_MS;
    cfg.button.short_press_toggle_measurement = true;
    cfg.button.long_press_reserved           = true;

    return cfg;
}
