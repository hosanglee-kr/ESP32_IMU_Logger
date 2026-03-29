
/* ============================================================================
 * File: T20_Def_SensDsp_021.h
 * Summary: BMI270 Sensor Control & DSP/MFCC Pipeline Definitions (v210)
 * ========================================================================== */

#pragma once

#include "T20_Def_Comm_021.h"

/* --- [DSP/MFCC Core Constants] --- */
#define G_T20_FFT_SIZE                            256U
#define G_T20_SAMPLE_RATE_HZ                      1600.0f
#define G_T20_MEL_FILTERS                         26U
#define G_T20_MFCC_COEFFS_MAX                     32U
#define G_T20_MFCC_COEFFS_DEFAULT                 13U
#define G_T20_FEATURE_DIM_MAX                     (G_T20_MFCC_COEFFS_MAX * 3U)
#define G_T20_FEATURE_DIM_DEFAULT                 (G_T20_MFCC_COEFFS_DEFAULT * 3U)
#define G_T20_SEQUENCE_FRAMES_MAX                 16U
#define G_T20_SEQUENCE_FRAMES_DEFAULT             8U
#define G_T20_PREPROCESS_STAGE_MAX                8U
#define G_T20_MFCC_HISTORY                        5U
#define G_T20_NOISE_MIN_FRAMES                    8U
#define G_T20_PI                                  3.14159265358979323846f
#define G_T20_EPSILON                             1.0e-12f

/* --- [BMI270 Hardware & SPI Register Constants] --- */
#define G_T20_SPI_FREQ_HZ                         10000000UL
#define G_T20_BMI270_CHIP_ID_EXPECTED             0x24U
#define G_T20_BMI270_REG_CHIP_ID                  0x00U
#define G_T20_BMI270_REG_STATUS                   0x03U
#define G_T20_BMI270_REG_GYR_X_LSB                0x0CU
#define G_T20_BMI270_REG_ACC_X_LSB                0x12U
#define G_T20_BMI270_REG_INT_STATUS_1             0x1DU
#define G_T20_BMI270_REG_READ_FLAG                0x80U
#define G_T20_BMI270_SPI_DUMMY_READ               0x00U
#define G_T20_BMI270_BURST_SAMPLE_BYTES           6U
#define G_T20_BMI270_BURST_AXIS_COUNT             3U
#define G_T20_BMI270_REG_FAKE_VECTOR_BASE         0x40U
#define G_T20_BMI270_FAKE_RAW_DECODE_SCALE        100.0f

/* --- [Runtime & Simulation Logic Constants] --- */
#define G_T20_DSP_ENABLE_RUNTIME_FILTER           1U
#define G_T20_DSP_ENABLE_RUNTIME_NOISE_PROFILE    1U
#define G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS       160U
#define G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT       0.20f
#define G_T20_BMI270_SIM_SAMPLE_INTERVAL_MS       5U
#define G_T20_BMI270_AXIS_MODE_GYRO_Z             0U
#define G_T20_BMI270_AXIS_MODE_ACC_Z              1U
#define G_T20_BMI270_AXIS_MODE_GYRO_NORM          2U
#define G_T20_LIVE_SOURCE_MODE_SYNTHETIC          0U
#define G_T20_LIVE_SOURCE_MODE_BMI270             1U
#define G_T20_LIVE_SOURCE_MODE_OFF                255U
#define G_T20_BMI270_INIT_RETRY_MAX               3U
#define G_T20_BMI270_SPI_RETRY_MAX                3U
#define G_T20_BMI270_DRDY_POLL_INTERVAL_MS        2U
#define G_T20_LIVE_DRDY_TIMEOUT_MS                100U
#define G_T20_LIVE_QUEUE_DEPTH                    16U
#define G_T20_BMI270_ISR_FLAG_SET                 1U
#define G_T20_BMI270_ISR_QUEUE_SIM_MAX            4U
#define G_T20_LIVE_FRAME_MAX_SAMPLES              1024U
#define G_T20_LIVE_FRAME_TEMP_MAX                 512U
#define G_T20_BMI270_STATUS_TEXT_MAX              48U

/* --- [BMI270 / DSP State Transition Constants (All)] --- */
#define G_T20_BMI270_SPI_BEGIN_OK                 1U
#define G_T20_BMI270_SPI_BEGIN_FAIL               0U
#define G_T20_BMI270_SPI_BUS_READY                1U
#define G_T20_BMI270_SPI_BUS_NOT_READY            0U
#define G_T20_BMI270_SPI_TRANSACTION_OK           1U
#define G_T20_BMI270_SPI_TRANSACTION_FAIL         0U
#define G_T20_BMI270_SPI_READ_OK                  1U
#define G_T20_BMI270_AXIS_DECODE_OK               1U
#define G_T20_BMI270_AXIS_DECODE_FAIL             0U
#define G_T20_BMI270_ISR_ATTACH_OK                1U
#define G_T20_BMI270_ISR_HOOK_READY               1U
#define G_T20_BMI270_ISR_ATTACH_STATE_READY       1U

#define G_T20_BMI270_READ_STATE_IDLE              0U
#define G_T20_BMI270_READ_STATE_PREPARED          1U
#define G_T20_BMI270_READ_STATE_DONE              2U

#define G_T20_BMI270_DRIVER_STATE_IDLE            0U
#define G_T20_BMI270_DRIVER_STATE_READY           1U
#define G_T20_BMI270_DRIVER_STATE_DONE            2U

#define G_T20_BMI270_BOOT_STATE_IDLE              0U
#define G_T20_BMI270_BOOT_STATE_READY             1U
#define G_T20_BMI270_BOOT_STATE_DONE              2U

#define G_T20_BMI270_SPI_BEGIN_RUNTIME_STATE_IDLE    0U
#define G_T20_BMI270_SPI_BEGIN_RUNTIME_STATE_READY   1U
#define G_T20_BMI270_SPI_BEGIN_RUNTIME_STATE_DONE    2U

#define G_T20_BMI270_REGISTER_READ_RUNTIME_STATE_IDLE 0U
#define G_T20_BMI270_REGISTER_READ_RUNTIME_STATE_READY 1U
#define G_T20_BMI270_REGISTER_READ_RUNTIME_STATE_DONE  2U

#define G_T20_BMI270_BURST_RUNTIME_STATE_IDLE       0U
#define G_T20_BMI270_BURST_RUNTIME_STATE_READY      1U
#define G_T20_BMI270_BURST_RUNTIME_STATE_DONE       2U

#define G_T20_BMI270_HW_LINK_STATE_IDLE             0U
#define G_T20_BMI270_HW_LINK_STATE_READY            1U
#define G_T20_BMI270_HW_LINK_STATE_DONE             2U

#define G_T20_BMI270_PIPELINE_LINK_STATE_IDLE       0U
#define G_T20_BMI270_PIPELINE_LINK_STATE_READY      1U
#define G_T20_BMI270_PIPELINE_LINK_STATE_DONE       2U

#define G_T20_BMI270_MEGA_PIPELINE_STATE_IDLE       0U
#define G_T20_BMI270_MEGA_PIPELINE_STATE_READY      1U
#define G_T20_BMI270_MEGA_PIPELINE_STATE_DONE       2U

#define G_T20_BMI270_DSP_INGRESS_STATE_IDLE         0U
#define G_T20_BMI270_DSP_INGRESS_STATE_READY        1U
#define G_T20_BMI270_DSP_INGRESS_STATE_DONE         2U

#define G_T20_BMI270_ISR_QUEUE_STATE_IDLE           0U
#define G_T20_BMI270_ISR_QUEUE_STATE_READY          1U
#define G_T20_BMI270_ISR_QUEUE_STATE_DONE           2U

#define G_T20_BMI270_CONNECT_PREP_STATE_IDLE        0U
#define G_T20_BMI270_CONNECT_PREP_STATE_READY       1U
#define G_T20_BMI270_CONNECT_PREP_STATE_DONE        2U

#define G_T20_BMI270_REAL_CONNECT_STAGE_IDLE        0U
#define G_T20_BMI270_REAL_CONNECT_STAGE_READY       1U
#define G_T20_BMI270_REAL_CONNECT_STAGE_DONE        2U

#define G_T20_BMI270_ACTUAL_REG_READ_READY          1U
#define G_T20_BMI270_ACTUAL_REG_READ_IDLE           0U
#define G_T20_BMI270_ACTUAL_BURST_READY             1U
#define G_T20_BMI270_ACTUAL_BURST_IDLE              0U

#define G_T20_BMI270_ISR_REQUEST_IDLE               0U
#define G_T20_BMI270_ISR_REQUEST_PENDING            1U
#define G_T20_BMI270_ISR_REQUEST_CONSUMED           2U

#define G_T20_BMI270_VERIFY_STATE_IDLE              0U
#define G_T20_BMI270_VERIFY_STATE_READY             1U
#define G_T20_BMI270_VERIFY_STATE_DONE              2U
#define G_T20_BMI270_VERIFY_RESULT_NONE             0U
#define G_T20_BMI270_VERIFY_RESULT_OK               1U
#define G_T20_BMI270_VERIFY_RESULT_FAIL             2U

/* --- [Enums & Structures] --- */
enum EM_T20_FilterType_t {
    EN_T20_FILTER_OFF = 0, EN_T20_FILTER_LPF, EN_T20_FILTER_HPF, EN_T20_FILTER_BPF
};

enum EM_T20_AxisType_t {
    EN_T20_AXIS_X = 0, EN_T20_AXIS_Y, EN_T20_AXIS_Z
};

enum EM_T20_PreprocessStageType_t {
    EN_T20_STAGE_NONE = 0, EN_T20_STAGE_DC_REMOVE, EN_T20_STAGE_PREEMPHASIS, EN_T20_STAGE_GATE, EN_T20_STAGE_FILTER
};

typedef struct { bool enable; float alpha; } ST_T20_PreEmphasisConfig_t;
typedef struct { bool enable; EM_T20_FilterType_t type; float cutoff_hz_1; float cutoff_hz_2; float q_factor; } ST_T20_FilterConfig_t;
typedef struct { bool enable_gate; float gate_threshold_abs; bool enable_spectral_subtract; float spectral_subtract_strength; uint16_t noise_learn_frames; } ST_T20_NoiseConfig_t;

typedef struct {
    bool enable;
    EM_T20_PreprocessStageType_t stage_type;
    float param_1; float param_2; float q_factor;
    float reserved_1; float reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct {
    uint16_t stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;

typedef struct {
    EM_T20_AxisType_t axis;
    bool remove_dc;
    ST_T20_PreEmphasisConfig_t preemphasis;
    ST_T20_FilterConfig_t filter;
    ST_T20_NoiseConfig_t noise;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct {
    uint16_t fft_size; uint16_t frame_size; uint16_t hop_size;
    float sample_rate_hz; uint16_t mel_filters; uint16_t mfcc_coeffs; uint16_t delta_window;
} ST_T20_FeatureConfig_t;

typedef struct {
    uint16_t log_mel_len; uint16_t mfcc_len; uint16_t delta_len; uint16_t delta2_len; uint16_t vector_len;
    float log_mel[G_T20_MEL_FILTERS];
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

typedef struct {
    float data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames; uint16_t feature_dim; uint16_t head; bool full;
} ST_T20_FeatureRingBuffer_t;

typedef struct {
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

