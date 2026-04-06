/* ============================================================================
 * File: T212_Def_Sens_216.h
 * Summary: BMI270 Sensor Control & DSP/MFCC Pipeline Definitions
 * ========================================================================== */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "T210_Def_Com_216.h"

// ----------------------------------------------------------------------------
// 1. 공통 Enum
// ----------------------------------------------------------------------------
typedef enum {
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
} EM_T20_FilterType_t;

typedef enum {
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
} EM_T20_AxisType_t;

typedef enum {
    EN_T20_NOISE_OFF = 0,
    EN_T20_NOISE_FIXED,
    EN_T20_NOISE_ADAPTIVE
} EM_T20_NoiseMode_t;

typedef enum {
    EN_T20_STAGE_DC_REMOVE = 0,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_GATE,
    EN_T20_STAGE_FILTER
} EM_T20_PipelineStageType_t;

// ----------------------------------------------------------------------------
// 2. BMI270 & ISR State
// ----------------------------------------------------------------------------
typedef struct {
    EM_T20_State_t master;       
    EM_T20_State_t spi_bus;      
    EM_T20_State_t isr_hook;     
    EM_T20_State_t burst_flow;   
    EM_T20_State_t pipeline;     
    EM_T20_State_t dsp_ingress;  
} ST_T20_BMI270_RuntimeState_t;

typedef struct {
    EM_T20_State_t master;     
    EM_T20_State_t spi;        
    EM_T20_State_t boot;       
    EM_T20_State_t irq;        
    EM_T20_State_t init;       
    EM_T20_State_t read;       
    EM_T20_State_t pipeline;   
    EM_T20_State_t hw_link;    
    EM_T20_State_t runtime;    
} ST_T20_BMI270_State_t;

typedef struct {
    volatile uint8_t write_idx;
    volatile uint8_t read_idx;
    uint8_t buffer[T20::C10_Sys::QUEUE_LEN * 2]; 
} ST_T20_ISRQueue_t;

static inline void T20_ISRQueue_Init(ST_T20_ISRQueue_t* q) {
    q->write_idx = 0;
    q->read_idx = 0;
}

// ----------------------------------------------------------------------------
// 3. DSP & Feature Config / Vector
// ----------------------------------------------------------------------------
typedef struct {
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

typedef struct {
    float    data[T20::C10_Sys::SEQUENCE_FRAMES_MAX][T20::C10_DSP::FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;

typedef struct {
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

typedef struct {
    bool                enable;
    EM_T20_FilterType_t type;
    float               cutoff_hz_1;
    float               cutoff_hz_2;
    float               q_factor;
} ST_T20_FilterConfig_t;

typedef struct {
    bool                enable_gate;
    float               gate_threshold_abs;
    EM_T20_NoiseMode_t  mode;               
    float               spectral_subtract_strength;
    float               adaptive_alpha;     
    uint16_t            noise_learn_frames;
} ST_T20_NoiseConfig_t;

typedef struct {
    uint16_t fft_size;
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;  
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

typedef struct {
    uint32_t frame_id;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len; 
    float    mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    float    delta[T20::C10_DSP::MFCC_COEFFS_MAX];
    float    delta2[T20::C10_DSP::MFCC_COEFFS_MAX];
    float    vector[T20::C10_DSP::FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

typedef struct {
    bool                       enable;
    EM_T20_PipelineStageType_t stage_type;
    float                      param_1;  
    float                      param_2;  
    float                      q_factor; 
} ST_T20_PipelineStage_t;

typedef struct {
    EM_T20_AxisType_t          axis;
    bool                       remove_dc;
    ST_T20_PreEmphasisConfig_t preemphasis;
    ST_T20_FilterConfig_t      filter;
    ST_T20_NoiseConfig_t       noise;

    struct {
        uint8_t                stage_count;
        ST_T20_PipelineStage_t stages[T20::C10_DSP::PREPROCESS_STAGE_MAX]; 
    } pipeline;
} ST_T20_PreprocessConfig_t;

