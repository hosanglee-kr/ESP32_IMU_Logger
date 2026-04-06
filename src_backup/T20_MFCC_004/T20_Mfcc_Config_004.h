// T20_Mfcc_Config_004.h

#pragma once

#include "T20_Mfcc_Types_004.h"

// ============================================================================
// [기본 상수]
// ============================================================================

#define G_T20_VERSION_STR_004                "T20_Mfcc_004"

#define G_T20_FFT_SIZE_004                   256
#define G_T20_SAMPLE_RATE_HZ_004             1600.0f
#define G_T20_MEL_FILTERS_004                26
#define G_T20_MFCC_COEFFS_004                13
#define G_T20_DELTA_WINDOW_004               2

#define G_T20_FEATURE_DIM_TOTAL_004          39

#define G_T20_QUEUE_LEN_004                  4
#define G_T20_SENSOR_TASK_STACK_004          6144
#define G_T20_PROCESS_TASK_STACK_004         12288
#define G_T20_SENSOR_TASK_PRIO_004           4
#define G_T20_PROCESS_TASK_PRIO_004          3

#define G_T20_MFCC_HISTORY_004               5
#define G_T20_NOISE_MIN_FRAMES_004           8

#define G_T20_SPI_FREQ_HZ_004                10000000UL

#define G_T20_PI_004                         3.14159265358979323846f
#define G_T20_EPSILON_004                    1.0e-12f

// ============================================================================
// [핀 설정]
// ============================================================================

#define G_T20_PIN_SPI_SCK_004                12
#define G_T20_PIN_SPI_MISO_004               13
#define G_T20_PIN_SPI_MOSI_004               11
#define G_T20_PIN_BMI_CS_004                 10
#define G_T20_PIN_BMI_INT1_004               14

// ============================================================================
// [기본 설정 생성 함수]
// ============================================================================

static inline T20_Config_004_t T20_makeDefaultConfig_004(void)
{
    T20_Config_004_t v_cfg;

    v_cfg.preprocess.axis = EN_T20_AXIS_Z_004;
    v_cfg.preprocess.remove_dc = true;

    v_cfg.preprocess.preemphasis.enable = true;
    v_cfg.preprocess.preemphasis.alpha  = 0.97f;

    v_cfg.preprocess.filter.enable      = true;
    v_cfg.preprocess.filter.type        = EN_T20_FILTER_HPF_004;
    v_cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    v_cfg.preprocess.filter.cutoff_hz_2 = 250.0f;
    v_cfg.preprocess.filter.q_factor    = 0.707f;

    v_cfg.preprocess.noise.enable_gate               = true;
    v_cfg.preprocess.noise.gate_threshold_abs        = 0.002f;
    v_cfg.preprocess.noise.enable_spectral_subtract  = true;
    v_cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    v_cfg.preprocess.noise.noise_learn_frames        = G_T20_NOISE_MIN_FRAMES_004;

    v_cfg.feature.fft_size       = G_T20_FFT_SIZE_004;
    v_cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ_004;
    v_cfg.feature.mel_filters    = G_T20_MEL_FILTERS_004;
    v_cfg.feature.mfcc_coeffs    = G_T20_MFCC_COEFFS_004;
    v_cfg.feature.delta_window   = G_T20_DELTA_WINDOW_004;

    v_cfg.output.output_mode      = EN_T20_OUTPUT_VECTOR_004;
    v_cfg.output.sequence_frames  = 8;
    v_cfg.output.sequence_flatten = true;

    return v_cfg;
}


