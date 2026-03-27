#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <string.h>
#include <ArduinoJson.h>
#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
#endif

#include "SparkFun_BMI270_Arduino_Library.h"
#include "FS.h"
#include "SD_MMC.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_052.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_052.h
버전: v052

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
    uint32_t recovery_retry_count;
    uint32_t recovery_fail_count;
    uint32_t last_flush_ms;
    uint32_t last_metadata_ms;
    uint32_t last_recovery_ms;
    char     last_error_text[96];

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


/*
===============================================================================
[DSP 캐시 항목]
-------------------------------------------------------------------------------
향후 multi-config FFT / mel cache 확장을 위한 준비 구조체
현재 단계에서는 mel bank 재생성 최소화 수준의 스캐폴드로 사용
===============================================================================
*/
typedef struct
{
    bool     valid;
    uint16_t profile_index;
    uint16_t fft_size;
    uint16_t mel_filters;
    float    sample_rate_hz;
    float    mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];
} ST_T20_DspCacheItem_t;

/*
===============================================================================
[경량 JSON 파서 결과]
-------------------------------------------------------------------------------
전체 JSON 라이브러리 없이 단순 key/value import/export에 사용하는 내부 구조
===============================================================================
*/

typedef struct
{
    bool found;
    uint16_t start;
    uint16_t end;
} ST_T20_JsonSection_t;


typedef struct
{
    bool found;
    uint16_t count;
    char items[G_T20_JSON_ARRAY_BUF_SIZE];
} ST_T20_JsonArray_t;

typedef struct
{
    bool     valid;
    uint16_t fft_size;
} ST_T20_TwiddleCacheItem_t;

typedef struct
{
    char path[96];
    char summary_path[96];
    char meta_path[96];
    uint32_t size_bytes;
    uint32_t frame_count;
    uint32_t crc32;
    uint32_t created_ms;
    bool valid;
} ST_T20_RecorderIndexItem_t;

typedef struct
{
    uint32_t frame_count;
    uint32_t file_size;
    uint32_t payload_crc32;
    uint32_t sample_rate_hz_q16;
    uint16_t fft_size;
    uint16_t mfcc_coeffs;
    uint16_t mel_filters;
    uint16_t feature_dim;
} ST_T20_RecorderSummary_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz_q16;
    uint16_t fft_size;
    uint16_t mfcc_coeffs;
    uint16_t mel_filters;
    uint16_t feature_dim;
    uint32_t payload_crc32;
} ST_T20_BinaryHeader_t;


typedef struct
{
    bool valid;
    char name[G_T20_CFG_PROFILE_NAME_LEN];
    char path[48];
} ST_T20_ProfileInfo_t;

typedef struct
{
    bool     found;
    float    float_value;
    int32_t  int_value;
    bool     bool_value;
    char     string_value[64];
} ST_T20_JsonValue_t;

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
    float __attribute__((aligned(16))) frame_accum_buffer[G_T20_FRAME_ACCUM_CAPACITY];
    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;
    volatile uint16_t frame_accum_count;
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

    ST_T20_DspCacheItem_t dsp_cache[G_T20_DSP_CACHE_MAX_ITEMS];
    ST_T20_TwiddleCacheItem_t twiddle_cache[G_T20_DSP_TWIDDLE_CACHE_MAX_ITEMS];
    uint8_t current_profile_index;
    ST_T20_ProfileInfo_t profiles[G_T20_CFG_PROFILE_COUNT];

    /* =========================================================================
     * [구버전/뷰어 호환 필드]
     * ========================================================================= */
    bool     measurement_active;
    float    latest_wave_frame[G_T20_FFT_SIZE];

    ST_T20_RotateItem_t recorder_index_items[G_T20_RECORDER_MAX_ROTATE_LIST];
    uint16_t recorder_index_count;

    float    viewer_last_vector[G_T20_FEATURE_DIM_MAX];
    uint16_t viewer_last_vector_len;
    uint32_t viewer_last_frame_id;

    float    viewer_last_log_mel[G_T20_MEL_FILTERS];
    uint16_t viewer_last_log_mel_len;

    float    viewer_last_mfcc[G_T20_MFCC_COEFFS_MAX];
    uint16_t viewer_last_mfcc_len;

    float    viewer_last_waveform[G_T20_FFT_SIZE];
    uint16_t viewer_last_waveform_len;

    float    viewer_last_spectrum[(G_T20_FFT_SIZE / 2) + 1];
    uint16_t viewer_last_spectrum_len;

    float    viewer_recent_waveforms[G_T20_VIEWER_RECENT_WAVE_COUNT][G_T20_FFT_SIZE];
    uint16_t viewer_recent_waveform_count;
    uint16_t viewer_recent_waveform_head;
    uint16_t viewer_effective_hop_size;

    char     viewer_events[G_T20_VIEWER_EVENT_MAX][G_T20_RECORDER_EVENT_TEXT_MAX];
    uint16_t viewer_event_count;

    ST_T20_SdmmcProfile_t sdmmc_profile;
    uint8_t __attribute__((aligned(G_T20_RECORDER_DMA_ALIGN_BYTES))) recorder_zero_copy_stage[G_T20_ZERO_COPY_STAGE_BUFFER_BYTES];



    bool latest_vector_valid;
    bool latest_sequence_valid;

    float biquad_coeffs[5];
    float biquad_state[2];

    float prev_raw_sample;
    uint16_t noise_learned_frames;

    /* recorder 상태기계 / 메타데이터 */
    EM_T20_RecorderState_t recorder_state;
    uint32_t recorder_record_count;
    uint32_t recorder_last_timestamp_ms;
    uint32_t recorder_batch_pending;
    uint32_t recorder_flush_count;
    bool     recorder_flush_requested;
    bool     recorder_finalize_requested;
    EM_T20_StorageBackend_t recorder_storage_backend;
    char     recorder_file_path[G_T20_RECORDER_FILE_PATH_MAX];
    ST_T20_RecorderVectorMessage_t __attribute__((aligned(G_T20_RECORDER_DMA_ALIGN_BYTES))) recorder_batch_msgs[G_T20_RECORDER_BATCH_BUFFER_MAX];
    bool     recorder_sdmmc_mounted;
    char     recorder_sdmmc_mount_path[32];
    char     recorder_sdmmc_board_hint[G_T20_SDMMC_BOARD_HINT_MAX];
    uint8_t  recorder_zero_copy_slot_index;

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
        frame_accum_count = 0;
        dropped_frames = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;
        recorder_state = EN_T20_RECORDER_IDLE;
        recorder_record_count = 0;
        recorder_last_timestamp_ms = 0;
        recorder_batch_pending = 0;
        recorder_flush_count = 0;
        recorder_flush_requested = false;
        recorder_finalize_requested = false;
        recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
        recorder_sdmmc_mounted = false;
        memset(recorder_sdmmc_mount_path, 0, sizeof(recorder_sdmmc_mount_path));
        strlcpy(recorder_sdmmc_mount_path, G_T20_SDMMC_MOUNT_PATH_DEFAULT, sizeof(recorder_sdmmc_mount_path));
        memset(recorder_sdmmc_board_hint, 0, sizeof(recorder_sdmmc_board_hint));
        recorder_zero_copy_slot_index = 0;
        memset(&sdmmc_profile, 0, sizeof(sdmmc_profile));
        strlcpy(sdmmc_profile.profile_name, "default-skeleton", sizeof(sdmmc_profile.profile_name));
        sdmmc_profile.enabled = true;
        sdmmc_profile.use_1bit_mode = true;
        memset(recorder_file_path, 0, sizeof(recorder_file_path));
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
        memset(frame_accum_buffer, 0, sizeof(frame_accum_buffer));
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
        memset(dsp_cache, 0, sizeof(dsp_cache));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));
    }
};

class AsyncWebServerRequest;

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
void  T20_pushViewerEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_kind, const char* p_text);
void  T20_pushViewerWaveformHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_waveform, uint16_t p_len);
bool  T20_pushSlidingSample(CL_T20_Mfcc::ST_Impl* p, float p_sample, uint8_t* p_frame_index_out);
bool  T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out);

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


/* ============================================================================
 * Recorder helper
 * ========================================================================== */

/*
 * binary header write helper
 * - binary recorder 시작 시 파일 맨 앞에 기본 header를 기록
 */
bool  T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg);

/*
 * binary header rewrite helper
 * - 녹화 종료 시 record_count 등 최종 메타데이터를 다시 덮어쓴다
 */
bool  T20_rewriteRecorderBinaryHeader(File& p_file, const ST_T20_RecorderBinaryHeader_t* p_hdr);


/* ============================================================================
 * Recorder lifecycle helper
 * ========================================================================== */

/*
 * binary recorder 세션 열기
 * - 파일 open + header write + 상태 전이
 * - 현재 단계에서는 helper 수준으로 제공
 */
bool  T20_tryOpenRecorderBinarySession(CL_T20_Mfcc::ST_Impl* p, const char* p_path);

/*
 * binary recorder 세션 종료
 * - header rewrite + flush/close 방향의 helper
 * - 현재 단계에서는 파일 핸들 외부 관리 TODO
 */
bool  T20_tryFinalizeRecorderBinarySession(CL_T20_Mfcc::ST_Impl* p, File& p_file);

/*
 * payload 레코드 1개 write helper
 * - prefix + float payload 고정 포맷
 */
bool  T20_writeRecorderBinaryVectorRecord(File& p_file,
                                          uint32_t p_timestamp_ms,
                                          const float* p_values,
                                          uint16_t p_value_count);

/*
 * fixed payload 크기 계산 helper
 */
uint16_t T20_getRecorderBinaryVectorRecordBytes(uint16_t p_value_count);


/* ============================================================================
 * Recorder batching / CSV server filter helper
 * ========================================================================== */

/*
 * 누적 batch를 파일에 flush
 * - 현재 단계에서는 vector 1건씩 append하는 단순 구현 골격
 * - 추후 queue/task 기반 비동기 flush로 확장 예정
 */
bool T20_flushRecorderVectorBatch(CL_T20_Mfcc::ST_Impl* p,
                                  const float* p_vector,
                                  uint16_t p_vector_len,
                                  uint32_t p_timestamp_ms);

/*
 * CSV table 서버측 필터/정렬/페이지네이션 JSON 생성
 */
bool T20_buildRecorderCsvTableFilteredJsonText(CL_T20_Mfcc::ST_Impl* p,
                                               char* p_out_buf,
                                               uint16_t p_len,
                                               const char* p_path,
                                               uint32_t p_bytes,
                                               const char* p_global_filter,
                                               uint16_t p_sort_col,
                                               uint16_t p_page,
                                               uint16_t p_page_size);


/* ============================================================================
 * Recorder queue / task helper
 * ========================================================================== */

/*
 * recorder queue에 vector 1건 적재
 */
bool T20_enqueueRecorderVector(CL_T20_Mfcc::ST_Impl* p,
                               const float* p_vector,
                               uint16_t p_vector_len,
                               uint32_t p_timestamp_ms);

/*
 * recorder queue 생성/삭제 helper
 */
bool T20_createRecorderObjects(CL_T20_Mfcc::ST_Impl* p);
void T20_releaseRecorderObjects(CL_T20_Mfcc::ST_Impl* p);

/*
 * recorder task 본체
 * - queue에서 vector message를 읽어 batch flush 수행
 */


/* ============================================================================
 * Recorder backend / batch helper
 * ========================================================================== */

/*
 * 저장 백엔드에 따라 파일 open
 * - 현재 단계에서는 LittleFS 우선 구현
 * - SD_MMC는 TODO 골격
 */
File T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend,
                                   const char* p_path,
                                   const char* p_mode);

/*
 * batch message 묶음을 실제 파일로 flush
 */
bool T20_flushRecorderBatchMessages(CL_T20_Mfcc::ST_Impl* p,
                                    const ST_T20_RecorderVectorMessage_t* p_msgs,
                                    uint16_t p_count);

/*
 * recorder queue drain helper
 * - finalize 전에 남은 메시지를 최대한 비워 쓴다
 */
uint16_t T20_drainRecorderQueue(CL_T20_Mfcc::ST_Impl* p,
                                ST_T20_RecorderVectorMessage_t* p_msgs,
                                uint16_t p_capacity);

/*
 * CSV 서버측 컬럼 필터 JSON 생성
 */
bool T20_buildRecorderCsvTableColumnFilteredJsonText(CL_T20_Mfcc::ST_Impl* p,
                                                     char* p_out_buf,
                                                     uint16_t p_len,
                                                     const char* p_path,
                                                     uint32_t p_bytes,
                                                     const char* p_global_filter,
                                                     const char* p_col_filters_csv,
                                                     uint16_t p_sort_col,
                                                     uint16_t p_page,
                                                     uint16_t p_page_size);


/* ============================================================================
 * CSV typed sort / chart sync helper
 * ========================================================================== */

/*
 * 문자열을 숫자형으로 파싱 가능한지 확인
 * - int/float 공통 정렬 보조용
 */
bool T20_parseSortableNumber(const String& p_text, double* p_out_value);

/*
 * 문자열이 날짜/일시 형식인지 간단 판별
 */
bool T20_parseSortableDateTimeKey(const String& p_text, uint64_t* p_out_key);

/*
 * 고급 CSV JSON 빌더
 * - 컬럼 필터 + asc/desc + 숫자/date 타입 우선 정렬
 */
bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p,
                                               char* p_out_buf,
                                               uint16_t p_len,
                                               const char* p_path,
                                               uint32_t p_bytes,
                                               const char* p_global_filter,
                                               const char* p_col_filters_csv,
                                               uint16_t p_sort_col,
                                               uint16_t p_sort_dir,
                                               uint16_t p_page,
                                               uint16_t p_page_size);


/* ============================================================================
 * CSV type meta cache skeleton
 * ========================================================================== */

/*
 * CSV 컬럼 타입 메타 추론 JSON 생성
 * - preview 범위 내에서 숫자/date/text 후보를 추론
 * - 이후 단계에서 정렬/필터 최적화 캐시로 확장 예정
 */
bool T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p,
                                          char* p_out_buf,
                                          uint16_t p_len,
                                          const char* p_path,
                                          uint32_t p_bytes);


/* ============================================================================
 * SD_MMC backend / chart sync helper
 * ========================================================================== */

/*
 * SD_MMC mount 시도
 * - 성공 시 recorder_sdmmc_mounted=true
 * - 실패 시 false 반환
 */
bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);

/*
 * SD_MMC unmount helper
 */
void T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);


/* ============================================================================
 * Zero-copy / selection sync hook skeleton
 * ========================================================================== */

/*
 * recorder write용 버퍼 슬롯 선택
 * - 현재 단계는 hook skeleton
 * - 이후 DMA/cache aligned zero-copy write 경로로 확장 예정
 */
uint8_t T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p);

/*
 * render/selection sync 상태 JSON 생성
 * - 웹 UI에서 멀티 canvas 상태와 연결하기 위한 skeleton
 */
bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p,
                                          char* p_out_buf,
                                          uint16_t p_len);


/* ============================================================================
 * [컴파일 보강용 선언]
 * ----------------------------------------------------------------------------
 * - 서로 다른 리비전 cpp가 참조하는 helper를 공용 선언으로 정리
 * ========================================================================== */
bool T20_initProfiles(CL_T20_Mfcc::ST_Impl* p);
bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out);
bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out);
bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p);

bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json);
bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);

bool T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len);

bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerSequenceOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force);

bool T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value);

bool T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode);
bool T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type);

bool T20_parseHttpRangeHeader(const String& p_range,
                              uint32_t p_file_size,
                              uint32_t* p_offset_out,
                              uint32_t* p_length_out);

bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p,
                                          char* p_out_buf,
                                          uint16_t p_len);
bool T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p,
                                          char* p_out_buf,
                                          uint16_t p_len);

uint8_t T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p);
