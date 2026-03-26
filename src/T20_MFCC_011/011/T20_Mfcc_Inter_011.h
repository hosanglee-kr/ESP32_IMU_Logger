#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_011.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_011.h
버전: v011

[기능 스펙]
- 외부 공개하지 않을 내부 구현 구조체 정의
- Core / DSP 분리 컴파일을 위한 공용 내부 선언 제공
- Sliding Window / Pipeline Snapshot 구조 제공

[설계 목적]
1. 공개 API와 내부 구현 세부를 분리
2. Core / DSP 파일이 동일한 내부 타입을 공유
3. 설정 변경(setConfig) 중에도 frame 처리 일관성을 보장하기 위한
   snapshot 기반 처리 구조 제공
4. Stage 배열 기반 전처리와 hop 기반 프레임 조립 구조를 명확히 분리

[향후 단계 구현 예정 사항]
- Session state / recording state 추가
- SD_MMC writer 상태 추가
- AsyncWeb live snapshot buffer 추가
- 버튼/LED/marker 관련 runtime 상태 추가
===============================================================================
*/

typedef struct
{
    uint32_t frame_start_sample;
} ST_T20_FrameMessage_t;

// Per-stage runtime generated from config snapshot.
typedef struct
{
    ST_T20_PreprocessStageConfig_t stage_cfg;
    float biquad_coeffs[5];
    float biquad_state[2];
} ST_T20_StageRuntime_t;

typedef struct
{
    ST_T20_Config_t cfg;
    uint16_t feature_dim;
    uint16_t vector_len;
} ST_T20_ConfigSnapshot_t;

typedef struct
{
    uint16_t stage_count;
    ST_T20_StageRuntime_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PipelineSnapshot_t;

struct CL_T20_Mfcc::ST_Impl
{
    // ------------------------------------------------------------------------
    // Hardware
    // ------------------------------------------------------------------------
    BMI270   imu;
    SPIClass spi;

    // ------------------------------------------------------------------------
    // RTOS
    // ------------------------------------------------------------------------
    TaskHandle_t  sensor_task_handle;
    TaskHandle_t  process_task_handle;
    QueueHandle_t frame_queue;
    SemaphoreHandle_t mutex;

    // ------------------------------------------------------------------------
    // Runtime State
    // ------------------------------------------------------------------------
    bool initialized;
    bool running;

    ST_T20_Config_t cfg;

    // ------------------------------------------------------------------------
    // Continuous Sample Ring
    // ------------------------------------------------------------------------
    float __attribute__((aligned(16))) sample_ring[G_T20_SAMPLE_RING_SIZE];
    volatile uint32_t sample_write_index;
    volatile uint32_t total_samples_written;
    uint32_t next_frame_start_sample;

    // Queue/drop diagnostics
    volatile uint32_t dropped_frames;

    // ------------------------------------------------------------------------
    // DSP Work Buffers
    // ------------------------------------------------------------------------
    float __attribute__((aligned(16))) frame_time[G_T20_FRAME_SIZE_FIXED];
    float __attribute__((aligned(16))) frame_stage_a[G_T20_FRAME_SIZE_FIXED];
    float __attribute__((aligned(16))) frame_stage_b[G_T20_FRAME_SIZE_FIXED];
    float __attribute__((aligned(16))) window[G_T20_FRAME_SIZE_FIXED];
    float __attribute__((aligned(16))) fft_buffer[G_T20_FRAME_SIZE_FIXED * 2];
    float __attribute__((aligned(16))) power[(G_T20_FRAME_SIZE_FIXED / 2) + 1];
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FRAME_SIZE_FIXED / 2) + 1];
    float __attribute__((aligned(16))) log_mel[G_T20_MEL_FILTERS_FIXED];
    float __attribute__((aligned(16))) mel_bank[G_T20_MEL_FILTERS_FIXED][(G_T20_FRAME_SIZE_FIXED / 2) + 1];

    // ------------------------------------------------------------------------
    // Feature History / Outputs
    // ------------------------------------------------------------------------
    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];
    uint16_t mfcc_history_count;

    ST_T20_FeatureVector_t latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    float latest_sequence_flat[G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX];

    bool latest_vector_valid;
    bool latest_sequence_valid;

    // ------------------------------------------------------------------------
    // Pipeline / Preprocess Runtime
    // ------------------------------------------------------------------------
    float prev_raw_sample;
    uint16_t noise_learned_frames;

    // Cached runtime generated from current config.
    ST_T20_PipelineSnapshot_t pipeline_runtime;

    ST_Impl()
    : spi(FSPI)
    {
        sensor_task_handle = nullptr;
        process_task_handle = nullptr;
        frame_queue = nullptr;
        mutex = nullptr;

        initialized = false;
        running = false;
        cfg = T20_makeDefaultConfig();

        sample_write_index = 0U;
        total_samples_written = 0U;
        next_frame_start_sample = 0U;
        dropped_frames = 0U;

        mfcc_history_count = 0U;
        latest_vector_valid = false;
        latest_sequence_valid = false;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0U;

        memset(sample_ring, 0, sizeof(sample_ring));
        memset(frame_time, 0, sizeof(frame_time));
        memset(frame_stage_a, 0, sizeof(frame_stage_a));
        memset(frame_stage_b, 0, sizeof(frame_stage_b));
        memset(window, 0, sizeof(window));
        memset(fft_buffer, 0, sizeof(fft_buffer));
        memset(power, 0, sizeof(power));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(log_mel, 0, sizeof(log_mel));
        memset(mel_bank, 0, sizeof(mel_bank));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        memset(latest_sequence_flat, 0, sizeof(latest_sequence_flat));
        memset(&pipeline_runtime, 0, sizeof(pipeline_runtime));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

// ============================================================================
// Core helpers
// ============================================================================

bool  T20_validateConfig(const ST_T20_Config_t* p_cfg);
void  T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void  T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void  T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void  T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
bool  T20_copyFrameFromRing(CL_T20_Mfcc::ST_Impl* p,
                            uint32_t p_frame_start_sample,
                            float* p_out_frame,
                            uint16_t p_frame_size);
void  T20_tryEnqueueReadyFrames(CL_T20_Mfcc::ST_Impl* p);

void  T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p,
                          const float* p_mfcc,
                          uint16_t p_dim);
void  T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                  uint16_t p_dim,
                                  uint16_t p_delta_window,
                                  float* p_delta_out);
void  T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                       uint16_t p_dim,
                                       float* p_delta2_out);
void  T20_buildVector(const float* p_mfcc,
                      const float* p_delta,
                      const float* p_delta2,
                      uint16_t p_dim,
                      float* p_out_vec);

void  T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb,
                  uint16_t p_frames,
                  uint16_t p_feature_dim);
void  T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb,
                  const float* p_feature_vec);
bool  T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
void  T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb,
                           float* p_out_flat);
void  T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);

// ============================================================================
// DSP helpers
// ============================================================================

bool  T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool  T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
bool  T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);
bool  T20_configurePipelineRuntime(CL_T20_Mfcc::ST_Impl* p);

float T20_hzToMel(float p_hz);
float T20_melToHz(float p_mel);

void  T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void  T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p);

bool  T20_buildConfigSnapshot(CL_T20_Mfcc::ST_Impl* p,
                              ST_T20_ConfigSnapshot_t* p_out);
bool  T20_buildPipelineSnapshot(const ST_T20_Config_t* p_cfg,
                                ST_T20_PipelineSnapshot_t* p_out);

bool  T20_makeBiquadCoeffsFromStage(const ST_T20_Config_t* p_cfg,
                                    const ST_T20_PreprocessStageConfig_t* p_stage_cfg,
                                    float* p_coeffs_out);

void  T20_applyDCRemove(float* p_data, uint16_t p_len);
void  T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p,
                           float* p_data,
                           uint16_t p_len,
                           float p_alpha);
void  T20_applyNoiseGate(float* p_data,
                         uint16_t p_len,
                         float p_threshold_abs);
void  T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                            const float* p_coeffs,
                            float* p_state,
                            const float* p_in,
                            float* p_out,
                            uint16_t p_len);
void  T20_applyWindow(CL_T20_Mfcc::ST_Impl* p,
                      float* p_data,
                      uint16_t p_len);

bool  T20_applyStage(CL_T20_Mfcc::ST_Impl* p,
                     ST_T20_StageRuntime_t* p_stage,
                     const float* p_in,
                     float* p_out,
                     uint16_t p_len);
bool  T20_applyPreprocessPipeline(CL_T20_Mfcc::ST_Impl* p,
                                  ST_T20_PipelineSnapshot_t* p_pipe,
                                  const float* p_in,
                                  float* p_out,
                                  uint16_t p_len);

void  T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p,
                               const float* p_time,
                               float* p_power);
void  T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p,
                             const float* p_power);
void  T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p,
                                   float* p_power);
void  T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p,
                             const float* p_power,
                             float* p_log_mel_out);
void  T20_computeDCT2(const float* p_in,
                      float* p_out,
                      uint16_t p_in_len,
                      uint16_t p_out_len);

void  T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                      const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                      ST_T20_PipelineSnapshot_t* p_pipe_snap,
                      const float* p_frame,
                      float* p_mfcc_out);
