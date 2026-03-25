#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_009.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_009.h
버전: v009

[기능 스펙]
- 외부 공개하지 않을 내부 구현 구조체 정의
- Core / DSP 분리 컴파일을 위한 공용 내부 선언 제공
===============================================================================
*/

typedef struct
{
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

struct CL_T20_Mfcc::ST_Impl
{
    BMI270   imu;
    SPIClass spi;

    TaskHandle_t      sensor_task_handle;
    TaskHandle_t      process_task_handle;
    QueueHandle_t     frame_queue;
    SemaphoreHandle_t mutex;

    bool initialized;
    bool running;

    ST_T20_Config_t cfg;

    float __attribute__((aligned(16))) frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];
    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;
    volatile uint32_t dropped_frames;

    float __attribute__((aligned(16))) work_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) temp_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) window[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) fft_buffer[G_T20_FFT_SIZE * 2];
    float __attribute__((aligned(16))) power[(G_T20_FFT_SIZE / 2) + 1];
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];
    float __attribute__((aligned(16))) log_mel[G_T20_MEL_FILTERS];
    float __attribute__((aligned(16))) mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];
    uint16_t mfcc_history_count;

    ST_T20_FeatureVector_t     latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    float latest_sequence_flat[G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX];

    bool latest_vector_valid;
    bool latest_sequence_valid;

    float biquad_coeffs[5];
    float biquad_state[2];

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
        memset(work_frame, 0, sizeof(work_frame));
        memset(temp_frame, 0, sizeof(temp_frame));
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
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

// Core
bool  T20_validateConfig(const ST_T20_Config_t* p_cfg);
void  T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void  T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void  T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void  T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);

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
                  
void  T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);
bool  T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
void  T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);
void  T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);

// DSP
bool  T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool  T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
bool  T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);
bool  T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);

float T20_hzToMel(float p_hz);
float T20_melToHz(float p_mel);

void  T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void  T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p);

void  T20_applyDCRemove(float* p_data, uint16_t p_len);
void  T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
void  T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
void  T20_applyBiquadFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len);
void  T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len);

void  T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
void  T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);
void  T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);
void  T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);
void  T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);

void  T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                      const ST_T20_Config_t* p_cfg,
                      const float* p_frame,
                      float* p_mfcc_out);
