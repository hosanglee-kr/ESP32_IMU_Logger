#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <memory>
#include <vector>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_058.h"

struct CL_T20_Mfcc::ST_Impl
{
    SPIClass          spi;
    TaskHandle_t      sensor_task_handle;
    TaskHandle_t      process_task_handle;
    TaskHandle_t      recorder_task_handle;
    QueueHandle_t     frame_queue;
    QueueHandle_t     recorder_queue;
    SemaphoreHandle_t mutex;

    bool initialized;
    bool running;
    bool measurement_active;
    bool recorder_enabled;

    ST_T20_Config_t cfg;
    ST_T20_ProfileInfo_t profiles[G_T20_CFG_PROFILE_COUNT];
    uint8_t current_profile_index;

    float frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];
    float work_frame[G_T20_FFT_SIZE];
    float temp_frame[G_T20_FFT_SIZE];
    float window[G_T20_FFT_SIZE];
    float power[(G_T20_FFT_SIZE / 2U) + 1U];
    float noise_spectrum[(G_T20_FFT_SIZE / 2U) + 1U];
    float log_mel[G_T20_MEL_FILTERS];
    float mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2U) + 1U];
    float mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];
    float biquad_coeffs[5];
    float biquad_state[2];

    uint8_t  active_fill_buffer;
    uint16_t active_sample_index;
    uint32_t dropped_frames;
    uint16_t mfcc_history_count;
    float    prev_raw_sample;
    uint16_t noise_learned_frames;

    ST_T20_FeatureVector_t     latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    bool latest_vector_valid;
    bool latest_sequence_valid;

    float latest_wave_frame[G_T20_FFT_SIZE];
    float viewer_last_vector[G_T20_FEATURE_DIM_MAX];
    uint16_t viewer_last_vector_len;
    uint32_t viewer_last_frame_id;

    float viewer_last_log_mel[G_T20_MEL_FILTERS];
    uint16_t viewer_last_log_mel_len;
    float viewer_last_mfcc[G_T20_MFCC_COEFFS_MAX];
    uint16_t viewer_last_mfcc_len;
    float viewer_last_waveform[G_T20_FFT_SIZE];
    uint16_t viewer_last_waveform_len;
    float viewer_last_spectrum[(G_T20_FFT_SIZE / 2U) + 1U];
    uint16_t viewer_last_spectrum_len;

    float viewer_recent_waveforms[G_T20_VIEWER_RECENT_WAVE_COUNT][G_T20_FFT_SIZE];
    uint16_t viewer_recent_waveform_count;
    uint16_t viewer_recent_waveform_head;
    uint16_t viewer_effective_hop_size;

    ST_T20_ViewerEvent_t viewer_events[G_T20_VIEWER_EVENT_MAX];
    uint16_t             viewer_event_count;

    ST_T20_RecorderIndexItem_t recorder_index_items[G_T20_RECORDER_MAX_ROTATE_LIST];
    uint16_t                   recorder_index_count;
    uint32_t                   recorder_record_count;

    EM_T20_StorageBackend_t recorder_storage_backend;
    bool     recorder_sdmmc_mounted;
    char     recorder_sdmmc_mount_path[32];
    char     recorder_sdmmc_board_hint[G_T20_SDMMC_BOARD_HINT_MAX];
    ST_T20_SdmmcProfile_t sdmmc_profile;

    char     recorder_file_path[G_T20_RECORDER_FILE_PATH_MAX];
    char     recorder_last_error[G_T20_RECORDER_LAST_ERROR_MAX];
    bool     recorder_flush_requested;
    bool     recorder_finalize_requested;
    uint8_t  recorder_zero_copy_slot_index;
    uint8_t  recorder_zero_copy_stage[G_T20_ZERO_COPY_STAGE_BUFFER_BYTES];

    uint32_t sample_write_index;
    uint32_t web_last_push_ms;
    uint32_t last_frame_process_ms;
    float    runtime_sim_phase;

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
        measurement_active = false;
        recorder_enabled = false;

        cfg = T20_makeDefaultConfig();
        memset(profiles, 0, sizeof(profiles));
        current_profile_index = 0;

        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(work_frame, 0, sizeof(work_frame));
        memset(temp_frame, 0, sizeof(temp_frame));
        memset(window, 0, sizeof(window));
        memset(power, 0, sizeof(power));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(log_mel, 0, sizeof(log_mel));
        memset(mel_bank, 0, sizeof(mel_bank));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));

        active_fill_buffer = 0;
        active_sample_index = 0;
        dropped_frames = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;

        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        latest_vector_valid = false;
        latest_sequence_valid = false;

        memset(latest_wave_frame, 0, sizeof(latest_wave_frame));
        memset(viewer_last_vector, 0, sizeof(viewer_last_vector));
        viewer_last_vector_len = 0;
        viewer_last_frame_id = 0;
        memset(viewer_last_log_mel, 0, sizeof(viewer_last_log_mel));
        viewer_last_log_mel_len = 0;
        memset(viewer_last_mfcc, 0, sizeof(viewer_last_mfcc));
        viewer_last_mfcc_len = 0;
        memset(viewer_last_waveform, 0, sizeof(viewer_last_waveform));
        viewer_last_waveform_len = 0;
        memset(viewer_last_spectrum, 0, sizeof(viewer_last_spectrum));
        viewer_last_spectrum_len = 0;
        memset(viewer_recent_waveforms, 0, sizeof(viewer_recent_waveforms));
        viewer_recent_waveform_count = 0;
        viewer_recent_waveform_head = 0;
        viewer_effective_hop_size = cfg.feature.hop_size;
        memset(viewer_events, 0, sizeof(viewer_events));
        viewer_event_count = 0;

        memset(recorder_index_items, 0, sizeof(recorder_index_items));
        recorder_index_count = 0;
        recorder_record_count = 0;
        recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
        recorder_sdmmc_mounted = false;
        memset(recorder_sdmmc_mount_path, 0, sizeof(recorder_sdmmc_mount_path));
        strlcpy(recorder_sdmmc_mount_path, G_T20_SDMMC_MOUNT_PATH_DEFAULT, sizeof(recorder_sdmmc_mount_path));
        memset(recorder_sdmmc_board_hint, 0, sizeof(recorder_sdmmc_board_hint));
        memset(&sdmmc_profile, 0, sizeof(sdmmc_profile));
        strlcpy(sdmmc_profile.profile_name, "default-skeleton", sizeof(sdmmc_profile.profile_name));
        sdmmc_profile.enabled = true;
        sdmmc_profile.use_1bit_mode = true;
        memset(recorder_file_path, 0, sizeof(recorder_file_path));
        memset(recorder_last_error, 0, sizeof(recorder_last_error));
        recorder_flush_requested = false;
        recorder_finalize_requested = false;
        recorder_zero_copy_slot_index = 0;
        memset(recorder_zero_copy_stage, 0, sizeof(recorder_zero_copy_stage));

        sample_write_index = 0;
        web_last_push_ms = 0;
        last_frame_process_ms = 0;
        runtime_sim_phase = 0.0f;
    }
};

extern CL_T20_Mfcc* g_t20_instance;

bool T20_validateConfig(const ST_T20_Config_t* p_cfg);
void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p);
void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
void  T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim);
void  T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out);
void  T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out);
void  T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec);

void  T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim);
void  T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);
bool  T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
void  T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);
void  T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);
void  T20_fillSyntheticFrame(CL_T20_Mfcc::ST_Impl* p, float* p_out_frame, uint16_t p_len);
bool  T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len);

bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out);
bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out);
bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p);
bool T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg);
bool T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg);
bool T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out);

bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json);
bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);

bool T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len);

bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildViewerChartBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, uint16_t p_points);

bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildRecorderPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool T20_buildRecorderRangeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length);
bool T20_buildRecorderBinaryHeaderJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path);
bool T20_buildRecorderCsvTableJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes,
                                               const char* p_global_filter, const char* p_col_filters_csv,
                                               uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size);
bool T20_buildRecorderBinaryRecordsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit);
bool T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path);

bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool   T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value);
bool   T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out_value);
bool   T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out_value);
bool   T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out_buf, uint16_t p_len);

bool   T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode);
bool   T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type);
bool   T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out);
bool   T20_isLikelyDateText(const String& p_text);
bool   T20_isLikelyDateTimeText(const String& p_text);
String T20_upgradeCsvTypeGuess(const String& p_current, const String& p_cell);
bool   T20_csvRowMatchesGlobalFilter(const std::vector<String>& p_row, const String& p_filter);

bool  T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool  T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p);
void  T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len);
float T20_hzToMel(float p_hz);
float T20_melToHz(float p_mel);
void  T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void  T20_applyDCRemove(float* p_data, uint16_t p_len);
void  T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
void  T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
void  T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);
void  T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);
void  T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);
void  T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
void  T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);
void  T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out);

uint8_t T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p);
void    T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force);
void    T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text);
bool    T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
void    T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
bool    T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg);
File    T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode);

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p);
bool     T20_rotateListDeleteFile(const char* p_path);
void     T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p);
bool     T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len);
void     T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default);
uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max);
void     T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);
