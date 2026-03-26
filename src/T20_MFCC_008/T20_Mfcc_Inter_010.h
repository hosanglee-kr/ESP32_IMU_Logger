#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_010.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_010.h
버전: v010

[기능 스펙]
- 외부 공개하지 않을 내부 구현 구조체 정의
- Core / DSP 분리 컴파일을 위한 공용 내부 선언 제공

[설계 목적]
1. 공개 헤더에는 최소 API만 노출
2. 실제 구현 상태는 내부 헤더에 모아 관리
3. Core 계층과 DSP 계층이 같은 내부 타입/함수를 공유
4. 상위 사용자 코드는 내부 멤버에 직접 의존하지 않음

[계층 구분]
- Core
  : 생명주기(begin/start/stop), task, queue, 상태 관리, 출력 관리
- DSP
  : 필터, FFT, Mel filterbank, MFCC, delta/delta-delta 계산
===============================================================================
*/

/*
===============================================================================
[프레임 메시지]
-------------------------------------------------------------------------------
Sensor task가 raw frame 하나를 모두 채운 뒤 Process task에 전달할 때 사용하는
queue 메시지 구조체
===============================================================================
*/
typedef struct
{
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

/*
===============================================================================
[프레임 처리용 DSP Snapshot]
-------------------------------------------------------------------------------
process task가 "한 프레임"을 처리하는 동안만 사용하는 작업 집합

[목적]
- cfg snapshot 기반으로 안전한 프레임 처리
- setConfig()와 독립적인 처리 보장
- 필터 / mel bank / working buffer / fft buffer를 프레임 처리 단위로 분리

[구성]
- cfg                 : 해당 프레임 처리 기준 설정 복사본
- filter_coeffs       : snapshot 기준 필터 계수
- filter_state        : snapshot 기준 필터 상태
- temp_frame          : 전처리용 mutable 버퍼
- work_frame          : 필터 출력 / FFT 입력 버퍼
- fft_buffer          : complex FFT 버퍼
- power               : power spectrum 버퍼
- log_mel             : log mel energy 버퍼
- mel_bank            : snapshot 기준 mel filterbank
===============================================================================
*/
typedef struct
{
    ST_T20_Config_t cfg;

    float filter_coeffs[5];
    float filter_state[2];

    float __attribute__((aligned(16))) temp_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) work_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) fft_buffer[G_T20_FFT_SIZE * 2];
    float __attribute__((aligned(16))) power[(G_T20_FFT_SIZE / 2) + 1];
    float __attribute__((aligned(16))) log_mel[G_T20_MEL_FILTERS];
    float __attribute__((aligned(16))) mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];
} ST_T20_DspSnapshot_t;

/*
===============================================================================
[내부 구현체]
-------------------------------------------------------------------------------
CL_T20_Mfcc의 실제 내부 상태를 보관하는 구현체
===============================================================================
*/
struct CL_T20_Mfcc::ST_Impl
{
    BMI270   imu;
    SPIClass spi;

    TaskHandle_t sensor_task_handle;
    TaskHandle_t process_task_handle;
    QueueHandle_t frame_queue;
    SemaphoreHandle_t mutex;

    bool initialized;
    bool running;

    ST_T20_Config_t cfg;

    /*
     * Sensor task가 채우는 raw frame buffer
     */
    float __attribute__((aligned(16))) frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];

    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;
    volatile uint32_t dropped_frames;

    /*
     * 공유 immutable/long-lived DSP state
     */
    float __attribute__((aligned(16))) window[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];
    float biquad_coeffs[5];   // 현재 cfg 기준 필터 계수 cache/debug 용도

    /*
     * Delta / Delta-Delta 계산용 MFCC history
     */
    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];
    uint16_t mfcc_history_count;

    /*
     * 최신 출력
     */
    ST_T20_FeatureVector_t     latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    float latest_sequence_flat[G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX];

    bool latest_vector_valid;
    bool latest_sequence_valid;

    /*
     * 기타 runtime state
     */
    float prev_raw_sample;
    uint16_t noise_learned_frames;

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

        active_fill_buffer = 0;
        active_sample_index = 0;
        dropped_frames = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;
        latest_vector_valid = false;
        latest_sequence_valid = false;

        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(window, 0, sizeof(window));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        memset(latest_sequence_flat, 0, sizeof(latest_sequence_flat));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

// ============================================================================
// [Core 계층 함수]
// ============================================================================

bool T20_validateConfig(const ST_T20_Config_t* p_cfg);

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p,
                         const float* p_mfcc,
                         uint16_t p_dim);

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                 uint16_t p_dim,
                                 uint16_t p_delta_window,
                                 float* p_delta_out);

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                      uint16_t p_dim,
                                      float* p_delta2_out);

void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec);

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb,
                 uint16_t p_frames,
                 uint16_t p_feature_dim);

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);
bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);
void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);

// ============================================================================
// [DSP 계층 함수]
// ============================================================================

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);
bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);

bool T20_prepareDspSnapshot(CL_T20_Mfcc::ST_Impl* p,
                            const ST_T20_Config_t* p_cfg,
                            ST_T20_DspSnapshot_t* p_snap);

bool T20_makeFilterCoeffs(const ST_T20_Config_t* p_cfg, float* p_coeffs_out);

float T20_hzToMel(float p_hz);
float T20_melToHz(float p_mel);

void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void T20_buildMelFilterbankFromConfig(
    const ST_T20_Config_t* p_cfg,
    float p_mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1]);

void T20_applyDCRemove(float* p_data, uint16_t p_len);
void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p,
                          float* p_data,
                          uint16_t p_len,
                          float p_alpha);

void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);

void T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                           float* p_coeffs,
                           float* p_state,
                           const float* p_in,
                           float* p_out,
                           uint16_t p_len);

void T20_applyWindowWithTable(const float* p_window, float* p_data, uint16_t p_len);

void T20_computePowerSpectrumSnapshot(ST_T20_DspSnapshot_t* p_snap,
                                      const float* p_time,
                                      float* p_power);

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p,
                            const ST_T20_Config_t* p_cfg,
                            const float* p_power);

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p,
                                  const ST_T20_Config_t* p_cfg,
                                  float* p_power);

void T20_applyMelFilterbankSnapshot(ST_T20_DspSnapshot_t* p_snap,
                                    const float* p_power,
                                    float* p_log_mel_out);

void T20_computeDCT2(const float* p_in,
                     float* p_out,
                     uint16_t p_in_len,
                     uint16_t p_out_len);

void T20_computeMFCC_Snapshot(CL_T20_Mfcc::ST_Impl* p,
                              ST_T20_DspSnapshot_t* p_snap,
                              const float* p_frame,
                              float* p_mfcc_out);
                              