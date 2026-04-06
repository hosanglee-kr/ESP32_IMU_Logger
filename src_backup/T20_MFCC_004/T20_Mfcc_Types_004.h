// T20_Mfcc_Types_004.h

#pragma once

#include <Arduino.h>

// ============================================================================
// [열거형]
// ============================================================================

enum T20_FilterType_004
{
    EN_T20_FILTER_OFF_004 = 0,
    EN_T20_FILTER_LPF_004,
    EN_T20_FILTER_HPF_004,
    EN_T20_FILTER_BPF_004
};

enum T20_AxisType_004
{
    EN_T20_AXIS_X_004 = 0,
    EN_T20_AXIS_Y_004,
    EN_T20_AXIS_Z_004
};

enum T20_OutputMode_004
{
    EN_T20_OUTPUT_VECTOR_004 = 0,     // 39차원 단일 feature vector
    EN_T20_OUTPUT_SEQUENCE_004        // 최근 N프레임 ring buffer sequence
};

// ============================================================================
// [설정 구조체]
// ============================================================================

typedef struct
{
    bool  enable;
    float alpha;      // 0.95 ~ 0.98 권장
} T20_PreEmphasisConfig_004_t;

typedef struct
{
    bool               enable;
    T20_FilterType_004 type;
    float              cutoff_hz_1;   // LPF/HPF cutoff, BPF low cut
    float              cutoff_hz_2;   // BPF high cut
    float              q_factor;
} T20_FilterConfig_004_t;

typedef struct
{
    bool     enable_gate;
    float    gate_threshold_abs;

    bool     enable_spectral_subtract;
    float    spectral_subtract_strength;
    uint16_t noise_learn_frames;
} T20_NoiseConfig_004_t;

typedef struct
{
    T20_AxisType_004        axis;
    bool                    remove_dc;
    T20_PreEmphasisConfig_004_t preemphasis;
    T20_FilterConfig_004_t      filter;
    T20_NoiseConfig_004_t       noise;
} T20_PreprocessConfig_004_t;

typedef struct
{
    uint16_t fft_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} T20_FeatureConfig_004_t;

typedef struct
{
    T20_OutputMode_004 output_mode;
    uint16_t           sequence_frames;   // 예: 8
    bool               sequence_flatten;  // true면 T x D를 1차원으로 export
} T20_OutputConfig_004_t;

typedef struct
{
    T20_PreprocessConfig_004_t preprocess;
    T20_FeatureConfig_004_t    feature;
    T20_OutputConfig_004_t     output;
} T20_Config_004_t;

// ============================================================================
// [출력 구조체]
// ============================================================================

typedef struct
{
    float mfcc[13];
    float delta[13];
    float delta2[13];
    float vector39[39];
} T20_FeatureVector_004_t;

// ============================================================================
// [시퀀스 ring buffer]
// ============================================================================

#define G_T20_MAX_SEQUENCE_FRAMES_004  16
#define G_T20_FEATURE_DIM_004          39

typedef struct
{
    float    data[G_T20_MAX_SEQUENCE_FRAMES_004][G_T20_FEATURE_DIM_004];
    uint16_t frames;
    uint16_t head;     // 다음 write 위치
    bool     full;
} T20_FeatureRingBuffer_004_t;

