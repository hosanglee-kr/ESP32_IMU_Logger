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

typedef struct
{
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

/*
===============================================================================
[Mel Bank Cache Key]
-------------------------------------------------------------------------------
mel filterbank는 fft_size / sample_rate / mel_filters 조합에 의해 결정되므로
이 3개 값이 동일하면 재생성이 불필요하다.
===============================================================================
*/
typedef struct
{
    uint16_t fft_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
} ST_T20_MelBankKey_t;

/*
===============================================================================
[DSP Snapshot]
-------------------------------------------------------------------------------
process task가 한 frame을 처리하는 동안만 사용하는 프레임 단위 작업 세트
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
     * sensor task fast-path에서 사용할 축 선택 캐시
     * - setConfig()/begin()에서 갱신
     */
    volatile uint8_t active_axis;

    /*
     * 설정 변경 추적용 generation
     * - begin()/setConfig()에서 증가
     */
    uint32_t cfg_generation;

    /*
     * Raw frame buffer
     */
    float __attribute__((aligned(16))) frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];

    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;

    /*
     * 진단 카운터
     */
    volatile uint32_t dropped_frames_total;
    volatile uint32_t dropped_frames_oldest;
    volatile uint32_t dropped_frames_latest;
    volatile uint32_t irq_notify_count;
    volatile uint32_t sensor_frames_completed;
    volatile uint32_t process_frames_completed;

    /*
     * Shared DSP tables / states
     */
    float __attribute__((aligned(16))) window[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];
    float biquad_coeffs[5];

    /*
     * mel cache
     */
    ST_T20_MelBankKey_t mel_cache_key;
    bool mel_cache_valid;
    float __attribute__((aligned(16))) mel_bank_cache[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

    /*
     * History
     */
    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];
    uint16_t mfcc_history_count;

    /*
     * Latest output
     */
    ST_T20_FeatureVector_t     latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    float latest_sequence_flat[G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX];

    bool latest_vector_valid;
    bool latest_sequence_valid;

    /*
     * Runtime states
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
        active_axis = (uint8_t)cfg.preprocess.axis;
        cfg_generation = 0U;

        active_fill_buffer = 0U;
        active_sample_index = 0U;

        dropped_frames_total = 0U;
        dropped_frames_oldest = 0U;
        dropped_frames_latest = 0U;
        irq_notify_count = 0U;
        sensor_frames_completed = 0U;
        process_frames_completed = 0U;

        mfcc_history_count = 0U;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0U;
        latest_vector_valid = false;
        latest_sequence_valid = false;

        mel_cache_key.fft_size = 0U;
        mel_cache_key.sample_rate_hz = 0.0f;
        mel_cache_key.mel_filters = 0U;
        mel_cache_valid = false;

        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(window, 0, sizeof(window));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(mel_bank_cache, 0, sizeof(mel_bank_cache));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        memset(latest_sequence_flat, 0, sizeof(latest_sequence_flat));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

// ============================================================================
// [Core]
// ============================================================================

bool T20_validateConfig(const ST_T20_Config_t* p_cfg);

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
float T20_selectAxisSampleFromAxis(CL_T20_Mfcc::ST_Impl* p, uint8_t p_axis);

bool T20_shouldResetHistoryOnConfigChange(const ST_T20_Config_t* p_old_cfg,
                                          const ST_T20_Config_t* p_new_cfg);

bool T20_shouldResetNoiseOnConfigChange(const ST_T20_Config_t* p_old_cfg,
                                        const ST_T20_Config_t* p_new_cfg);

bool T20_shouldResetSequenceOnConfigChange(const ST_T20_Config_t* p_old_cfg,
                                           const ST_T20_Config_t* p_new_cfg);

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
// [DSP]
// ============================================================================

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);
bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);

bool T20_prepareDspSnapshot(CL_T20_Mfcc::ST_Impl* p,
                            const ST_T20_Config_t* p_cfg,
                            ST_T20_DspSnapshot_t* p_snap);

bool T20_makeFilterCoeffs(const ST_T20_Config_t* p_cfg, float* p_coeffs_out);

bool T20_isSameMelBankKey(const ST_T20_MelBankKey_t* p_a,
                          const ST_T20_MelBankKey_t* p_b);

void T20_makeMelBankKey(const ST_T20_Config_t* p_cfg, ST_T20_MelBankKey_t* p_key_out);

bool T20_ensureMelBankCache(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg);

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
                              