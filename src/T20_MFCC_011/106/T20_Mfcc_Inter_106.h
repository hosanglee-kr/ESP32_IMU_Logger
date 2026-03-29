/* ============================================================================
[향후 단계 구현 예정 정리 - Inter]
1. ST_Impl 멤버는 기능 추가 시 이 파일을 기준으로 먼저 확정
2. 새 상태값 추가 시 생성자 초기화와 JSON/status 출력 동기화
3. 선언/정의/호출 불일치 방지
============================================================================ */

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

#include "T20_Mfcc_106.h"

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
    ST_T20_SdmmcProfile_t sdmmc_profiles[G_T20_SDMMC_PROFILE_PRESET_COUNT];
    bool                  sdmmc_profile_applied;
    char                  sdmmc_last_apply_reason[64];
    bool                  selection_sync_enabled;
    uint32_t              selection_sync_frame_from;
    uint32_t              selection_sync_frame_to;
    char                  selection_sync_name[G_T20_SELECTION_SYNC_NAME_MAX];
    char                  type_meta_name[G_T20_TYPE_META_NAME_MAX];
    char                  type_meta_kind[G_T20_TYPE_META_KIND_MAX];
    bool                  type_meta_enabled;
    bool                  selection_sync_range_valid;
    uint32_t              selection_sync_effective_from;
    uint32_t              selection_sync_effective_to;
    char                  type_meta_auto_text[G_T20_TYPE_META_AUTO_TEXT_MAX];
    float                 viewer_selection_points[G_T20_VIEWER_SELECTION_POINTS_MAX];
    uint16_t              viewer_selection_points_len;
    float                 viewer_overlay_points[G_T20_VIEWER_SELECTION_POINTS_MAX];
    uint16_t              viewer_overlay_points_len;
    uint16_t              viewer_overlay_accum_count;
    uint16_t              viewer_overlay_subset_count;
    char                  type_preview_schema_kind[32];
    char                  type_preview_detected_delim[8];
    char                  type_preview_parser_name[32];
    char                  type_preview_sample_rows[G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX][64];
    uint16_t              type_preview_sample_row_count;
    char                  type_preview_text_buf[G_T20_TYPE_PREVIEW_TEXT_BUF_MAX];
    char                  type_preview_link_path[128];

    char     recorder_file_path[G_T20_RECORDER_FILE_PATH_MAX];
    char     recorder_last_error[G_T20_RECORDER_LAST_ERROR_MAX];
    bool     recorder_flush_requested;
    bool     recorder_finalize_requested;
    bool     recorder_file_opened;
    uint8_t  recorder_zero_copy_slot_index;
    uint8_t  recorder_zero_copy_stage[G_T20_ZERO_COPY_STAGE_BUFFER_BYTES];
    ST_T20_RecorderVectorMessage_t recorder_batch_vectors[G_T20_RECORDER_BATCH_VECTOR_MAX];
    uint16_t recorder_batch_count;
    uint32_t recorder_batch_last_push_ms;
    char     runtime_cfg_profile_name[G_T20_RUNTIME_CFG_PROFILE_NAME_MAX];
    uint8_t  recorder_dma_slots[G_T20_ZERO_COPY_DMA_SLOT_COUNT][G_T20_ZERO_COPY_DMA_SLOT_BYTES];
    uint16_t recorder_dma_slot_used[G_T20_ZERO_COPY_DMA_SLOT_COUNT];
    uint8_t  recorder_dma_active_slot;
    uint16_t recorder_batch_watermark_low;
    uint16_t recorder_batch_watermark_high;
    uint32_t recorder_batch_idle_flush_ms;

    uint32_t sample_write_index;
    uint32_t web_last_push_ms;
    uint32_t last_frame_process_ms;
    float    runtime_sim_phase;

    uint32_t              viewer_recent_frame_ids[G_T20_VIEWER_RECENT_WAVE_COUNT];
    char                  type_preview_header_guess[128];
    char                  viewer_bundle_mode_name[32];
    uint16_t              recorder_rotate_keep_max;
    bool                  recorder_fallback_active;
    char                  recorder_active_path[128];
    uint32_t              recorder_last_flush_ms;
    uint8_t               live_source_mode;
    bool                  bmi270_live_enabled;
    bool                  bmi270_live_ready;
    uint32_t              live_frame_counter;
    uint32_t              live_last_sample_ms;
    float                 live_frame_temp[G_T20_LIVE_FRAME_TEMP_MAX];
    uint16_t              live_frame_fill;
    bool                  live_frame_ready;
    uint32_t              bmi270_sample_counter;
    bool                  recorder_session_open;
    uint32_t              recorder_session_id;
    char                  recorder_session_name[G_T20_RECORDER_SESSION_NAME_MAX];
    uint32_t              recorder_session_open_ms;
    uint32_t              recorder_session_close_ms;
    uint8_t               bmi270_axis_mode;
    float                 bmi270_last_sample_value;
    uint32_t              bmi270_last_drdy_ms;
    float                 live_sample_queue[G_T20_LIVE_QUEUE_DEPTH];
    uint16_t              live_sample_queue_count;
    char                  bmi270_status_text[G_T20_BMI270_STATUS_TEXT_MAX];
    uint8_t               bmi270_init_retry;
    uint32_t              live_last_heartbeat_ms;
    uint8_t               bmi270_chip_id;
    bool                  bmi270_spi_ok;
    bool                  bmi270_drdy_enabled;
    uint32_t              bmi270_last_poll_ms;
    volatile uint8_t      bmi270_drdy_isr_flag;
    uint8_t               bmi270_last_reg_addr;
    uint8_t               bmi270_last_reg_value;
    uint8_t               bmi270_isr_queue_count;
    bool                  bmi270_actual_spi_path_enabled;
    uint8_t               bmi270_last_burst_len;
    uint8_t               bmi270_last_read_ok;
    uint8_t               bmi270_last_transaction_ok;
    uint8_t               bmi270_last_isr_attach_ok;
    uint8_t               bmi270_spi_bus_ready;
    uint8_t               bmi270_last_txn_reg;
    uint8_t               bmi270_spi_read_phase_ready;
    uint8_t               bmi270_isr_hook_ready;
    uint8_t               bmi270_spi_begin_ok;
    uint8_t               bmi270_isr_attach_state;
    uint8_t               bmi270_actual_reg_read_ready;
    float                 bmi270_last_decoded_sample;
    uint8_t               bmi270_last_axis_decode_ok;
    float                 bmi270_last_axis_values[G_T20_BMI270_BURST_AXIS_COUNT];
    uint8_t               bmi270_read_state;
    uint32_t              bmi270_last_read_ms;
    uint8_t               bmi270_actual_burst_ready;
    uint8_t               recorder_finalize_state;
    uint32_t              recorder_finalize_last_ms;
    uint8_t               bmi270_burst_flow_state;
    bool                  recorder_finalize_saved;
    uint8_t               bmi270_isr_request_state;
    uint8_t               recorder_finalize_result;
    uint8_t               bmi270_spi_start_state;
    uint8_t               bmi270_isr_hook_state;
    uint8_t               recorder_finalize_persist_state;
    uint8_t               bmi270_reg_burst_state;
    uint8_t               recorder_finalize_persist_result;
    uint8_t               bmi270_actual_read_txn_state;
    uint8_t               recorder_finalize_save_state;
    uint8_t               bmi270_spi_exec_state;
    uint8_t               recorder_finalize_exec_state;
    uint16_t              preview_column_hint_count;
    char                  preview_column_hints[8][32];
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
        memset(viewer_recent_frame_ids, 0, sizeof(viewer_recent_frame_ids));
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
        memset(sdmmc_profiles, 0, sizeof(sdmmc_profiles));
        sdmmc_profile_applied = false;
        memset(sdmmc_last_apply_reason, 0, sizeof(sdmmc_last_apply_reason));
        selection_sync_enabled = false;
        selection_sync_frame_from = 0;
        selection_sync_frame_to = 0;
        memset(selection_sync_name, 0, sizeof(selection_sync_name));
        strlcpy(selection_sync_name, "default", sizeof(selection_sync_name));
        memset(type_meta_name, 0, sizeof(type_meta_name));
        strlcpy(type_meta_name, "mfcc_vector", sizeof(type_meta_name));
        memset(type_meta_kind, 0, sizeof(type_meta_kind));
        strlcpy(type_meta_kind, "feature_vector", sizeof(type_meta_kind));
        type_meta_enabled = true;
        selection_sync_range_valid = false;
        selection_sync_effective_from = 0;
        selection_sync_effective_to = 0;
        memset(type_meta_auto_text, 0, sizeof(type_meta_auto_text));
        strlcpy(type_meta_auto_text, "unclassified", sizeof(type_meta_auto_text));
        memset(viewer_selection_points, 0, sizeof(viewer_selection_points));
        viewer_selection_points_len = 0;
        memset(viewer_overlay_points, 0, sizeof(viewer_overlay_points));
        viewer_overlay_points_len = 0;
        viewer_overlay_accum_count = 0;
        viewer_overlay_subset_count = 0;
        memset(type_preview_schema_kind, 0, sizeof(type_preview_schema_kind));
        strlcpy(type_preview_schema_kind, "unknown", sizeof(type_preview_schema_kind));
        memset(type_preview_detected_delim, 0, sizeof(type_preview_detected_delim));
        strlcpy(type_preview_detected_delim, ",", sizeof(type_preview_detected_delim));
        memset(type_preview_parser_name, 0, sizeof(type_preview_parser_name));
        strlcpy(type_preview_parser_name, "basic-preview-parser", sizeof(type_preview_parser_name));
        memset(type_preview_sample_rows, 0, sizeof(type_preview_sample_rows));
        type_preview_sample_row_count = 0;
        memset(type_preview_text_buf, 0, sizeof(type_preview_text_buf));
        memset(viewer_bundle_mode_name, 0, sizeof(viewer_bundle_mode_name));
        strlcpy(viewer_bundle_mode_name, "chart_bundle", sizeof(viewer_bundle_mode_name));
        recorder_rotate_keep_max = G_T20_RECORDER_ROTATE_KEEP_MAX;
        recorder_fallback_active = false;
        memset(recorder_active_path, 0, sizeof(recorder_active_path));
        recorder_last_flush_ms = 0;
        live_source_mode = 0;
        bmi270_live_enabled = false;
        bmi270_live_ready = false;
        live_frame_counter = 0;
        live_last_sample_ms = 0;
        memset(live_frame_temp, 0, sizeof(live_frame_temp));
        live_frame_fill = 0;
        live_frame_ready = false;
        bmi270_sample_counter = 0;
        recorder_session_open = false;
        recorder_session_id = 0;
        memset(recorder_session_name, 0, sizeof(recorder_session_name));
        strlcpy(recorder_session_name, "default-session", sizeof(recorder_session_name));
        recorder_session_open_ms = 0;
        recorder_session_close_ms = 0;
        bmi270_axis_mode = G_T20_BMI270_AXIS_MODE_GYRO_Z;
        bmi270_last_sample_value = 0.0f;
        bmi270_last_drdy_ms = 0;
        memset(live_sample_queue, 0, sizeof(live_sample_queue));
        live_sample_queue_count = 0;
        memset(bmi270_status_text, 0, sizeof(bmi270_status_text));
        strlcpy(bmi270_status_text, "idle", sizeof(bmi270_status_text));
        bmi270_init_retry = 0;
        live_last_heartbeat_ms = 0;
        bmi270_chip_id = 0;
        bmi270_spi_ok = false;
        bmi270_drdy_enabled = false;
        bmi270_last_poll_ms = 0;
        bmi270_drdy_isr_flag = 0;
        bmi270_last_reg_addr = 0;
        bmi270_last_reg_value = 0;
        bmi270_isr_queue_count = 0;
        bmi270_actual_spi_path_enabled = false;
        bmi270_last_burst_len = 0;
        bmi270_last_read_ok = 0;
        bmi270_last_transaction_ok = 0;
        bmi270_last_isr_attach_ok = 0;
        bmi270_spi_bus_ready = G_T20_BMI270_SPI_BUS_NOT_READY;
        bmi270_last_txn_reg = 0;
        bmi270_spi_read_phase_ready = G_T20_BMI270_SPI_READ_PHASE_IDLE;
        bmi270_isr_hook_ready = 0;
        bmi270_spi_begin_ok = G_T20_BMI270_SPI_BEGIN_FAIL;
        bmi270_isr_attach_state = 0;
        bmi270_actual_reg_read_ready = G_T20_BMI270_ACTUAL_REG_READ_IDLE;
        bmi270_last_decoded_sample = 0.0f;
        bmi270_last_axis_decode_ok = G_T20_BMI270_AXIS_DECODE_FAIL;
        memset(bmi270_last_axis_values, 0, sizeof(bmi270_last_axis_values));
        bmi270_read_state = G_T20_BMI270_READ_STATE_IDLE;
        bmi270_last_read_ms = 0;
        bmi270_actual_burst_ready = G_T20_BMI270_ACTUAL_BURST_IDLE;
        recorder_finalize_state = G_T20_RECORDER_FINALIZE_STATE_IDLE;
        recorder_finalize_last_ms = 0;
        bmi270_burst_flow_state = G_T20_BMI270_BURST_FLOW_STATE_IDLE;
        recorder_finalize_saved = false;
        bmi270_isr_request_state = G_T20_BMI270_ISR_REQUEST_IDLE;
        recorder_finalize_result = G_T20_RECORDER_FINALIZE_RESULT_NONE;
        bmi270_spi_start_state = G_T20_BMI270_SPI_START_STATE_IDLE;
        bmi270_isr_hook_state = G_T20_BMI270_ISR_HOOK_STATE_IDLE;
        recorder_finalize_persist_state = G_T20_RECORDER_FINALIZE_PERSIST_IDLE;
        bmi270_reg_burst_state = G_T20_BMI270_REG_BURST_STATE_IDLE;
        recorder_finalize_persist_result = G_T20_RECORDER_FINALIZE_PERSIST_RESULT_NONE;
        bmi270_actual_read_txn_state = G_T20_BMI270_ACTUAL_READ_TXN_STATE_IDLE;
        recorder_finalize_save_state = G_T20_RECORDER_FINALIZE_SAVE_STATE_IDLE;
        bmi270_spi_exec_state = G_T20_BMI270_SPI_EXEC_STATE_IDLE;
        recorder_finalize_exec_state = G_T20_RECORDER_FINALIZE_EXEC_IDLE;
        preview_column_hint_count = 0;
        memset(preview_column_hints, 0, sizeof(preview_column_hints));
        memset(type_preview_link_path, 0, sizeof(type_preview_link_path));
        strlcpy(type_preview_link_path, "/api/t20/type_meta", sizeof(type_preview_link_path));
        strlcpy(sdmmc_profile.profile_name, "default-skeleton", sizeof(sdmmc_profile.profile_name));
        sdmmc_profile.enabled = true;
        sdmmc_profile.use_1bit_mode = true;
        memset(recorder_file_path, 0, sizeof(recorder_file_path));
        memset(recorder_last_error, 0, sizeof(recorder_last_error));
        recorder_flush_requested = false;
        recorder_finalize_requested = false;
        recorder_file_opened = false;
        recorder_zero_copy_slot_index = 0;
        memset(recorder_zero_copy_stage, 0, sizeof(recorder_zero_copy_stage));
        memset(recorder_batch_vectors, 0, sizeof(recorder_batch_vectors));
        recorder_batch_count = 0;
        recorder_batch_last_push_ms = 0;
        memset(runtime_cfg_profile_name, 0, sizeof(runtime_cfg_profile_name));
        strlcpy(runtime_cfg_profile_name, "default", sizeof(runtime_cfg_profile_name));
        memset(recorder_dma_slots, 0, sizeof(recorder_dma_slots));
        memset(recorder_dma_slot_used, 0, sizeof(recorder_dma_slot_used));
        recorder_dma_active_slot = 0;
        recorder_batch_watermark_low = G_T20_RECORDER_BATCH_WATERMARK_LOW;
        recorder_batch_watermark_high = G_T20_RECORDER_BATCH_WATERMARK_HIGH;
        recorder_batch_idle_flush_ms = G_T20_RECORDER_BATCH_IDLE_FLUSH_MS;

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
bool T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);
bool T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);
bool T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);
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
void    T20_initSdmmcProfiles(CL_T20_Mfcc::ST_Impl* p);
bool    T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name);
bool    T20_applySdmmcProfilePins(CL_T20_Mfcc::ST_Impl* p);
bool    T20_buildSdmmcProfilesJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
void    T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
bool    T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg);
bool    T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderAppendVector(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
uint8_t T20_getActiveDmaSlotIndex(CL_T20_Mfcc::ST_Impl* p);
bool    T20_rotateDmaSlot(CL_T20_Mfcc::ST_Impl* p);
bool    T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool    T20_commitDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p, uint8_t p_slot_index);
bool    T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool    T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p);
bool    T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p);
File    T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode);

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p);
bool     T20_rotateListDeleteFile(const char* p_path);
void     T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p);
bool     T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len);
void     T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default);
uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max);
void     T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

void T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p);
void T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p);

void T20_updateViewerSelectionProjection(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_buildTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void T20_updateViewerOverlayProjection(CL_T20_Mfcc::ST_Impl* p);

bool T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path);
void T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p);

void T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p);

void T20_updateTypePreviewHeaderGuess(CL_T20_Mfcc::ST_Impl* p);

void T20_updateTypePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
void T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p);

void T20_updatePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildUnifiedViewerBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len);

bool T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p);

bool T20_recorderFallbackToLittleFs(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildRecorderStorageJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len);
bool T20_beginLiveSource(CL_T20_Mfcc::ST_Impl* p);
void T20_stopLiveSource(CL_T20_Mfcc::ST_Impl* p);
bool T20_processLiveSourceTick(CL_T20_Mfcc::ST_Impl* p);
bool T20_buildLiveSourceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_feedLiveSample(CL_T20_Mfcc::ST_Impl* p, float p_value);

bool T20_tryBuildLiveFrame(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildLiveDebugJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p);

bool T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p);

bool T20_recorderOpenSession(CL_T20_Mfcc::ST_Impl* p, const char* p_name);

bool T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p, const char* p_reason);

bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text);

bool T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildRecorderSessionJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);

bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);

bool T20_readBMI270Sample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample);

bool T20_pushLiveQueueSample(CL_T20_Mfcc::ST_Impl* p, float p_sample);

bool T20_drainLiveQueueToFrameBuffer(CL_T20_Mfcc::ST_Impl* p);

bool T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p);

void T20_liveHeartbeat(CL_T20_Mfcc::ST_Impl* p);

bool T20_probeBMI270ChipId(CL_T20_Mfcc::ST_Impl* p);

bool T20_enableBMI270Drdy(CL_T20_Mfcc::ST_Impl* p);

bool T20_pollBMI270Drdy(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270ReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out);

bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample);

void T20_bmi270DrdyIsr(CL_T20_Mfcc::ST_Impl* p);

bool T20_beginActualBmi270SpiPath(CL_T20_Mfcc::ST_Impl* p);

bool T20_tryConsumeBmi270IsrQueue(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270ReadBurstSample(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len);

bool T20_bmi270BeginSpiTransaction(CL_T20_Mfcc::ST_Impl* p);

void T20_bmi270EndSpiTransaction(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270AttachDrdyIsr(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270BeginSpiBus(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270PrepareReadReg(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg);

bool T20_bmi270ExecutePreparedRead(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_out);

bool T20_bmi270InstallDrdyHook(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270ActualSpiBegin(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270ActualAttachIsr(CL_T20_Mfcc::ST_Impl* p);

bool T20_bmi270ActualReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out);

bool T20_bmi270DecodeBurstToSample(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len, float* p_out);

bool T20_bmi270DecodeBurstAxes(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len);

void T20_bmi270SetReadState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool T20_bmi270ActualReadBurst(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len);

void T20_recorderSetFinalizeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool T20_recorderPrepareFinalize(CL_T20_Mfcc::ST_Impl* p);

bool T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void T20_bmi270SetBurstFlowState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool T20_recorderFinalizeSaveSummary(CL_T20_Mfcc::ST_Impl* p);

void T20_bmi270SetIsrRequestState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_recorderSetFinalizeResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void T20_bmi270SetSpiStartState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_bmi270SetIsrHookState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_recorderSetFinalizePersistState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool T20_recorderPreparePersistFinalize(CL_T20_Mfcc::ST_Impl* p);

void T20_bmi270SetRegBurstState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_recorderSetFinalizePersistResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void T20_bmi270SetActualReadTxnState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_recorderSetFinalizeSaveState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_bmi270SetSpiExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void T20_recorderSetFinalizeExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool T20_bmi270PrepareActualSpiExecute(CL_T20_Mfcc::ST_Impl* p);

bool T20_recorderPrepareExecutePersist(CL_T20_Mfcc::ST_Impl* p);
