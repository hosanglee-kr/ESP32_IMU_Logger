#pragma once

/* ============================================================================
 * File: T20_Mfcc_Def_011.h
 * Summary:
 *   T20 MFCC / vibration feature module public definitions.
 *
 *   This header contains:
 *   1) Public constants
 *   2) Pin definitions
 *   3) Public enums
 *   4) Public configuration structs
 *   5) Public output structs
 *   6) Ring buffer structs
 *   7) Default config factory
 *
 * [Bundle-A scope]
 * - Preprocess stage array design finalized
 * - Sliding window / hop_size structure finalized
 * - Runtime max-size buffer strategy finalized
 * - Public API-facing structures fixed for v011 baseline
 *
 * [Future planned items - keep in mind]
 * - Measurement session state machine (READY / RECORDING / STOPPING / ERROR)
 * - Button-based measurement start / stop / marker input
 * - SD_MMC recorder layer
 *   - raw data: binary / CSV selectable
 *   - config dump: JSON
 *   - metadata / event log: JSONL / CSV selectable
 * - AsyncWeb control / live visualization API
 * - Multi-axis feature mode and sensor fusion expansion
 * - Multi-config FFT/mel-bank cache pools
 * - DMA / zero-copy / cache-aware optimization pass
 * ========================================================================== */

#include <Arduino.h>

// ============================================================================
// [Version / Static Limits]
// ============================================================================

#define G_T20_VERSION_STR                    "T20_Mfcc_011"

// Raw sample ring size.
// Must be larger than frame_size and large enough for overlap + task lag.
#define G_T20_SAMPLE_RING_SIZE               2048U

// Sliding frame dispatch queue depth.
#define G_T20_FRAME_QUEUE_LEN                8U

// FFT/frame size policy for Bundle-A.
// v011 keeps fixed FFT size for simpler migration and stable DSP table usage.
#define G_T20_FRAME_SIZE_FIXED               256U
#define G_T20_SAMPLE_RATE_HZ_DEFAULT         1600.0f
#define G_T20_MEL_FILTERS_FIXED              26U

// MFCC dimension policy.
#define G_T20_MFCC_COEFFS_DEFAULT            13U
#define G_T20_MFCC_COEFFS_MAX                32U
#define G_T20_FEATURE_DIM_DEFAULT            (G_T20_MFCC_COEFFS_DEFAULT * 3U)
#define G_T20_FEATURE_DIM_MAX                (G_T20_MFCC_COEFFS_MAX * 3U)

// Sequence policy.
#define G_T20_SEQUENCE_FRAMES_DEFAULT        8U
#define G_T20_SEQUENCE_FRAMES_MAX            16U

// Delta history policy.
#define G_T20_MFCC_HISTORY                   5U
#define G_T20_DELTA_WINDOW_DEFAULT           2U

// Preprocess stage capacity.
#define G_T20_PREPROCESS_STAGE_MAX           8U

// Noise learning minimum recommendation.
#define G_T20_NOISE_MIN_FRAMES               8U

// RTOS sizes.
#define G_T20_SENSOR_TASK_STACK              6144U
#define G_T20_PROCESS_TASK_STACK             12288U
#define G_T20_SENSOR_TASK_PRIO               4U
#define G_T20_PROCESS_TASK_PRIO              3U

// SPI / math.
#define G_T20_SPI_FREQ_HZ                    10000000UL
#define G_T20_PI                             3.14159265358979323846f
#define G_T20_EPSILON                        1.0e-12f

// ============================================================================
// [Pin Mapping]
// ============================================================================

#define G_T20_PIN_SPI_SCK                    12
#define G_T20_PIN_SPI_MISO                   13
#define G_T20_PIN_SPI_MOSI                   11
#define G_T20_PIN_BMI_CS                     10
#define G_T20_PIN_BMI_INT1                   14

// ============================================================================
// [Enums]
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

// New: preprocess stage type.
// Filter stages are modeled as pipeline stages rather than one fixed filter block.
enum EM_T20_PreprocessStageType_t
{
    EN_T20_STAGE_NONE = 0,
    EN_T20_STAGE_DC_REMOVE,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_NOISE_GATE,
    EN_T20_STAGE_BIQUAD_LPF,
    EN_T20_STAGE_BIQUAD_HPF,
    EN_T20_STAGE_BIQUAD_BPF,
    EN_T20_STAGE_RESERVED_1,
    EN_T20_STAGE_RESERVED_2
};

// ============================================================================
// [Config Structs]
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

// Stage-oriented preprocess config.
// param_1 / param_2 / q_factor meaning depends on stage_type.
typedef struct
{
    bool                         enable;
    EM_T20_PreprocessStageType_t stage_type;
    float                        param_1;
    float                        param_2;
    float                        q_factor;
    float                        reserved_1;
    float                        reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t                     stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;

typedef struct
{
    EM_T20_AxisType_t axis;
} ST_T20_InputConfig_t;

typedef struct
{
    uint16_t frame_size;      // v011 fixed validation target = G_T20_FRAME_SIZE_FIXED
    uint16_t hop_size;        // frame_size => no overlap, frame_size/2 => 50% overlap
    float    sample_rate_hz;
    uint16_t mel_filters;     // v011 fixed validation target = G_T20_MEL_FILTERS_FIXED
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
    ST_T20_InputConfig_t              input;
    ST_T20_PreprocessPipelineConfig_t pipeline;
    ST_T20_NoiseConfig_t              noise;
    ST_T20_FeatureConfig_t            feature;
    ST_T20_OutputConfig_t             output;
} ST_T20_Config_t;

// ============================================================================
// [Outputs]
// ============================================================================

typedef struct
{
    uint16_t log_mel_len;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len;

    float log_mel[G_T20_MEL_FILTERS_FIXED];
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

// ============================================================================
// [Defaults]
// ============================================================================

static inline ST_T20_PreprocessStageConfig_t T20_makeStageNone(void)
{
    ST_T20_PreprocessStageConfig_t v_stage;
    memset(&v_stage, 0, sizeof(v_stage));
    v_stage.enable = false;
    v_stage.stage_type = EN_T20_STAGE_NONE;
    return v_stage;
}

static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t v_cfg;
    memset(&v_cfg, 0, sizeof(v_cfg));

    v_cfg.input.axis = EN_T20_AXIS_Z;

    // Recommended default pipeline:
    // 1) DC remove
    // 2) HPF 15Hz
    // 3) Noise gate
    // 4) Pre-emphasis
    v_cfg.pipeline.stage_count = 4;
    v_cfg.pipeline.stages[0] = { true, EN_T20_STAGE_DC_REMOVE,    0.0f,   0.0f,   0.0f,   0.0f, 0.0f };
    v_cfg.pipeline.stages[1] = { true, EN_T20_STAGE_BIQUAD_HPF,  15.0f,   0.0f,   0.707f, 0.0f, 0.0f };
    v_cfg.pipeline.stages[2] = { true, EN_T20_STAGE_NOISE_GATE,   0.002f, 0.0f,   0.0f,   0.0f, 0.0f };
    v_cfg.pipeline.stages[3] = { true, EN_T20_STAGE_PREEMPHASIS,  0.97f,  0.0f,   0.0f,   0.0f, 0.0f };

    for (uint16_t v_i = 4; v_i < G_T20_PREPROCESS_STAGE_MAX; ++v_i) {
        v_cfg.pipeline.stages[v_i] = T20_makeStageNone();
    }

    v_cfg.noise.enable_gate = true;
    v_cfg.noise.gate_threshold_abs = 0.002f;
    v_cfg.noise.enable_spectral_subtract = true;
    v_cfg.noise.spectral_subtract_strength = 1.0f;
    v_cfg.noise.noise_learn_frames = G_T20_NOISE_MIN_FRAMES;

    v_cfg.feature.frame_size = G_T20_FRAME_SIZE_FIXED;
    v_cfg.feature.hop_size = G_T20_FRAME_SIZE_FIXED;   // no overlap default
    v_cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ_DEFAULT;
    v_cfg.feature.mel_filters = G_T20_MEL_FILTERS_FIXED;
    v_cfg.feature.mfcc_coeffs = G_T20_MFCC_COEFFS_DEFAULT;
    v_cfg.feature.delta_window = G_T20_DELTA_WINDOW_DEFAULT;

    v_cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;
    v_cfg.output.sequence_frames = G_T20_SEQUENCE_FRAMES_DEFAULT;
    v_cfg.output.sequence_flatten = true;

    return v_cfg;
}
