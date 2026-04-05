/* ============================================================================
 * File: T221_Mfcc_Inter_214.h
 * Summary: CL_T20_Mfcc 내부 구현체 (ST_Impl) Full 버전
 * ========================================================================== */



/* ============================================================================
[잔여 구현 계획 재점검 - Inter v212]

원칙
- 헤더에는 선언만 유지
- struct 밖 전역 정의 삽입 금지
- direct member access보다 alias accessor 우선
- multiple definition 가능성 항상 점검

이제부터는 선언을 더 늘리기보다
실제 begin/burst/isr/flush/close/finalize 연결에 필요한 선언만 최소로 유지하는 것이 효율적이다.
============================================================================ */

/* ============================================================================
[반복 오류 패턴 점검 체크리스트 추가]
6. 헤더(.h)에 구조체 멤버 라인을 struct 밖에 삽입하지 말 것
7. 전역 정의가 되어 multiple definition으로 링크 에러가 나는지 점검
8. 신규 상태는 struct 내부 멤버인지, 아니면 alias accessor 전용인지 먼저 구분
============================================================================ */

/* ============================================================================
[향후 단계 구현 예정 정리 - Inter]
1. ST_Impl 멤버는 기능 추가 시 이 파일을 기준으로 먼저 확정
2. 새 상태값 추가 시 생성자 초기화와 JSON/status 출력 동기화
3. 선언/정의/호출 불일치 방지
============================================================================ */

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

#include "T220_Mfcc_214.h"

/* ----------------------------------------------------------------------------
 * CL_T20_Mfcc::ST_Impl 정의
 * ---------------------------------------------------------------------------- */

// 전방선언
void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);


    



struct CL_T20_Mfcc::ST_Impl {

    // [1] 시스템 및 RTOS 핸들 (System & RTOS Resources)
    SPIClass            spi;                    // 센서 통신용 SPI 인터페이스 (FSPI)
    BMI270              bmi;
    TaskHandle_t        sensor_task_handle;     // 센서 데이터 수집 태스크
    TaskHandle_t        process_task_handle;    // DSP/MFCC 연산 태스크
    TaskHandle_t        recorder_task_handle;   // SD/MMC 저장 전용 태스크
    QueueHandle_t       frame_queue;            // 원시 데이터(Raw Frame) 전달 큐
    QueueHandle_t       recorder_queue;         // 특징량(Feature) 저장용 큐
    SemaphoreHandle_t   mutex;                  // 공유 자원 보호용 세마포어

    bool                initialized;            // 시스템 초기화 완료 여부
    bool                running;                // 전체 루프 동작 중 여부
    bool                measurement_active;     // 실제 데이터 측정 활성화 상태

    // [2] 설정 및 프로필 (Configuration & Profiles)
    ST_T20_Config_t     cfg;                    // 현재 동작 중인 시스템 설정
    ST_T20_ProfileInfo_t profiles[G_T20_CFG_PROFILE_COUNT]; // 가용 프로필 리스트 (4U)
    uint8_t             current_profile_index;  // 현재 선택된 프로필 인덱스
    char                runtime_cfg_profile_name[G_T20_RUNTIME_CFG_PROFILE_NAME_MAX]; // 활성 프로필 이름 (32U)

    // [3] 센서(BMI270) 상태 머신 (Sensor State Machine)
    ST_T20_BMI270_State_t        bmi_state;     // v212 표준 상태 (master, spi, init 등 포함)
    ST_T20_BMI270_RuntimeState_t bmi_runtime;   // v210 상세 상태 (burst_flow, isr_hook 등)

    uint8_t             live_source_mode;       // 소스 모드 (Synthetic=0, BMI270=1, Off=255)
    uint8_t             bmi270_chip_id;         // 읽어온 칩 ID (Expected: 0x24)
    bool                bmi270_spi_ok;          // SPI 통신 건전성 상태
    bool                bmi270_drdy_enabled;    // DRDY 인터럽트 활성화 여부
    volatile uint8_t    bmi270_drdy_isr_flag;   // 데이터 준비 인터럽트 플래그 (IRAM 접근용)
    uint32_t            bmi270_sample_counter;  // 누적 샘플 카운트
    uint32_t            bmi270_last_drdy_ms;    // 마지막 DRDY 수신 시점 (ms)
    uint32_t            bmi270_last_poll_ms;    // 마지막 폴링 시점
    float               bmi270_last_axis_values[G_T20_BMI270_BURST_AXIS_COUNT]; // X, Y, Z 최종값 (3U)
    uint8_t             bmi270_axis_mode;       // 현재 해석 중인 축 모드 (Gyro/Acc/Norm)
    char                bmi270_status_text[G_T20_BMI270_STATUS_TEXT_MAX]; // 상태 설명 텍스트 (48U)

    // [4] DSP 및 MFCC 연산 버퍼 (DSP Pipeline Buffers)
    float               frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE]; // 핑퐁 수집 버퍼 (4U x 256U)
    
    // [4] DSP 및 MFCC 연산 버퍼 (DSP Pipeline Buffers) - 16바이트 정렬 적용
    alignas(16) float               work_frame[G_T20_FFT_SIZE];     // 연산용 작업 프레임
    alignas(16) float               temp_frame[G_T20_FFT_SIZE];     // 임시 저장용
    alignas(16) float               window[G_T20_FFT_SIZE];         // 해밍/한 윈도우 계수
    alignas(16) float               power[(G_T20_FFT_SIZE/2)+1];    // 파워 스펙트럼 (Magnitude)
    
    float               noise_spectrum[(G_T20_FFT_SIZE/2)+1]; // 노이즈 학습 데이터
    float               log_mel[G_T20_MEL_FILTERS];      // 로그 멜 결과
    float               mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE/2)+1]; // 멜 가중치 행렬
    float               mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX]; // Delta 계산용 (5U x 32U)
    
    // ESP-DSP 필터용 계수 및 상태 (사이즈 고정)
    alignas(16) float               biquad_coeffs[5];   // 필터 계수       
    alignas(16) float               biquad_state[2];    // 필터 상태 유지


    uint8_t             active_fill_buffer;     // 현재 데이터를 채우고 있는 버퍼 인덱스
    uint16_t            active_sample_index;    // 현재 프레임 내 샘플 인덱스
    uint32_t            dropped_frames;         // 처리 지연으로 누락된 프레임 수
    uint16_t            mfcc_history_count;     // 현재 쌓인 MFCC 이력 수
    
    uint16_t            noise_learned_frames;   // 학습된 노이즈 프레임 수
    // [v214 추가] 노이즈 제어 및 학습 상태 멤버
    bool     noise_learning_active; // 이 라인 추가
    
    
    float               prev_raw_sample;        // Pre-emphasis용 직전 샘플값
    float               runtime_sim_phase;      // 시뮬레이션용 페이즈 변수
    
    uint32_t last_frame_process_ms; 

    // [5] 특징량 및 시퀀스 결과 (Feature & Sequence Output)
    ST_T20_FeatureVector_t     latest_feature;  // 최신 특징량 (MFCC+Delta+Delta2)
    ST_T20_FeatureRingBuffer_t seq_rb;          // 시퀀스 구성용 링버퍼
    bool                       latest_vector_valid;   // 단일 벡터 유효 여부
    bool                       latest_sequence_valid; // 시퀀스 준비 완료 여부

    // [6] 레코더 및 저장 장치 (Recorder & Storage)
    ST_T20_RecorderState_t        rec_state;    // 레코더 표준 상태 (storage, file_io, write 등)
    ST_T20_RecorderRuntimeState_t rec_runtime;  // v210 상세 상태 (finalize, sync 등)

    EM_T20_StorageBackend_t recorder_storage_backend; // 저장소 종류 (LittleFS/SDMMC)
    bool                recorder_enabled;       // 레코딩 활성화 상태
    bool                recorder_sdmmc_mounted; // SD카드 마운트 여부
    bool                recorder_file_opened;   // 현재 파일 열림 여부 (v213 추가)
    bool                recorder_fallback_active; // Fallback 모드 활성화 여부
    bool                recorder_flush_requested; // Flush 요청 플래그 (v213 추가)
    char                recorder_active_path[128]; // 현재 작업 경로
    char                recorder_file_path[G_T20_RECORDER_FILE_PATH_MAX]; // 설정된 파일 경로
    char                recorder_last_error[G_T20_RECORDER_LAST_ERROR_MAX]; // 에러 메시지
    uint32_t            recorder_record_count;  // 누적 레코드 수
    uint32_t            recorder_last_flush_ms; // 마지막 Flush 시간

    // 배치 및 DMA 설정 (v213 세분화)
    uint16_t            recorder_batch_watermark_low;  // 하한 워터마크
    uint16_t            recorder_batch_watermark_high; // 상한 워터마크
    uint32_t            recorder_batch_idle_flush_ms;  // 유휴 시간 Flush 기준
    uint32_t            recorder_batch_last_push_ms;   // 마지막 데이터 투입 시간
    uint16_t            recorder_batch_count;          // 현재 배치 데이터 수
    ST_T20_RecorderVectorMessage_t recorder_batch_vectors[G_T20_RECORDER_BATCH_VECTOR_MAX];

    // Zero-Copy DMA 슬롯
    uint8_t             recorder_dma_slots[G_T20_ZERO_COPY_DMA_SLOT_COUNT][G_T20_ZERO_COPY_DMA_SLOT_BYTES] __attribute__((aligned(32)));
    uint16_t            recorder_dma_slot_used[G_T20_ZERO_COPY_DMA_SLOT_COUNT];
    uint8_t             recorder_dma_active_slot;
    uint8_t             recorder_zero_copy_slot_index;

    // 세션 관리 (v210 기능)
    bool                recorder_session_open;
    uint32_t            recorder_session_id;
    char                recorder_session_name[G_T20_RECORDER_SESSION_NAME_MAX];
    uint32_t            recorder_session_open_ms;
    uint32_t            recorder_session_close_ms;
    
        // 레코더 인덱스 및 로테이션]
    uint16_t            recorder_index_count;      // <--- 추가
    uint16_t            recorder_rotate_keep_max;  // <--- 추가
    struct ST_T20_IndexItem {
        char     path[128];
        uint32_t size_bytes;
        uint32_t created_ms;
        uint32_t record_count;
    } recorder_index_items[G_T20_RECORDER_MAX_ROTATE_LIST]; // <--- 추가


    // [7] 뷰어 및 웹 인터페이스 (Viewer & Meta Data)
    uint32_t            viewer_last_frame_id;
    float               viewer_last_waveform[G_T20_FFT_SIZE];
    float               viewer_last_spectrum[(G_T20_FFT_SIZE/2)+1];
    float               viewer_last_mfcc[G_T20_MFCC_COEFFS_MAX];
    uint16_t            viewer_last_waveform_len;
    uint16_t            viewer_last_spectrum_len;
    uint16_t            viewer_last_mfcc_len;
    uint16_t            viewer_last_vector_len;

    ST_T20_ViewerEvent_t viewer_events[G_T20_VIEWER_EVENT_MAX];
    uint16_t            viewer_event_count;

    // 메타데이터 및 동기화 (Web 연동용)
    char                type_meta_name[G_T20_TYPE_META_NAME_MAX];
    char                type_meta_kind[G_T20_TYPE_META_KIND_MAX];
    char                type_meta_auto_text[G_T20_TYPE_META_AUTO_TEXT_MAX];
    bool                type_meta_enabled;
    bool                selection_sync_enabled;
    uint32_t            selection_sync_frame_from;
    uint32_t            selection_sync_frame_to;
    uint32_t            selection_sync_effective_from;
    uint32_t            selection_sync_effective_to;
    bool                selection_sync_range_valid;
    char                selection_sync_name[G_T20_SELECTION_SYNC_NAME_MAX];

    float               viewer_selection_points[G_T20_VIEWER_SELECTION_POINTS_MAX];
    uint16_t            viewer_selection_points_len;
    float               viewer_overlay_points[G_T20_VIEWER_SELECTION_POINTS_MAX];
    uint16_t            viewer_overlay_points_len;
    uint16_t            viewer_overlay_accum_count;
    uint16_t            viewer_overlay_subset_count;

    // [8] SDMMC 프로필 및 상태 (v212 로직 대응을 위해 추가)
    struct ST_T20_SdmmcProfile {
        char     profile_name[32];
        bool     enabled;
        bool     use_1bit_mode;
        int8_t   clk_pin;
        int8_t   cmd_pin;
        int8_t   d0_pin;
        int8_t   d1_pin;
        int8_t   d2_pin;
        int8_t   d3_pin;
    } sdmmc_profile, sdmmc_profiles[G_T20_SDMMC_PROFILE_PRESET_COUNT];

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
        live_source_mode = 0; // SYNTHETIC
        bmi270_chip_id = 0;
        bmi270_spi_ok = bmi270_drdy_enabled = false;
        bmi270_drdy_isr_flag = 0;
        bmi270_sample_counter = 0;
        bmi270_last_drdy_ms = bmi270_last_poll_ms = 0;
        memset(bmi270_last_axis_values, 0, sizeof(bmi270_last_axis_values));
        bmi270_axis_mode = 0; // GYRO_Z
        strlcpy(bmi270_status_text, "idle", sizeof(bmi270_status_text));

        // [4] DSP 버퍼 초기화
        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(work_frame, 0, sizeof(work_frame));
        memset(window, 0, sizeof(window));
        memset(power, 0, sizeof(power));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(log_mel, 0, sizeof(log_mel));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        active_fill_buffer = 0; active_sample_index = 0;
        dropped_frames = 0; mfcc_history_count = 0;
        noise_learned_frames = 0; prev_raw_sample = 0.0f;
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
        recorder_flush_requested = false;
        memset(recorder_active_path, 0, sizeof(recorder_active_path));
        memset(recorder_last_error, 0, sizeof(recorder_last_error));
        recorder_record_count = 0; recorder_last_flush_ms = 0;
        recorder_batch_count = 0; recorder_batch_last_push_ms = 0;
        recorder_batch_watermark_low = G_T20_RECORDER_BATCH_WATERMARK_LOW;
        recorder_batch_watermark_high = G_T20_RECORDER_BATCH_WATERMARK_HIGH;
        recorder_batch_idle_flush_ms = G_T20_RECORDER_BATCH_IDLE_FLUSH_MS;
        memset(recorder_dma_slots, 0, sizeof(recorder_dma_slots));
        memset(recorder_dma_slot_used, 0, sizeof(recorder_dma_slot_used));
        recorder_dma_active_slot = 0; recorder_zero_copy_slot_index = 0;
        recorder_session_open = false; recorder_session_id = 0;
        strlcpy(recorder_session_name, "default", sizeof(recorder_session_name));

        // [7] 뷰어 초기화
        viewer_last_frame_id = 0;
        memset(viewer_last_waveform, 0, sizeof(viewer_last_waveform));
        memset(viewer_last_spectrum, 0, sizeof(viewer_last_spectrum));
        memset(viewer_last_mfcc, 0, sizeof(viewer_last_mfcc));
        viewer_last_waveform_len = viewer_last_spectrum_len = viewer_last_mfcc_len = 0;
        memset(viewer_events, 0, sizeof(viewer_events));
        viewer_event_count = 0;
        strlcpy(type_meta_name, "none", sizeof(type_meta_name));
        strlcpy(type_meta_kind, "feature_vector", sizeof(type_meta_kind));
        type_meta_enabled = true; selection_sync_enabled = false;
        selection_sync_range_valid = false;
        
        // SDMMC 관련 초기화 추가
        memset(&sdmmc_profile, 0, sizeof(sdmmc_profile));
        memset(sdmmc_profiles, 0, sizeof(sdmmc_profiles));
        sdmmc_profile_applied = false;
        strlcpy(sdmmc_last_apply_reason, "init", sizeof(sdmmc_last_apply_reason));
        
        // (필요 시) 기본 프로필 이름 설정
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
void    T20_computeDCT2(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out); // 시그니처 변경됨
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




/*
bool				T20_validateConfig(const ST_T20_Config_t* p_cfg);
void				T20_initProfiles(CL_T20_Mfcc::ST_Impl* p);
void				T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);
void				T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);
void				T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);
void				T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

float				T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
void				T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim);
void				T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out);
void				T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out);
void				T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec);

void				T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim);
void				T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);
bool				T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
void				T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);
void				T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);
void				T20_fillSyntheticFrame(CL_T20_Mfcc::ST_Impl* p, float* p_out_frame, uint16_t p_len);
bool				T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len);

bool				T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out);
bool				T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out);
bool				T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p);
bool				T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg);
bool				T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg);
bool				T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out);

bool				T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json);
bool				T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);

bool				T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text);
bool				T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);
bool				T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p);
bool				T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len);

bool				T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildViewerChartBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, uint16_t p_points);

bool				T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildRecorderPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool				T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool				T20_buildRecorderRangeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length);
bool				T20_buildRecorderBinaryHeaderJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path);
bool				T20_buildRecorderCsvTableJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool				T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool				T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes);
bool				T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes,
															  const char* p_global_filter, const char* p_col_filters_csv,
															  uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size);
bool				T20_buildRecorderBinaryRecordsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit);
bool				T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path);

bool				T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value);
bool				T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out_value);
bool				T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out_value);
bool				T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out_buf, uint16_t p_len);

bool				T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode);
bool				T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type);
bool				T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out);
bool				T20_isLikelyDateText(const String& p_text);
bool				T20_isLikelyDateTimeText(const String& p_text);
String				T20_upgradeCsvTypeGuess(const String& p_current, const String& p_cell);
bool				T20_csvRowMatchesGlobalFilter(const std::vector<String>& p_row, const String& p_filter);

bool				T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
bool				T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p);
void				T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len);
float				T20_hzToMel(float p_hz);
float				T20_melToHz(float p_mel);
void				T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
void				T20_applyDCRemove(float* p_data, uint16_t p_len);
void				T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
void				T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
void				T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);
void				T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);
void				T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);
void				T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
void				T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);
void				T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out);

uint8_t				T20_selectRecorderWriteBufferSlot(CL_T20_Mfcc::ST_Impl* p);
void				T20_recorderFlushIfNeeded(CL_T20_Mfcc::ST_Impl* p, bool p_force);
void				T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text);
void				T20_initSdmmcProfiles(CL_T20_Mfcc::ST_Impl* p);
bool				T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name);
bool				T20_applySdmmcProfilePins(CL_T20_Mfcc::ST_Impl* p);
bool				T20_buildSdmmcProfilesJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
void				T20_unmountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p);
bool				T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg);
bool				T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p);
bool				T20_recorderAppendVector(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
uint8_t				T20_getActiveDmaSlotIndex(CL_T20_Mfcc::ST_Impl* p);
bool				T20_rotateDmaSlot(CL_T20_Mfcc::ST_Impl* p);
bool				T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool				T20_commitDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p, uint8_t p_slot_index);
bool				T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p);
bool				T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg);
bool				T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p);
bool				T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p);
bool				T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p);
File				T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode);

uint32_t			T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p);
bool				T20_rotateListDeleteFile(const char* p_path);
void				T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p);
bool				T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len);
void				T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default);
uint32_t			T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max);
void				T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

void				T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p);
void				T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p);

void				T20_updateViewerSelectionProjection(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_buildTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_updateViewerOverlayProjection(CL_T20_Mfcc::ST_Impl* p);

bool				T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path);
void				T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p);

void				T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p);

void				T20_updateTypePreviewHeaderGuess(CL_T20_Mfcc::ST_Impl* p);

void				T20_updateTypePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
void				T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p);

void				T20_updatePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildUnifiedViewerBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len);

bool				T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderFallbackToLittleFs(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderStorageJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len);
bool				T20_beginLiveSource(CL_T20_Mfcc::ST_Impl* p);
void				T20_stopLiveSource(CL_T20_Mfcc::ST_Impl* p);
bool				T20_processLiveSourceTick(CL_T20_Mfcc::ST_Impl* p);
bool				T20_buildLiveSourceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_feedLiveSample(CL_T20_Mfcc::ST_Impl* p, float p_value);

bool				T20_tryBuildLiveFrame(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildLiveDebugJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderOpenSession(CL_T20_Mfcc::ST_Impl* p, const char* p_name);

bool				T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p, const char* p_reason);

bool				T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text);

bool				T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderSessionJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);

bool				T20_configBMI270_2100Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);

bool				T20_readBMI270Sample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample);

bool				T20_pushLiveQueueSample(CL_T20_Mfcc::ST_Impl* p, float p_sample);

bool				T20_drainLiveQueueToFrameBuffer(CL_T20_Mfcc::ST_Impl* p);

bool				T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p);

void				T20_liveHeartbeat(CL_T20_Mfcc::ST_Impl* p);

bool				T20_probeBMI270ChipId(CL_T20_Mfcc::ST_Impl* p);

bool				T20_enableBMI270Drdy(CL_T20_Mfcc::ST_Impl* p);

bool				T20_pollBMI270Drdy(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270ReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out);

bool				T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample);

void				T20_bmi270DrdyIsr(CL_T20_Mfcc::ST_Impl* p);

bool				T20_beginActualBmi270SpiPath(CL_T20_Mfcc::ST_Impl* p);

bool				T20_tryConsumeBmi270IsrQueue(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270ReadBurstSample(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len);

bool				T20_bmi270BeginSpiTransaction(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270EndSpiTransaction(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270AttachDrdyIsr(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270BeginSpiBus(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareReadReg(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg);

bool				T20_bmi270ExecutePreparedRead(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_out);

bool				T20_bmi270InstallDrdyHook(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270ActualSpiBegin(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270ActualAttachIsr(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270ActualReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out);

bool				T20_bmi270DecodeBurstToSample(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len, float* p_out);

bool				T20_bmi270DecodeBurstAxes(CL_T20_Mfcc::ST_Impl* p, const uint8_t* p_buf, uint16_t p_len);

void				T20_bmi270SetReadState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270ActualReadBurst(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len);

void				T20_recorderSetFinalizeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_recorderPrepareFinalize(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetBurstFlowState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_recorderFinalizeSaveSummary(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetIsrRequestState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizeResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void				T20_bmi270SetSpiStartState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetIsrHookState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizePersistState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_recorderPreparePersistFinalize(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetRegBurstState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizePersistResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void				T20_bmi270SetActualReadTxnState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizeSaveState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetSpiExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizeExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareActualSpiExecute(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareExecutePersist(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetSpiApplyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizeCommitState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareApplyActualRead(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareCommitPersist(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetSpiSessionState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetBurstApplyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetPersistWriteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetCommitResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

bool				T20_bmi270OpenActualSpiSession(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270CloseActualSpiSession(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareBurstApply(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPreparePersistWrite(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetTxnPipelineState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizePipelineState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareTransactionPipeline(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalizePipeline(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetReadbackState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetManifestState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareReadback(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareManifestWrite(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetVerifyState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetVerifyResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void				T20_recorderSetIndexState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetSummaryState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareVerify(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareIndexWrite(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareSummaryWrite(CL_T20_Mfcc::ST_Impl* p);

void				T20_bmi270SetHwBridgeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetIsrBridgeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetArtifactState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetArtifactResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

bool				T20_bmi270PrepareHardwareBridge(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareIsrBridge(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareArtifactWrite(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderArtifactJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetBootState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetIrqRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetIrqFilterState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetPackageState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetCleanupState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareBootFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareIrqRoute(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareIrqFilter(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPreparePackageFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareCleanupFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderPackageCleanupJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetSpiClassState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetHwExecState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetExportState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetRecoverState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareSpiClassBridge(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareHardwareExecute(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareExportFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRecoverFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderExportRecoverJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetLiveCaptureState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetSamplePipeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetDeliveryState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalReportState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareLiveCaptureFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareSamplePipe(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareDeliveryFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalReport(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderDeliveryJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetDriverState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetSessionCtrlState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetPublishState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetAuditState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareDriverLayer(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareSessionControl(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPreparePublishFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareAuditFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderPublishAuditJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetActualBeginState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);
bool				T20_bmi270PrepareActualBeginRequest(CL_T20_Mfcc::ST_Impl* p);
bool				T20_buildRecorderFinalizeBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetSpiBeginRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetRegisterReadRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFileWriteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetBundleMapState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareSpiBeginRuntime(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRegisterReadRuntime(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFileWriteFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareBundleMapFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderFileBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetSpiAttachPrepState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetBurstReadPrepState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetPathRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetWriteFinalizeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareSpiAttachPrep(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareBurstReadPrep(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPreparePathRouteFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareWriteFinalizeFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderWriteFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetBurstRuntimeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetIsrQueueState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetCommitRouteState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetFinalizeSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareBurstRuntimeFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareIsrQueueFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareCommitRouteFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalizeSyncFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderCommitFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_recorderSetStoreBundleState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetStoreResult(CL_T20_Mfcc::ST_Impl* p, uint8_t p_result);

void				T20_recorderSetWriteCommitState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_recorderPrepareStoreBundle(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareWriteCommitFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRecorderStoreBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetHwLinkState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetFrameBuildState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetMetaSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetReportSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareHwLinkFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareFrameBuildFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareMetaSyncFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareReportSyncFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildHwAndFinalizeSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

void				T20_bmi270SetDspIngressState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_bmi270SetRawPipeState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetAuditSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

void				T20_recorderSetManifestSyncState(CL_T20_Mfcc::ST_Impl* p, uint8_t p_state);

bool				T20_bmi270PrepareDspIngressFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRawPipeFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareAuditSyncFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareManifestSyncFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildIoSyncBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PreparePipelineLinkFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRealApplyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalSyncBundleFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRealApplyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRealPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PreparePipelineReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PreparePipelineExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareSyncReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareSyncExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildPipelineExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareExecLinkFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareDspReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareSyncLinkFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildExecReadyBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareRuntimeReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRuntimeExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRuntimeReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRuntimeExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildRuntimeExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareApplyReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareApplyExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareApplyReadyFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareApplyExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildApplyExecBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareApplyPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRealPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareApplyPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRealPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildApplyPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareMegaPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareRealConnectStageFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareMegaPipelineFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareRealConnectStageFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildMegaPipelineBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareIntegrationBundleFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareConnectPrepFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareIntegrationBundleFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareConnectPrepFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildIntegrationBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

bool				T20_bmi270PrepareFinalIntegrationFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_bmi270PrepareConnectExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareFinalIntegrationFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_recorderPrepareConnectExecFlow(CL_T20_Mfcc::ST_Impl* p);

bool				T20_buildFinalIntegrationBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);


// [함수 전방 선언 추가 - T230에서 호출 가능하도록]
bool T20_bmi270_LoadProductionConfig(CL_T20_Mfcc::ST_Impl* p);
bool T20_bmi270ActualReadRegister(CL_T20_Mfcc::ST_Impl* p, uint8_t p_reg, uint8_t* p_out);

*/


bool				T20_buildRecorderFinalizeBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_buildIoSyncBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);
bool				T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len);
bool				T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path);
void				T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p);
void				T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p);
uint32_t			T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max);
bool				T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes,
															  const char* p_global_filter, const char* p_col_filters_csv,
															  uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size);
bool				T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len);

