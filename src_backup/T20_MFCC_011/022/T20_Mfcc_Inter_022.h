#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_022.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_022.h
버전: v022

[기능 스펙]
- 공개하지 않을 내부 구현 구조체 정의
- Core / DSP / Recorder 분리 컴파일을 위한 공용 내부 선언 제공
- recorder queue/task / binary header 대응
- stage 배열형 전처리 runtime
- sliding window / sample ring runtime
- session state / button runtime
- recorder runtime 골격

[향후 단계 TODO]
- recorder 실제 event/raw/meta task 분리
- multi-config FFT / mel bank cache
- zero-copy / DMA / cache aligned 고도화
===============================================================================
*/

typedef struct
{
    uint32_t frame_start_sample;
} ST_T20_FrameMessage_t;

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

typedef struct
{
    bool     mounted;
    bool     session_open;
    uint32_t session_index;
    char     session_dir[G_T20_RECORDER_MAX_PATH_LEN];
    char     raw_path[G_T20_RECORDER_MAX_PATH_LEN];
    char     cfg_path[G_T20_RECORDER_MAX_PATH_LEN];
    char     meta_path[G_T20_RECORDER_MAX_PATH_LEN];
    char     event_path[G_T20_RECORDER_MAX_PATH_LEN];
    char     feature_path[G_T20_RECORDER_MAX_PATH_LEN];

    File     raw_file;
    File     meta_file;
    File     event_file;
    File     feature_file;

    bool     raw_dirty;
    bool     meta_dirty;
    bool     event_dirty;
    bool     feature_dirty;

    uint32_t queued_record_count;
    uint32_t dropped_record_count;
    uint32_t written_record_count;
    uint32_t last_flush_ms;
    uint32_t last_metadata_ms;

    ST_T20_RawBinaryHeader_t raw_header;
} ST_T20_RecorderRuntime_t;

typedef struct
{
    EM_T20_RecorderRecordType_t record_type;
    uint32_t timestamp_ms;
    union
    {
        struct
        {
            char text[G_T20_RECORDER_EVENT_TEXT_MAX];
        } event_rec;

        struct
        {
            uint16_t sample_count;
            float    samples[G_T20_RECORDER_RAW_MAX_SAMPLES];
        } raw_rec;

        struct
        {
            ST_T20_FeatureVector_t feature;
        } feature_rec;
    } data;
} ST_T20_RecorderQueueItem_t;



/*
===============================================================================
[회전 파일 목록 항목]
-------------------------------------------------------------------------------
rotate된 recorder 파일 세트를 웹에서 조회하기 위한 최근 항목 정보
===============================================================================
*/
typedef struct
{
    char raw_path[192];
    char meta_path[192];
    char event_path[192];
    char feature_path[192];
    uint32_t rotate_id;
    bool valid;
} ST_T20_RotateItem_t;

struct CL_T20_Mfcc::ST_Impl
{
    BMI270   imu;
    SPIClass spi;

    TaskHandle_t      sensor_task_handle;
    TaskHandle_t      process_task_handle;
    TaskHandle_t      recorder_task_handle;
    QueueHandle_t     frame_queue;
    QueueHandle_t     recorder_queue;
    SemaphoreHandle_t mutex;

    bool initialized;
    bool running;

    ST_T20_Config_t cfg;

    // Session runtime
    EM_T20_SessionState_t session_state;
    uint32_t measurement_start_ms;
    uint32_t measurement_stop_ms;

    // Button runtime
    bool     button_last_raw;
    bool     button_stable_state;
    uint32_t button_last_change_ms;
    uint32_t button_press_start_ms;
    bool     button_long_reported;

    // Recorder runtime
    ST_T20_RecorderRuntime_t recorder;


    // AsyncWeb runtime
    bool  web_attached;
    void* web_server_ptr;
    char  web_base_path[G_T20_WEB_BASE_PATH_MAX];

    // Continuous sample stream ring
    float __attribute__((aligned(16))) sample_ring[G_T20_SAMPLE_RING_SIZE];
    volatile uint32_t sample_write_index;
    volatile uint32_t total_samples_written;
    volatile uint32_t next_frame_start_sample;

    // Legacy raw frame buffers
    float __attribute__((aligned(16))) frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];
    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;
    volatile uint32_t dropped_frames;

    // Processing buffers
    float __attribute__((aligned(16))) frame_time[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) frame_stage_a[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) frame_stage_b[G_T20_FFT_SIZE];
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

    ST_T20_RotateItem_t rotate_items[G_T20_RECORDER_MAX_ROTATE_LIST];
    uint16_t rotate_item_count;

    uint32_t web_push_seq;
    uint32_t web_last_live_change_ms;
    uint32_t web_last_status_change_ms;
    uint32_t web_last_force_push_ms;
    uint32_t web_last_status_hash;

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
        recorder_task_handle = nullptr;
        frame_queue = nullptr;
        recorder_queue = nullptr;
        mutex = nullptr;

        initialized = false;
        running = false;
        cfg = T20_makeDefaultConfig();

        session_state = EN_T20_SESSION_IDLE;
        measurement_start_ms = 0;
        measurement_stop_ms = 0;

        button_last_raw = false;
        button_stable_state = false;
        button_last_change_ms = 0;
        button_press_start_ms = 0;
        button_long_reported = false;

        memset(&recorder, 0, sizeof(recorder));

        sample_write_index = 0;
        total_samples_written = 0;
        next_frame_start_sample = 0;

        active_fill_buffer = 0;
        active_sample_index = 0;
        dropped_frames = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;
        latest_vector_valid = false;
        latest_sequence_valid = false;
        rotate_item_count = 0;
        web_push_seq = 0;
        web_last_live_change_ms = 0;
        web_last_status_change_ms = 0;
        web_last_force_push_ms = 0;
        web_last_status_hash = 0;

        memset(sample_ring, 0, sizeof(sample_ring));
        memset(frame_buffer, 0, sizeof(frame_buffer));
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
        memset(rotate_items, 0, sizeof(rotate_items));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

// Session / button helpers
bool T20_measurementStart(CL_T20_Mfcc::ST_Impl* p);
bool T20_measurementStop(CL_T20_Mfcc::ST_Impl* p);
EM_T20_ButtonEvent_t T20_pollButtonEvent(CL_T20_Mfcc::ST_Impl* p);

// Recorder helpers
bool T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p);
void T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p);
bool T20_recorderOpenSession(CL_T20_Mfcc::ST_Impl* p);
void T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p);
bool T20_recorderWriteConfig(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg);
bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_event_name);
bool T20_recorderWriteRawFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len);
bool T20_recorderWriteFeature(CL_T20_Mfcc::ST_Impl* p, const ST_T20_FeatureVector_t* p_feature);

// Core
bool  T20_validateConfig(const ST_T20_Config_t* p_cfg);
void  T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void  T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void  T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void  T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
bool  T20_tryEnqueueReadyFrames(CL_T20_Mfcc::ST_Impl* p);
bool  T20_copyFrameFromRing(CL_T20_Mfcc::ST_Impl* p,
                            uint32_t p_frame_start_sample,
                            float* p_out_frame,
                            uint16_t p_frame_size);

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

bool  T20_buildConfigSnapshot(CL_T20_Mfcc::ST_Impl* p, ST_T20_ConfigSnapshot_t* p_out);
bool  T20_buildPipelineSnapshot(const ST_T20_Config_t* p_cfg, ST_T20_PipelineSnapshot_t* p_out);

float T20_hzToMel(float p_hz);
float T20_melToHz(float p_mel);

void  T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void  T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg);

void  T20_applyDCRemove(float* p_data, uint16_t p_len);
void  T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
void  T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);

bool  T20_makeBiquadCoeffsFromStage(const ST_T20_Config_t* p_cfg,
                                    const ST_T20_PreprocessStageConfig_t* p_stage,
                                    float* p_coeffs_out);

void  T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                            const ST_T20_PreprocessStageConfig_t* p_stage,
                            float* p_coeffs,
                            float* p_state,
                            const float* p_in,
                            float* p_out,
                            uint16_t p_len);

bool  T20_applyStage(CL_T20_Mfcc::ST_Impl* p,
                     const ST_T20_Config_t* p_cfg,
                     ST_T20_StageRuntime_t* p_stage_rt,
                     const float* p_in,
                     float* p_out,
                     uint16_t p_len);

bool  T20_applyPreprocessPipeline(CL_T20_Mfcc::ST_Impl* p,
                                  const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                                  ST_T20_PipelineSnapshot_t* p_pipe_snap,
                                  const float* p_in,
                                  float* p_out,
                                  uint16_t p_len);

void  T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len);
void  T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
void  T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, const float* p_power);
void  T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, float* p_power);
void  T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, const float* p_power, float* p_log_mel_out);
void  T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);

void  T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                      const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                      ST_T20_PipelineSnapshot_t* p_pipe_snap,
                      const float* p_frame,
                      float* p_mfcc_out);


bool T20_recorderEnqueueEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_event_name);
bool T20_recorderEnqueueRawFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len);
bool T20_recorderEnqueueFeature(CL_T20_Mfcc::ST_Impl* p, const ST_T20_FeatureVector_t* p_feature);
bool T20_recorderWriteBinaryHeader(CL_T20_Mfcc::ST_Impl* p);
void T20_recorderTask(void* p_arg);

// Web
bool T20_webAttachStaticFiles(CL_T20_Mfcc::ST_Impl* p, void* p_server);
bool T20_webAttach(CL_T20_Mfcc::ST_Impl* p, void* p_server, const char* p_base_path);
void T20_webDetach(CL_T20_Mfcc::ST_Impl* p);
bool T20_webPushLiveNow(CL_T20_Mfcc::ST_Impl* p);
void T20_webPeriodicPush(CL_T20_Mfcc::ST_Impl* p);
bool T20_applyConfigJsonAdvanced(CL_T20_Mfcc::ST_Impl* p, const char* p_json);
