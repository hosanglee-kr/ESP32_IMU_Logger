// T20_Mfcc_Types_007.h

#pragma once

#include <Arduino.h>

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
    EN_T20_OUTPUT_VECTOR = 0,     // 39차원 단일 feature vector
    EN_T20_OUTPUT_SEQUENCE        // 최근 N프레임 ring buffer sequence
};

// ============================================================================
// [설정 구조체]
// ============================================================================

typedef struct
{
    bool  enable;
    float alpha;      // 0.95 ~ 0.98 권장
} ST_T20_PreEmphasisConfig_t;

typedef struct
{
    bool               enable;
    EM_T20_FilterType_t type;
    float              cutoff_hz_1;   // LPF/HPF cutoff, BPF low cut
    float              cutoff_hz_2;   // BPF high cut
    float              q_factor;
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
    EM_T20_AxisType_t        axis;
    bool                    remove_dc;
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
    uint16_t           sequence_frames;   // 예: 8
    bool               sequence_flatten;  // true면 T x D를 1차원으로 export
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
    float mfcc[13];
    float delta[13];
    float delta2[13];
    float vector39[39];
} ST_T20_FeatureVector_t;

// ============================================================================
// [시퀀스 ring buffer]
// ============================================================================

#define G_T20_MAX_SEQUENCE_FRAMES  16
#define G_T20_FEATURE_DIM          39

typedef struct
{
    float    data[G_T20_MAX_SEQUENCE_FRAMES][G_T20_FEATURE_DIM];
    uint16_t frames;
    uint16_t head;     // 다음 write 위치
    bool     full;
} ST_T20_FeatureRingBuffer_t;

