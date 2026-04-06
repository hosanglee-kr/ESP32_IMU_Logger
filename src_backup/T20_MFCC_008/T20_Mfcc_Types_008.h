#pragma once

#include <Arduino.h>

/* ============================================================================
 * File: T20_Mfcc_Types_008.h
 * Summary:
 *   Public type definitions for the T20 MFCC / vibration feature module.
 * ========================================================================== */

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
    float mfcc[G_T20_MFCC_COEFFS];
    float delta[G_T20_MFCC_COEFFS];
    float delta2[G_T20_MFCC_COEFFS];
    float vector39[G_T20_FEATURE_DIM_DEFAULT];
} ST_T20_FeatureVector2_t;

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
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;