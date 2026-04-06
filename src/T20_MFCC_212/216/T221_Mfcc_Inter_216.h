/* ============================================================================
 * File: T221_Mfcc_Inter_216.h
 * Summary: CL_T20_Mfcc 내부 구현체 (ST_Impl) Full 버전 (C++17 네임스페이스 및 SIMD 정렬 완벽 적용)
 * ========================================================================== */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <LittleFS.h>
#include "SparkFun_BMI270_Arduino_Library.h"

#include <memory>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "T220_Mfcc_216.h"

/* ----------------------------------------------------------------------------
 * CL_T20_Mfcc::ST_Impl 정의
 * ---------------------------------------------------------------------------- */

// 전방선언
void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);

struct CL_T20_Mfcc::ST_Impl {

    // [1] 시스템 및 RTOS 핸들 (System & RTOS Resources)
    SPIClass            spi;                    
    BMI270              bmi;
    TaskHandle_t        sensor_task_handle;     
    TaskHandle_t        process_task_handle;    
    TaskHandle_t        recorder_task_handle;   
    QueueHandle_t       frame_queue;            
    QueueHandle_t       recorder_queue;         
    SemaphoreHandle_t   mutex;                  

    bool                initialized;            
    bool                running;                
    bool                measurement_active;     

    // [2] 설정 및 프로필 (Configuration & Profiles)
    ST_T20_Config_t     cfg;                    
    ST_T20_ProfileInfo_t profiles[T20::C10_Sys::CFG_PROFILE_COUNT]; 
    uint8_t             current_profile_index;  
    char                runtime_cfg_profile_name[T20::C10_Sys::RUNTIME_CFG_PROFILE_NAME_MAX]; 

    // [3] 센서(BMI270) 상태 머신 (Sensor State Machine)
    ST_T20_BMI270_State_t        bmi_state;     
    ST_T20_BMI270_RuntimeState_t bmi_runtime;   

    uint8_t             live_source_mode;       
    uint8_t             bmi270_chip_id;         
    bool                bmi270_spi_ok;          
    bool                bmi270_drdy_enabled;    
    volatile uint8_t    bmi270_drdy_isr_flag;   
    uint32_t            bmi270_sample_counter;  
    uint32_t            bmi270_last_drdy_ms;    
    uint32_t            bmi270_last_poll_ms;    
    float               bmi270_last_axis_values[T20::C10_BMI::BURST_AXIS_COUNT]; 
    uint8_t             bmi270_axis_mode;       
    char                bmi270_status_text[T20::C10_BMI::STATUS_TEXT_MAX]; 

    // [4] DSP 및 MFCC 연산 버퍼 (DSP Pipeline Buffers) - 16바이트 정렬(SIMD) 전체 적용
    alignas(16) float               frame_buffer[T20::C10_Sys::RAW_FRAME_BUFFERS][T20::C10_DSP::FFT_SIZE]; 
    alignas(16) float               work_frame[T20::C10_DSP::FFT_SIZE];     
    alignas(16) float               temp_frame[T20::C10_DSP::FFT_SIZE];     
    alignas(16) float               window[T20::C10_DSP::FFT_SIZE];         
    alignas(16) float               power[(T20::C10_DSP::FFT_SIZE/2)+1];    
    
    alignas(16) float               noise_spectrum[(T20::C10_DSP::FFT_SIZE/2)+1]; 
    alignas(16) float               log_mel[T20::C10_DSP::MEL_FILTERS];                          
    alignas(16) float               mel_bank[T20::C10_DSP::MEL_FILTERS][(T20::C10_DSP::FFT_SIZE/2)+1];   
    
    // DCT 코사인 매트릭스 캐싱 (SIMD 내적 가속용)
    alignas(16) float               dct_matrix[T20::C10_DSP::MFCC_COEFFS_MAX][T20::C10_DSP::MEL_FILTERS];
    alignas(16) float               mfcc_history[T20::C10_DSP::MFCC_HISTORY][T20::C10_DSP::MFCC_COEFFS_MAX]; 
    
    // ESP-DSP 필터용 계수 및 상태
    alignas(16) float               biquad_coeffs[5];         
    alignas(16) float               biquad_state[2];          

    uint8_t             active_fill_buffer;     
    uint16_t            active_sample_index;    
    uint32_t            dropped_frames;         
    uint16_t            mfcc_history_count;     
    
    uint16_t            noise_learned_frames;   
    bool                noise_learning_active; 
    
    float               prev_raw_sample;        
    float               runtime_sim_phase;      
    uint32_t            last_frame_process_ms; 

    // [5] 특징량 및 시퀀스 결과 (Feature & Sequence Output)
    ST_T20_FeatureVector_t     latest_feature;  
    ST_T20_FeatureRingBuffer_t seq_rb;          
    bool                       latest_vector_valid;   
    bool                       latest_sequence_valid; 

    // [6] 레코더 및 저장 장치 (Recorder & Storage)
    ST_T20_RecorderState_t        rec_state;    
    ST_T20_RecorderRuntimeState_t rec_runtime;  

    EM_T20_StorageBackend_t recorder_storage_backend; 
    bool                recorder_enabled;       
    bool                recorder_sdmmc_mounted; 
    bool                recorder_file_opened;   
    bool                recorder_fallback_active; 
    bool                recorder_flush_requested; 
    char                recorder_active_path[128]; 
    char                recorder_file_path[T20::C10_Rec::FILE_PATH_MAX]; 
    char                recorder_last_error[T20::C10_Rec::LAST_ERROR_MAX]; 
    uint32_t            recorder_record_count;  
    uint32_t            recorder_last_flush_ms; 

    // 배치 및 DMA 설정
    uint16_t            recorder_batch_watermark_low;  
    uint16_t            recorder_batch_watermark_high; 
    uint32_t            recorder_batch_idle_flush_ms;  
    uint32_t            recorder_batch_last_push_ms;   
    uint16_t            recorder_batch_count;          
    ST_T20_RecorderVectorMessage_t recorder_batch_vectors[T20::C10_Rec::BATCH_VECTOR_MAX];

    // Zero-Copy DMA 슬롯
    uint8_t             recorder_dma_slots[T20::C10_Rec::DMA_SLOT_COUNT][T20::C10_Rec::DMA_SLOT_BYTES] __attribute__((aligned(32)));
    uint16_t            recorder_dma_slot_used[T20::C10_Rec::DMA_SLOT_COUNT];
    uint8_t             recorder_dma_active_slot;
    uint8_t             recorder_zero_copy_slot_index;

    // 세션 관리
    bool                recorder_session_open;
    uint32_t            recorder_session_id;
    char                recorder_session_name[T20::C10_Rec::SESSION_NAME_MAX];
    uint32_t            recorder_session_open_ms;
    uint32_t            recorder_session_close_ms;
    
    // 레코더 인덱스 및 로테이션
    uint16_t            recorder_index_count;      
    uint16_t            recorder_rotate_keep_max;  
    struct ST_T20_IndexItem {
        char     path[128];
        uint32_t size_bytes;
        uint32_t created_ms;
        uint32_t record_count;
    } recorder_index_items[T20::C10_Rec::MAX_ROTATE_LIST]; 

    // [7] 뷰어 및 웹 인터페이스 (Viewer & Meta Data)
    uint32_t            viewer_last_frame_id;
    float               viewer_last_waveform[T20::C10_DSP::FFT_SIZE];
    float               viewer_last_spectrum[(T20::C10_DSP::FFT_SIZE/2)+1];
    float               viewer_last_mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    uint16_t            viewer_last_waveform_len;
    uint16_t            viewer_last_spectrum_len;
    uint16_t            viewer_last_mfcc_len;
    uint16_t            viewer_last_vector_len;

    ST_T20_ViewerEvent_t viewer_events[T20::C10_View::EVENT_MAX];
    uint16_t            viewer_event_count;

    // 메타데이터 및 동기화 (Web 연동용)
    char                type_meta_name[T20::C10_View::META_NAME_MAX];
    char                type_meta_kind[T20::C10_View::META_KIND_MAX];
    char                type_meta_auto_text[T20::C10_View::META_AUTO_TEXT_MAX];
    bool                type_meta_enabled;
    bool                selection_sync_enabled;
    uint32_t            selection_sync_frame_from;
    uint32_t            selection_sync_frame_to;
    uint32_t            selection_sync_effective_from;
    uint32_t            selection_sync_effective_to;
    bool                selection_sync_range_valid;
    char                selection_sync_name[T20::C10_View::SYNC_NAME_MAX];

    float               viewer_selection_points[T20::C10_View::SELECTION_POINTS_MAX];
    uint16_t            viewer_selection_points_len;
    float               viewer_overlay_points[T20::C10_View::SELECTION_POINTS_MAX];
    uint16_t            viewer_overlay_points_len;
    uint16_t            viewer_overlay_accum_count;
    uint16_t            viewer_overlay_subset_count;

    // [8] SDMMC 프로필 및 상태 
    struct ST_T20_SdmmcProfile {
        char     profile_name[32];
        bool     enabled;
        bool     use_1bit_mode;
        uint8_t  clk_pin;
        uint8_t  cmd_pin;
        uint8_t  d0_pin;
        uint8_t  d1_pin;
        uint8_t  d2_pin;
        uint8_t  d3_pin;
    } sdmmc_profile, sdmmc_profiles[T20::C10_Rec::SDMMC_PROFILE_COUNT];

    bool     sdmmc_profile_applied;
    char     sdmmc_last_apply_reason[64];
    
    // ------------------------------------------------------------------------
    // 생성자: 자원 할당 및 초기화
    // ------------------------------------------------------------------------
    ST_Impl() : spi(FSPI), bmi() {
        // [1] RTOS 초기화
        sensor_task_handle = process_task_handle = recorder_task_handle = nullptr;
        frame_queue = recorder_queue = nullptr;
        mutex = nullptr;
        initialized = running = measurement_active = false;

        // [2] 설정/프로필 초기화
        cfg = T20_makeDefaultConfig();
        memset(profiles, 0, sizeof(profiles));
        current_profile_index = 0;
        memset(runtime_cfg_profile_name, 0, sizeof(runtime_cfg_profile_name));

        // [3] 센서 상태 초기화
        memset(&bmi_state, 0, sizeof(bmi_state));
        memset(&bmi_runtime, 0, sizeof(bmi_runtime));
        live_source_mode = 0; 
        bmi270_chip_id = 0;
        bmi270_spi_ok = bmi270_drdy_enabled = false;
        bmi270_drdy_isr_flag = 0;
        bmi270_sample_counter = 0;
        bmi270_last_drdy_ms = bmi270_last_poll_ms = 0;
        memset(bmi270_last_axis_values, 0, sizeof(bmi270_last_axis_values));
        bmi270_axis_mode = 0; 
        strlcpy(bmi270_status_text, "idle", sizeof(bmi270_status_text));

        // [4] DSP 버퍼 초기화 
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
        memset(dct_matrix, 0, sizeof(dct_matrix)); 
        
        active_fill_buffer = 0; 
        active_sample_index = 0;
        dropped_frames = 0; 
        mfcc_history_count = 0;
        noise_learned_frames = 0; 
        prev_raw_sample = 0.0f;
        noise_learning_active = false;
        runtime_sim_phase = 0.0f;
        last_frame_process_ms = 0;
        
        // [5] 특징량 초기화
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        latest_vector_valid = latest_sequence_valid = false;

        // [6] 레코더 초기화
        memset(&rec_state, 0, sizeof(rec_state));
        memset(&rec_runtime, 0, sizeof(rec_runtime));
        recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
        recorder_enabled = recorder_sdmmc_mounted = recorder_file_opened = false;
        recorder_fallback_active = recorder_flush_requested = false;
        memset(recorder_active_path, 0, sizeof(recorder_active_path));
        memset(recorder_file_path, 0, sizeof(recorder_file_path));
        memset(recorder_last_error, 0, sizeof(recorder_last_error));
        recorder_record_count = 0; 
        recorder_last_flush_ms = 0;
        recorder_batch_count = 0; 
        recorder_batch_last_push_ms = 0;
        
        // 레코더 배치 벡터 버퍼 초기화 (메모리 찌꺼기 방지)
        memset(recorder_batch_vectors, 0, sizeof(recorder_batch_vectors));
        
        recorder_batch_watermark_low  = T20::C10_Rec::BATCH_WMARK_LOW;
        recorder_batch_watermark_high = T20::C10_Rec::BATCH_WMARK_HIGH;
        recorder_batch_idle_flush_ms  = T20::C10_Rec::BATCH_IDLE_FLUSH_MS;
        memset(recorder_dma_slots, 0, sizeof(recorder_dma_slots));
        memset(recorder_dma_slot_used, 0, sizeof(recorder_dma_slot_used));
        recorder_dma_active_slot = 0; 
        recorder_zero_copy_slot_index = 0;
        recorder_session_open = false; 
        recorder_session_id = 0;
        recorder_session_open_ms = recorder_session_close_ms = 0;
        strlcpy(recorder_session_name, "default", sizeof(recorder_session_name));
        
        recorder_index_count = 0;
        recorder_rotate_keep_max = T20::C10_Rec::ROTATE_KEEP_MAX;
        memset(recorder_index_items, 0, sizeof(recorder_index_items));

        // [7] 뷰어 초기화
        viewer_last_frame_id = 0;
        memset(viewer_last_waveform, 0, sizeof(viewer_last_waveform));
        memset(viewer_last_spectrum, 0, sizeof(viewer_last_spectrum));
        memset(viewer_last_mfcc, 0, sizeof(viewer_last_mfcc));
        viewer_last_waveform_len = viewer_last_spectrum_len = viewer_last_mfcc_len = viewer_last_vector_len = 0;
        memset(viewer_events, 0, sizeof(viewer_events));
        viewer_event_count = 0;
        strlcpy(type_meta_name, "none", sizeof(type_meta_name));
        strlcpy(type_meta_kind, "feature_vector", sizeof(type_meta_kind));
        memset(type_meta_auto_text, 0, sizeof(type_meta_auto_text));
        type_meta_enabled = true; 
        selection_sync_enabled = false;
        selection_sync_frame_from = selection_sync_frame_to = 0;
        selection_sync_effective_from = selection_sync_effective_to = 0;
        selection_sync_range_valid = false;
        memset(selection_sync_name, 0, sizeof(selection_sync_name));
        memset(viewer_selection_points, 0, sizeof(viewer_selection_points));
        memset(viewer_overlay_points, 0, sizeof(viewer_overlay_points));
        viewer_selection_points_len = viewer_overlay_points_len = viewer_overlay_accum_count = viewer_overlay_subset_count = 0;

        // [8] SDMMC 프로필 초기화
        memset(&sdmmc_profile, 0, sizeof(sdmmc_profile));
        memset(sdmmc_profiles, 0, sizeof(sdmmc_profiles));
        sdmmc_profile_applied = false;
        strlcpy(sdmmc_last_apply_reason, "init", sizeof(sdmmc_last_apply_reason));
        strlcpy(sdmmc_profile.profile_name, "default", sizeof(sdmmc_profile.profile_name));
    }
};

extern CL_T20_Mfcc* g_t20_instance;

/* ============================================================================
 * [필수 함수 선언 리스트 - T221_Mfcc_Inter_214.h]
 * ========================================================================== */

// --- 1. Core & RTOS (T230) ---
void    T20_sensorTask(void* p_arg);
void    T20_processTask(void* p_arg);
void    T20_recorderTask(void* p_arg);
void    T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);
void    T20_initProfiles(CL_T20_Mfcc::ST_Impl* p);
void    T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void    T20_handleControlInputs(CL_T20_Mfcc::ST_Impl* p);
void    T20_checkDataFlowWatchdog(CL_T20_Mfcc::ST_Impl* p);
bool    T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len);

// --- 2. DSP & MFCC Engine (T231) ---
bool    T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
float   T20_hzToMel(float p_hz);
float   T20_melToHz(float p_mel);
void    T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
bool    T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p);
void    T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len);
void    T20_applyDCRemove(float* p_data, uint16_t p_len);
void    T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
void    T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
void    T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
void    T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);
void    T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);
void    T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);
void    T20_computeDCT2(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out);
void    T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim);
void    T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out);
void    T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out);
void    T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec);
void    T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out);

// --- 3. Sensor & Hardware (T232) ---
bool    T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
bool    T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample);
bool    T20_bmi270ReadFifoBatch(CL_T20_Mfcc::ST_Impl* p);
bool    T20_bmi270InstallDrdyHook(CL_T20_Mfcc::ST_Impl* p);
bool    T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p);
bool    T20_bmi270_LoadProductionConfig(CL_T20_Mfcc::ST_Impl* p);
void IRAM_ATTR T20_onBmiDrdyISR();

// --- 4. Recorder & Storage (T234) ---
File    T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode);
bool    T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p);
bool    T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg);
bool    T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len);
bool    T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool    T20_commitDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p, uint8_t p_slot_index);
bool    T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool    T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p);
void    T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p);
bool    T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text);
void    T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text);
bool    T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p);
bool    T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p, const char* p_reason);
bool    T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
bool    T20_applySdmmcProfilePins(CL_T20_Mfcc::ST_Impl* p);
bool    T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name);
bool    T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);
bool    T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);

// --- 5. JSON & Web API (T250 등) ---
bool    T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len);
bool    T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);
void    T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim);
void    T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p);
void    T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p);
void    T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p);
bool    T20_buildRecorderFinalizeBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_buildIoSyncBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len);
bool    T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path);
void    T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p);
void    T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p);
uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max);
bool    T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size);
bool    T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool    T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);


