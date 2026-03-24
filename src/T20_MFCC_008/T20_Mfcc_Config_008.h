#pragma once

#include "T20_Mfcc_Types_008.h"

/* ============================================================================
 * File: T20_Mfcc_Config_008.h
 * Summary:
 *   Default constants and default config factory for the T20 MFCC module.
 * ========================================================================== */

// ============================================================================
// [기본 상수]
// ============================================================================

#define G_T20_VERSION_STR                "T20_Mfcc_008"

#define G_T20_RAW_FRAME_BUFFERS          4
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
    cfg.feature.mfcc_coeffs    = G_T20_MFCC_COEFFS;
    cfg.feature.delta_window   = G_T20_DELTA_WINDOW;

    cfg.output.output_mode      = EN_T20_OUTPUT_VECTOR;
    
    // 프레임 길이: FFT크기(256) / 샘플링(1600Hz) = 0.16초 = 160ms
    // 전체 시퀀스가 커버하는 시간 : 8 * 160ms = 1.28초
    cfg.output.sequence_frames  = 8;
    cfg.output.sequence_flatten = true;

    return cfg;
}