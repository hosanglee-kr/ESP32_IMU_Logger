// T20_Mfcc_Config_005.h

#pragma once

#include "T20_Mfcc_Types_005.h"

// ============================================================================
// [기본 상수]
// ============================================================================

#define G_T20_VERSION_STR                "T20_Mfcc_004"

#define G_T20_FFT_SIZE                   256
#define G_T20_SAMPLE_RATE_HZ             1600.0f
#define G_T20_MEL_FILTERS                26
#define G_T20_MFCC_COEFFS                13
#define G_T20_DELTA_WINDOW               2

#define G_T20_FEATURE_DIM_TOTAL          39

#define G_T20_QUEUE_LEN                  4
#define G_T20_SENSOR_TASK_STACK          6144
#define G_T20_PROCESS_TASK_STACK         12288
#define G_T20_SENSOR_TASK_PRIO           4
#define G_T20_PROCESS_TASK_PRIO          3

#define G_T20_MFCC_HISTORY               5
#define G_T20_NOISE_MIN_FRAMES           8

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
// [기본 설정 생성 함수]
// ============================================================================

static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t v_cfg;

    v_cfg.preprocess.axis = EN_T20_AXIS_Z;
    v_cfg.preprocess.remove_dc = true;

    v_cfg.preprocess.preemphasis.enable = true;
    v_cfg.preprocess.preemphasis.alpha  = 0.97f;

    v_cfg.preprocess.filter.enable      = true;
    v_cfg.preprocess.filter.type        = EN_T20_FILTER_HPF;
    v_cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    v_cfg.preprocess.filter.cutoff_hz_2 = 250.0f;
    v_cfg.preprocess.filter.q_factor    = 0.707f;

    v_cfg.preprocess.noise.enable_gate               = true;
    v_cfg.preprocess.noise.gate_threshold_abs        = 0.002f;
    v_cfg.preprocess.noise.enable_spectral_subtract  = true;
    v_cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    v_cfg.preprocess.noise.noise_learn_frames        = G_T20_NOISE_MIN_FRAMES;

    v_cfg.feature.fft_size       = G_T20_FFT_SIZE;
    v_cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ;
    v_cfg.feature.mel_filters    = G_T20_MEL_FILTERS;
    v_cfg.feature.mfcc_coeffs    = G_T20_MFCC_COEFFS;
    v_cfg.feature.delta_window   = G_T20_DELTA_WINDOW;

    v_cfg.output.output_mode      = EN_T20_OUTPUT_VECTOR;
    v_cfg.output.sequence_frames  = 8;
    v_cfg.output.sequence_flatten = true;

    return v_cfg;
}


