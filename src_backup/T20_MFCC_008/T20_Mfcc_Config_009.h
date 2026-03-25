#pragma once


/* ============================================================================
 * File: T20_Mfcc_Config_009.h
 * Summary:
 *   Public type definitions for the T20 MFCC / vibration feature module.
 *   Default constants and default config factory for the T20 MFCC module.
 * ========================================================================== */

// ============================================================================
// [기본 상수]
// ============================================================================

#define G_T20_VERSION_STR                "T20_Mfcc_009"

#define G_T20_RAW_FRAME_BUFFERS          4
#define G_T20_FFT_SIZE                   256
#define G_T20_SAMPLE_RATE_HZ             1600.0f
#define G_T20_MEL_FILTERS                26

//#define G_T20_MFCC_COEFFS                13   // 삭제예정
#define G_T20_MFCC_COEFFS_DEFAULT        13   // 기본값
#define G_T20_MFCC_COEFFS_MAX            32   // 메모리 최대값

#define G_T20_DELTA_WINDOW               2

#define G_T20_QUEUE_LEN                  4
#define G_T20_SENSOR_TASK_STACK          6144
#define G_T20_PROCESS_TASK_STACK         12288
#define G_T20_SENSOR_TASK_PRIO           4
#define G_T20_PROCESS_TASK_PRIO          3

#define G_T20_MFCC_HISTORY               5
#define G_T20_NOISE_MIN_FRAMES           8

// #define G_T20_MAX_SEQUENCE_FRAMES        16  // 삭제 예정
#define G_T20_SEQUENCE_FRAMES_MAX        16  // 메모리 최대깂
#define G_T20_SEQUENCE_FRAMES_DEFAULT    8  // 기본값


// #define G_T20_FEATURE_DIM                39  // 삭제예정
#define G_T20_FEATURE_DIM_DEFAULT        (G_T20_MFCC_COEFFS_DEFAULT * 3) // 39  // 기본값
#define G_T20_FEATURE_DIM_MAX            (G_T20_MFCC_COEFFS_MAX * 3)     // 96 // 최대 feature dim buffer


#define G_T20_SPI_FREQ_HZ                10000000UL

#define G_T20_PI                         3.14159265358979323846f
#define G_T20_EPSILON                    1.0e-12f

// ============================================================================
// [핀 설정]
// ============================================================================

#define G_T20_PIN_SPI_SCK                12
#define G_T20_PIN_SPI_MISO               13
#define G_T20_PIN_SPI_MOSI               11
#define G_T20_PIN_BMI_CS                 10
#define G_T20_PIN_BMI_INT1               14


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

// ============================================================================
// [설정 구조체]
// ============================================================================

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
    EM_T20_AxisType_t          axis;
    bool                       remove_dc;
    ST_T20_PreEmphasisConfig_t preemphasis;
    ST_T20_FilterConfig_t      filter;
    ST_T20_NoiseConfig_t       noise;
} ST_T20_PreprocessConfig_t;

typedef struct
{
    uint16_t fft_size;
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

// ============================================================================
// [출력 구조체]
// ============================================================================
typedef struct
{
    uint16_t log_mel_len;   // 실제 사용된 log-mel 개수
    uint16_t mfcc_len;      // 실제 사용된 mfcc 개수
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len;

    float log_mel[G_T20_MEL_FILTERS];
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;


/*
typedef struct
{
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;
*/

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

    cfg.preprocess.axis = EN_T20_AXIS_Z;
    cfg.preprocess.remove_dc = true;

    cfg.preprocess.preemphasis.enable = true;
    cfg.preprocess.preemphasis.alpha  = 0.97f;

    cfg.preprocess.filter.enable      = true;
    cfg.preprocess.filter.type        = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    cfg.preprocess.filter.cutoff_hz_2 = 250.0f;
    cfg.preprocess.filter.q_factor    = 0.707f;

    cfg.preprocess.noise.enable_gate = true;
    cfg.preprocess.noise.gate_threshold_abs = 0.002f;
    cfg.preprocess.noise.enable_spectral_subtract = true;
    cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    cfg.preprocess.noise.noise_learn_frames = G_T20_NOISE_MIN_FRAMES;

    cfg.feature.fft_size       = G_T20_FFT_SIZE;
    cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters    = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs    = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window   = G_T20_DELTA_WINDOW;

    cfg.output.output_mode      = EN_T20_OUTPUT_VECTOR;
    
    // 프레임 길이: FFT크기(256) / 샘플링(1600Hz) = 0.16초 = 160ms
    // 전체 시퀀스가 커버하는 시간 : 8 * 160ms = 1.28초
    cfg.output.sequence_frames  = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten = true;

    return cfg;
}