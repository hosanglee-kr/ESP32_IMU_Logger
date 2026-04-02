
/* ============================================================================
 * File: T212_Def_Sens_214.h
 * Summary: BMI270 Sensor Control & DSP/MFCC Pipeline Definitions
 * ========================================================================== */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "T210_Def_Com_214.h"


#define G_T20_LIVE_SOURCE_MODE_BMI270         1U
#define G_T20_SEQUENCE_FRAMES_MAX             16U



#define G_T20_PREPROCESS_STAGE_MAX    8U
#define G_T20_MFCC_HISTORY            5U

// 센서 데이터 해석 모드
#define G_T20_BMI270_AXIS_MODE_GYRO_Z    0U
#define G_T20_BMI270_AXIS_MODE_ACC_Z     1U
#define G_T20_BMI270_AXIS_MODE_GYRO_NORM 2U

#define G_T20_BMI270_BURST_AXIS_COUNT         3U
#define G_T20_BMI270_STATUS_TEXT_MAX          48U


#define G_T20_SEQUENCE_FRAMES_DEFAULT		   16U  // 1600Hz 샘플링에서 16프레임은 약 10ms 단위의 데이터 윈도우


/* --- BMI270 Register Map (v210 복구) --- */
#define G_T20_BMI270_REG_CHIP_ID       0x00U
#define G_T20_BMI270_REG_STATUS        0x03U
#define G_T20_BMI270_REG_GYR_X_LSB     0x0CU
#define G_T20_BMI270_REG_ACC_X_LSB     0x12U
#define G_T20_BMI270_REG_INT_STATUS_1  0x1DU
#define G_T20_BMI270_REG_READ_FLAG     0x80U




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



// 메시지 타입 정의
typedef struct {
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

// 링버퍼 타입 정의 (에러 해결 핵심)
typedef struct {
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;



/* --- Runtime Limits --- */
typedef struct {
    uint16_t queue_depth;
    uint16_t frame_max_samples;
    uint16_t temp_buffer_size;
} ST_T20_RuntimeLimits_t;

/* --- BMI270 상세 상태 관리 (v210의 수많은 상태 상수를 멤버로 전환) --- */
typedef struct {
    EM_T20_State_t master;       // 전체 드라이버 상태
    EM_T20_State_t spi_bus;      // v210: SPI_BEGIN, BUS_READY 등
    EM_T20_State_t isr_hook;     // v210: ISR_ATTACH, ISR_BRIDGE 등
    EM_T20_State_t burst_flow;   // v210: BURST_READ_PREP, BURST_APPLY 등
    EM_T20_State_t pipeline;     // v210: MEGA_PIPELINE, REAL_CONNECT 등
    EM_T20_State_t dsp_ingress;  // v210: DSP_READY, RAW_PIPE 등
} ST_T20_BMI270_RuntimeState_t;




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
    uint8_t buffer[16]; // G_T20_RUNTIME_LIMITS.queue_depth와 일치 확인 필요
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
    bool				enable;
    EM_T20_FilterType_t type;
    float				cutoff_hz_1;
    float				cutoff_hz_2;
    float				q_factor;
} ST_T20_FilterConfig_t;

typedef struct {
    bool     enable_gate;
    float    gate_threshold_abs;
    bool     enable_spectral_subtract;   // v210 복구
    float    spectral_subtract_strength; // v210 복구
    uint16_t noise_learn_frames;
} ST_T20_NoiseConfig_t;

/* ============================================================================
 * Feature Config / Vector
 * ========================================================================== */
/* --- Feature Config --- */
typedef struct {
    uint16_t fft_size;
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;  // sample_rate -> sample_rate_hz 로 이름 변경
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

// [DSP 특징 벡터 구조체] 
typedef struct {
    uint32_t frame_id;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len; 
    float    mfcc[G_T20_MFCC_COEFFS_MAX];
    float    delta[G_T20_MFCC_COEFFS_MAX];
    float    delta2[G_T20_MFCC_COEFFS_MAX];
    float    vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

/*
typedef struct {
    uint16_t mfcc_len;
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
} ST_T20_FeatureVector_t;
*/

/* ============================================================================
 * BMI270 State (통합 상태 관리)
 * ========================================================================== */

typedef struct {
	EM_T20_State_t master;     // 전체 드라이버 마스터 상태
    EM_T20_State_t spi;        // SPI 통신 개시/세션 상태
    EM_T20_State_t boot;       // BMI270 칩 인식 및 부팅 (v210 복구)
    EM_T20_State_t irq;        // IRQ Pin/Route 설정 (v210 복구)
    EM_T20_State_t init;       // 전체 초기화 완료 여부
    EM_T20_State_t read;       // 레지스터/버스트 읽기 상태
    EM_T20_State_t pipeline;   // MEGA_PIPELINE (데이터 흐름 연결)
    EM_T20_State_t hw_link;    // 하드웨어 브릿지 연결 상태
    EM_T20_State_t runtime;    // 실시간 루프 동작 상태
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





/* --- Pipeline Stage Definitions --- */
typedef enum {
    EN_T20_STAGE_DC_REMOVE = 0,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_GATE,
    EN_T20_STAGE_FILTER
} EM_T20_PipelineStageType_t;

typedef struct {
    bool                       enable;
    EM_T20_PipelineStageType_t stage_type;
    float                      param_1;  // preemphasis.alpha 또는 filter.cutoff1 등
    float                      param_2;  // filter.cutoff2 등
    float                      q_factor; // filter.q
} ST_T20_PipelineStage_t;

/* --- Preprocess Unified Config --- */
typedef struct {
    EM_T20_AxisType_t          axis;
    bool                       remove_dc;
    ST_T20_PreEmphasisConfig_t preemphasis;
    ST_T20_FilterConfig_t      filter;
    ST_T20_NoiseConfig_t       noise;

    struct {
        uint8_t                stage_count;
        ST_T20_PipelineStage_t stages[G_T20_PREPROCESS_STAGE_MAX]; // 스테이지를 초기화함
    } pipeline;
} ST_T20_PreprocessConfig_t;
