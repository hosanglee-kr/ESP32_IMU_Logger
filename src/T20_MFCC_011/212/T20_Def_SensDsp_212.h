
/* ============================================================================
 * File: T20_Def_SensDsp_212.h
 * Summary: BMI270 Sensor Control & DSP/MFCC Pipeline Definitions (v210)
 * ========================================================================== */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "T20_Def_Comm_212.h"

/* ============================================================================
 * Global Constants (G_T20_)
 * ========================================================================== */

/* --- DSP --- */
static const uint16_t G_T20_FFT_SIZE               = 256U;
static const float    G_T20_SAMPLE_RATE_HZ         = 1600.0f;
static const uint16_t G_T20_MEL_FILTERS            = 26U;
static const uint16_t G_T20_MFCC_COEFFS_MAX        = 32U;
static const uint16_t G_T20_MFCC_COEFFS_DEFAULT    = 13U;

#define G_T20_FEATURE_DIM_MAX   (G_T20_MFCC_COEFFS_MAX * 3U)

/* --- Runtime Limits --- */
typedef struct {
    uint16_t queue_depth;
    uint16_t frame_max_samples;
    uint16_t temp_buffer_size;
} ST_T20_RuntimeLimits_t;

static const ST_T20_RuntimeLimits_t G_T20_RUNTIME_LIMITS = {
    .queue_depth = 16U,
    .frame_max_samples = 1024U,
    .temp_buffer_size = 512U
};

/* --- Hardware --- */
static const uint32_t G_T20_SPI_FREQ_HZ = 10000000UL;
static const uint8_t  G_T20_BMI270_CHIP_ID_EXPECTED = 0x24U;

/* ============================================================================
 * Common Enums
 * ========================================================================== */


/* --- Filter --- */
typedef enum {
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
} EM_T20_FilterType_t;

/* --- Axis --- */
typedef enum {
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
} EM_T20_AxisType_t;



/* ============================================================================
 * ISR Queue
 * ========================================================================== */

typedef struct {
    volatile uint8_t write_idx;
    volatile uint8_t read_idx;
    uint8_t buffer[16];
} ST_T20_ISRQueue_t;

static inline void T20_ISRQueue_Init(ST_T20_ISRQueue_t* q)
{
    q->write_idx = 0;
    q->read_idx = 0;
}

static inline bool T20_ISRQueue_Push(ST_T20_ISRQueue_t* q, uint8_t v)
{
    uint8_t next = (q->write_idx + 1) % G_T20_RUNTIME_LIMITS.queue_depth;
    if (next == q->read_idx) return false;
    q->buffer[q->write_idx] = v;
    q->write_idx = next;
    return true;
}

static inline bool T20_ISRQueue_Pop(ST_T20_ISRQueue_t* q, uint8_t* v)
{
    if (q->read_idx == q->write_idx) return false;
    *v = q->buffer[q->read_idx];
    q->read_idx = (q->read_idx + 1) % G_T20_RUNTIME_LIMITS.queue_depth;
    return true;
}

/* ============================================================================
 * Preprocess Pipeline
 * ========================================================================== */

typedef struct {
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

typedef struct {
    bool enable;
    EM_T20_FilterType_t type;
    float cutoff1;
    float cutoff2;
    float q;
} ST_T20_FilterConfig_t;

typedef struct {
    bool enable_gate;
    float threshold;
} ST_T20_NoiseConfig_t;

/* ============================================================================
 * Feature Config / Vector
 * ========================================================================== */

typedef struct {
    uint16_t fft_size;
    uint16_t hop_size;
    float sample_rate;
    uint16_t mfcc_coeffs;
} ST_T20_FeatureConfig_t;

typedef struct {
    uint16_t mfcc_len;
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
} ST_T20_FeatureVector_t;

/* ============================================================================
 * BMI270 State (통합 상태 관리)
 * ========================================================================== */

typedef struct {
    EM_T20_State_t spi;
    EM_T20_State_t init;
    EM_T20_State_t read;
    EM_T20_State_t pipeline;
    EM_T20_State_t runtime;
} ST_T20_BMI270_State_t;

/* ============================================================================
 * State Machine (함수화)
 * ========================================================================== */

typedef struct {
    ST_T20_BMI270_State_t state;
} ST_T20_BMI270_Handle_t;

static inline void T20_BMI270_StateMachine(ST_T20_BMI270_Handle_t* h)
{
    switch (h->state.spi)
    {
        case EN_T20_STATE_IDLE:
            h->state.spi = EN_T20_STATE_READY;
            break;

        case EN_T20_STATE_READY:
            h->state.spi = EN_T20_STATE_RUNNING;
            break;

        case EN_T20_STATE_RUNNING:
            h->state.spi = EN_T20_STATE_DONE;
            break;

        case EN_T20_STATE_DONE:
        default:
            break;
    }
}

/* ============================================================================
 * Debug Helper
 * ========================================================================== */

static inline void T20_Debug_PrintState(ST_T20_BMI270_State_t* s)
{
    printf("[SPI:%s INIT:%s READ:%s PIPE:%s RUN:%s]\n",
        T20_StateToString(s->spi),
        T20_StateToString(s->init),
        T20_StateToString(s->read),
        T20_StateToString(s->pipeline),
        T20_StateToString(s->runtime));
}
