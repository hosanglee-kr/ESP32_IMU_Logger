/* ============================================================================
 * File: T230_Mfcc_Cor_216.cpp
 * Summary: 메인 컨트롤러 및 RTOS 태스크 스케줄링 (C++17 Namespace & SIMD 핑퐁 최적화 반영)
 * ========================================================================== */

#include "T221_Mfcc_Inter_216.h"

CL_T20_Mfcc* g_t20_instance = nullptr;

// --- 전방 선언(Forward Declaration) ---
void T20_sensorTask(void* p_arg);
void T20_processTask(void* p_arg);
void T20_recorderTask(void* p_arg);

// 클래스 생성자 및 소멸자
CL_T20_Mfcc::CL_T20_Mfcc() : _impl(new ST_Impl()) {
    g_t20_instance = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc() {
    stop();
    if (_impl) delete _impl;
    if (g_t20_instance == this) g_t20_instance = nullptr;
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
    if (_impl == nullptr) return false;

    // [Step 1] 메모리 및 기본 구조체 초기화
    T20_resetRuntimeResources(_impl);
    _impl->cfg = (p_cfg != nullptr) ? *p_cfg : T20_makeDefaultConfig();

    // [Step 2] 동기화 객체 생성
    _impl->mutex = xSemaphoreCreateMutex();
    _impl->frame_queue = xQueueCreate(T20::C10_Sys::QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    _impl->recorder_queue = xQueueCreate(32, sizeof(ST_T20_RecorderVectorMessage_t)); 

    // [Step 3] 하드웨어 물리 연결 (SPI)
    _impl->spi.begin(
        T20::C10_Pin::BMI_SCK, 
        T20::C10_Pin::BMI_MISO, 
        T20::C10_Pin::BMI_MOSI, 
        T20::C10_Pin::BMI_CS
    );

    
    // [Step 4] 센서 펌웨어 설정 적용 
    if (!T20_bmi270_LoadProductionConfig(_impl)) {
        strlcpy(_impl->bmi270_status_text, "hw_init_fail", T20::C10_BMI::STATUS_TEXT_MAX);
        return false;
    }
    
    LittleFS.begin(false);
    
    // [Step 5] DSP 및 파일 시스템 준비
    T20_initDSP(_impl);
    T20_loadRuntimeConfigFile(_impl);
    T20_tryMountSdmmcRecorderBackend(_impl); 

    _impl->initialized = true;
    _impl->bmi_state.init = EN_T20_STATE_DONE;
    return true;
}

bool CL_T20_Mfcc::start(void) {
    if (_impl == nullptr || !_impl->initialized || _impl->running) return false;

    pinMode(_impl->cfg.system.button_pin, INPUT_PULLUP);
    _impl->measurement_active = _impl->cfg.system.auto_start;

    // Core 분산 배치: 센서(Core 0), 연산 및 저장(Core 1)
    xTaskCreatePinnedToCore(T20_sensorTask,   "T20_Sens", T20::C10_Task::SENSOR_STACK,   _impl, T20::C10_Task::SENSOR_PRIO,   &_impl->sensor_task_handle,   0);
    xTaskCreatePinnedToCore(T20_processTask,  "T20_Proc", T20::C10_Task::PROCESS_STACK,  _impl, T20::C10_Task::PROCESS_PRIO,  &_impl->process_task_handle,  1);
    xTaskCreatePinnedToCore(T20_recorderTask, "T20_Rec",  T20::C10_Task::RECORDER_STACK, _impl, T20::C10_Task::RECORDER_PRIO, &_impl->recorder_task_handle, 1);

    _impl->running = true;
    _impl->bmi_state.runtime = EN_T20_STATE_RUNNING;
    return true;
}

void CL_T20_Mfcc::stop(void) {
    if (_impl != nullptr) {
        _impl->running = false;
        T20_stopTasks(_impl);
    }
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p) {
    if (p->sensor_task_handle) { vTaskDelete(p->sensor_task_handle); p->sensor_task_handle = nullptr; }
    if (p->process_task_handle) { vTaskDelete(p->process_task_handle); p->process_task_handle = nullptr; }
    if (p->recorder_task_handle) { vTaskDelete(p->recorder_task_handle); p->recorder_task_handle = nullptr; }
}

// ============================================================================
// 핑퐁 버퍼(Double Buffering)가 완벽히 적용된 Sensor Task
// ========================================================================== 

void T20_sensorTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    T20_bmi270InstallDrdyHook(p);
    
    // FIFI Batch 방식
    for (;;) {
        // 인터럽트(16샘플 누적 시 발생) 대기
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        // 함수 내부에서 한 번에 16개를 긁어오고 핑퐁 버퍼와 큐 전송까지 일괄 완료
        T20_bmi270ReadFifoBatch(p); 
    }
    
    // 단건 방식
    #ifdef single_read
        for (;;) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            
            if (!p->running) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    
            float sample = 0.0f;
            if (T20_bmi270ReadVectorSample(p, &sample)) {
                p->bmi270_sample_counter++;
    
                // 현재 쓰기 권한이 있는 버퍼 인덱스
                uint8_t write_idx = p->active_fill_buffer;
                uint16_t sample_idx = p->active_sample_index % T20::C10_DSP::FFT_SIZE;
                
                p->frame_buffer[write_idx][sample_idx] = sample; 
                p->active_sample_index++;
    
                // Hop Size 달성 시 DSP 태스크로 버퍼 인덱스 통지
                if ((p->active_sample_index % p->cfg.feature.hop_size) == 0 && p->active_sample_index >= T20::C10_DSP::FFT_SIZE) {
                    ST_T20_FrameMessage_t msg;
                    msg.frame_index = write_idx; 
                    
                    if (xQueueSend(p->frame_queue, &msg, 0) == pdPASS) {
                        // 전송 성공 시 핑퐁 스위칭
                        p->active_fill_buffer = (write_idx + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                    } else {
                        p->dropped_frames++;
                    }
                }
            }
        }
    #endif
}

// ============================================================================
// SIMD 최적화 연동을 위한 Process Task
// ========================================================================== 
void T20_processTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    ST_T20_FrameMessage_t msg;
    
    alignas(16) float dsp_input[T20::C10_DSP::FFT_SIZE];

    for (;;) {
        if (xQueueReceive(p->frame_queue, &msg, portMAX_DELAY) == pdTRUE) {
            uint8_t read_idx = msg.frame_index; 
            
            // 공유 다중 버퍼에서 로컬 정렬 버퍼로 고속 복사 (Data Race 방지)
            memcpy(dsp_input, p->frame_buffer[read_idx], sizeof(float) * T20::C10_DSP::FFT_SIZE);
            p->last_frame_process_ms = millis();

            T20_processOneFrame(p, dsp_input, T20::C10_DSP::FFT_SIZE);
        }
    }
}


bool T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len) {
    if (!p || !p_frame) return false;

    alignas(16) float current_mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    alignas(16) float delta[T20::C10_DSP::MFCC_COEFFS_MAX];
    alignas(16) float delta2[T20::C10_DSP::MFCC_COEFFS_MAX];

    T20_computeMFCC(p, p_frame, current_mfcc);
    T20_pushMfccHistory(p, current_mfcc, p->cfg.feature.mfcc_coeffs);
    
    if (p->mfcc_history_count >= T20::C10_DSP::MFCC_HISTORY) {
        uint16_t dim = p->cfg.feature.mfcc_coeffs;

        T20_computeDeltaFromHistory(p, dim, 2, delta);
        T20_computeDeltaDeltaFromHistory(p, dim, delta2);

        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            T20_buildVector(p->mfcc_history[2], delta, delta2, dim, p->latest_feature.vector);
            
            memcpy(p->viewer_last_waveform, p_frame, sizeof(float) * T20::C10_DSP::FFT_SIZE);
            memcpy(p->viewer_last_spectrum, p->power, sizeof(float) * ((T20::C10_DSP::FFT_SIZE/2)+1));
            
            p->latest_vector_valid = true;
            xSemaphoreGive(p->mutex);
        }

        p->viewer_last_frame_id++;

        T20_broadcastBinaryData(p);
        
        if (p->recorder_enabled) {
            ST_T20_RecorderVectorMessage_t rec_msg;
            rec_msg.frame_id = p->viewer_last_frame_id;
            rec_msg.vector_len = dim * 3; 
            memcpy(rec_msg.vector, p->latest_feature.vector, sizeof(float) * rec_msg.vector_len);
            
            T20_stageVectorToDmaSlot(p, &rec_msg);
        }
        return true;
    }
    
    return false; 
}


void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;
    p->running = false;
    p->recorder_record_count = 0;
    p->dropped_frames = 0;
}

void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;
    for (int i = 0; i < T20::C10_Sys::CFG_PROFILE_COUNT; i++) {
        p->profiles[i].used = false;
        snprintf(p->profiles[i].name, sizeof(p->profiles[i].name), "Profile_%d", i);
    }
}

void CL_T20_Mfcc::printConfig(Stream& p_out) const {
	p_out.println(F("----------- T20_Mfcc Config -----------"));
	p_out.printf("FrameSize   : %u\n", _impl->cfg.feature.frame_size);
	p_out.printf("HopSize     : %u\n", _impl->cfg.feature.hop_size);
	p_out.printf("MFCC Coeffs : %u\n", _impl->cfg.feature.mfcc_coeffs);
	p_out.printf("Output Mode : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
	p_out.println(F("---------------------------------------"));
}

void CL_T20_Mfcc::printStatus(Stream& p_out) const {
	p_out.println(F("----------- T20_Mfcc Status -----------"));
	p_out.printf("Initialized  : %s\n", _impl->initialized ? "YES" : "NO");
	p_out.printf("Running      : %s\n", _impl->running ? "YES" : "NO");
	p_out.printf("Record Count : %lu\n", (unsigned long)_impl->recorder_record_count);
	p_out.printf("Dropped      : %lu\n", (unsigned long)_impl->dropped_frames);
	p_out.println(F("---------------------------------------"));
}


bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    if (p == nullptr || !p->latest_vector_valid) return false;

    JsonDocument doc;
    doc["frame_id"] = p->viewer_last_frame_id;
    doc["vector_len"] = p->latest_feature.vector_len; 

    JsonArray mfcc = doc["mfcc"].to<JsonArray>();
    for (int i = 0; i < 13; i++) mfcc.add(p->latest_feature.vector[i]);

    JsonArray delta = doc["delta"].to<JsonArray>();
    for (int i = 13; i < 26; i++) delta.add(p->latest_feature.vector[i]);

    JsonArray delta2 = doc["delta2"].to<JsonArray>();
    for (int i = 26; i < 39; i++) delta2.add(p->latest_feature.vector[i]);

    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    JsonDocument doc;
    doc["session_id"] = p->recorder_session_id;
    doc["final_count"] = p->recorder_record_count;
    doc["duration_ms"] = p->recorder_session_close_ms - p->recorder_session_open_ms;
    doc["status"] = (p->rec_state.write == EN_T20_STATE_ERROR) ? "fail" : "success";
    doc["last_error"] = p->recorder_last_error;

    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

void T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return;
    T20_updateSelectionSyncState(p);
    T20_updateTypeMetaAutoClassify(p);
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim) {
    if (p_rb == nullptr) return;
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
}

void T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return;

    if (!p->selection_sync_enabled || p->selection_sync_frame_to < p->selection_sync_frame_from) {
        p->selection_sync_range_valid     = false;
        p->selection_sync_effective_from = p->selection_sync_enabled ? p->selection_sync_frame_from : 0;
        p->selection_sync_effective_to     = p->selection_sync_enabled ? p->selection_sync_frame_to : 0;
        return;
    }

    p->selection_sync_range_valid     = true;
    p->selection_sync_effective_from = p->selection_sync_frame_from;
    p->selection_sync_effective_to     = p->selection_sync_frame_to;
}

void T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return;

    if (!p->type_meta_enabled) {
        strlcpy(p->type_meta_auto_text, "disabled", sizeof(p->type_meta_auto_text));
        return;
    }

    if (strcmp(p->type_meta_kind, "feature_vector") == 0) {
        if (p->viewer_last_vector_len >= 39U) {
            strlcpy(p->type_meta_auto_text, "mfcc_39d_vector", sizeof(p->type_meta_auto_text));
        } else if (p->viewer_last_vector_len > 0U) {
            strlcpy(p->type_meta_auto_text, "compact_feature_vector", sizeof(p->type_meta_auto_text));
        } else {
            strlcpy(p->type_meta_auto_text, "empty_feature_vector", sizeof(p->type_meta_auto_text));
        }
    } else if (strcmp(p->type_meta_kind, "sequence") == 0) {
        strlcpy(p->type_meta_auto_text, p->latest_sequence_valid ? "sequence_ready" : "sequence_warming_up", sizeof(p->type_meta_auto_text));
    } else if (strcmp(p->type_meta_kind, "waveform") == 0) {
        strlcpy(p->type_meta_auto_text, "waveform_frame", sizeof(p->type_meta_auto_text));
    } else {
        strlcpy(p->type_meta_auto_text, "generic_meta", sizeof(p->type_meta_auto_text));
    }
}

void T20_handleControlInputs(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;
    static uint32_t last_btn_ms = 0;
    if (digitalRead(p->cfg.system.button_pin) == LOW) {
        if (millis() - last_btn_ms > T20::C10_Web::BTN_DEBOUNCE_MS) { 
            p->measurement_active = !p->measurement_active;
            last_btn_ms = millis();
            T20_recorderWriteEvent(p, p->measurement_active ? "btn_start" : "btn_stop");
        }
    }
}

void T20_checkDataFlowWatchdog(CL_T20_Mfcc::ST_Impl* p) {
    static uint32_t last_counter = 0;
    static uint32_t last_check_ms = 0;

    if (millis() - last_check_ms > 1000) { 
        if (p->measurement_active && p->bmi270_sample_counter == last_counter) {
            T20_recorderWriteEvent(p, "wdog_stall_reinit");
            T20_tryBMI270Reinit(p); 
        }
        last_counter = p->bmi270_sample_counter;
        last_check_ms = millis();
    }
}

bool T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len) {
    if (p_out_buf == nullptr || p_len == 0) return false;
    size_t needed = measureJson(p_doc) + 1;
    if (needed > p_len) {
        strlcpy(p_out_buf, "{\"error\":\"buffer_overflow\"}", p_len);
        return false;
    }
    serializeJson(p_doc, p_out_buf, p_len);
    return true;
}

bool T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text) {
    if (p == nullptr || p_json_text == nullptr) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, p_json_text);
    if (err) return false;

    const char* profile_name        = doc["profile_name"] | p->runtime_cfg_profile_name;
    const char* backend             = doc["recorder_backend"] | ((p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc");
    const char* sdmmc_profile       = doc["sdmmc_profile"] | p->sdmmc_profile.profile_name;
    const char* output_mode         = doc["output_mode"] | ((p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence");
    const char* selection_sync_name = doc["selection_sync_name"] | p->selection_sync_name;
    const char* type_meta_name      = doc["type_meta_name"] | p->type_meta_name;
    const char* type_meta_kind      = doc["type_meta_kind"] | p->type_meta_kind;

    strlcpy(p->runtime_cfg_profile_name, profile_name, sizeof(p->runtime_cfg_profile_name));
    strlcpy(p->selection_sync_name, selection_sync_name, sizeof(p->selection_sync_name));
    strlcpy(p->type_meta_name, type_meta_name, sizeof(p->type_meta_name));
    strlcpy(p->type_meta_kind, type_meta_kind, sizeof(p->type_meta_kind));

    p->cfg.feature.hop_size                       = doc["hop_size"] | p->cfg.feature.hop_size;
    p->cfg.feature.mfcc_coeffs                    = doc["mfcc_coeffs"] | p->cfg.feature.mfcc_coeffs;
    p->cfg.preprocess.preemphasis.alpha           = doc["pre_alpha"] | p->cfg.preprocess.preemphasis.alpha;
    p->cfg.preprocess.filter.cutoff_hz_1          = doc["filter_cutoff"] | p->cfg.preprocess.filter.cutoff_hz_1;
    p->cfg.preprocess.noise.spectral_subtract_strength = doc["sub_strength"] | p->cfg.preprocess.noise.spectral_subtract_strength;
    
    if (doc["noise_mode"].is<int>()) {
        int n_mode = doc["noise_mode"].as<int>();
        if (n_mode >= 0 && n_mode <= 2) {
            p->cfg.preprocess.noise.mode = static_cast<EM_T20_NoiseMode_t>(n_mode);
        }
    }

    p->type_meta_enabled             = doc["type_meta_enabled"] | p->type_meta_enabled;
    p->cfg.output.sequence_frames    = doc["sequence_frames"] | p->cfg.output.sequence_frames;
    p->recorder_enabled              = doc["recorder_enabled"] | p->recorder_enabled;
    p->selection_sync_enabled        = doc["selection_sync_enabled"] | p->selection_sync_enabled;
    p->selection_sync_frame_from     = doc["selection_sync_frame_from"] | p->selection_sync_frame_from;
    p->selection_sync_frame_to       = doc["selection_sync_frame_to"] | p->selection_sync_frame_to;
    
    p->recorder_batch_watermark_low  = doc["batch_watermark_low"] | p->recorder_batch_watermark_low;
    p->recorder_batch_watermark_high = doc["batch_watermark_high"] | p->recorder_batch_watermark_high;
    p->recorder_batch_idle_flush_ms  = doc["batch_idle_flush_ms"] | p->recorder_batch_idle_flush_ms;
    
    if (p->recorder_batch_watermark_low == 0) p->recorder_batch_watermark_low = 1;
    if (p->recorder_batch_watermark_high < p->recorder_batch_watermark_low) {
        p->recorder_batch_watermark_high = p->recorder_batch_watermark_low;
    }

    if (strcmp(backend, "sdmmc") == 0) p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
    else p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;

    if (strcmp(output_mode, "sequence") == 0) p->cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;
    else p->cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;

    T20_applySdmmcProfileByName(p, sdmmc_profile);
    T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U));
    
    T20_configureRuntimeFilter(p);
    T20_syncDerivedViewState(p);
    
    return true;
}

bool T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    
    doc["version"]                         = T20::C10_Sys::VERSION_STR;
    doc["profile_name"]                    = p->runtime_cfg_profile_name;
    doc["frame_size"]                      = p->cfg.feature.frame_size;
    doc["hop_size"]                        = p->cfg.feature.hop_size;
    doc["sample_rate_hz"]                  = p->cfg.feature.sample_rate_hz;
    doc["mfcc_coeffs"]                     = p->cfg.feature.mfcc_coeffs;
    doc["sequence_frames"]                 = p->cfg.output.sequence_frames;
    doc["output_mode"]                     = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";

    doc["pre_alpha"]                       = p->cfg.preprocess.preemphasis.alpha;
    doc["filter_cutoff"]                   = p->cfg.preprocess.filter.cutoff_hz_1;
    doc["noise_mode"]                      = static_cast<int>(p->cfg.preprocess.noise.mode);
    doc["sub_strength"]                    = p->cfg.preprocess.noise.spectral_subtract_strength;

    doc["recorder_backend"]                = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
    doc["recorder_enabled"]                = p->recorder_enabled;
    doc["sdmmc_profile"]                   = p->sdmmc_profile.profile_name;
    doc["sdmmc_profile_applied"]           = p->sdmmc_profile_applied;
    doc["sdmmc_last_apply_reason"]         = p->sdmmc_last_apply_reason;

    doc["selection_sync_enabled"]          = p->selection_sync_enabled;
    doc["selection_sync_frame_from"]       = p->selection_sync_frame_from;
    doc["selection_sync_frame_to"]         = p->selection_sync_frame_to;
    doc["selection_sync_name"]             = p->selection_sync_name;
    doc["selection_sync_range_valid"]      = p->selection_sync_range_valid;
    doc["type_meta_enabled"]               = p->type_meta_enabled;
    doc["type_meta_name"]                  = p->type_meta_name;
    doc["type_meta_kind"]                  = p->type_meta_kind;

    doc["batch_watermark_low"]             = p->recorder_batch_watermark_low;
    doc["batch_watermark_high"]            = p->recorder_batch_watermark_high;
    doc["batch_idle_flush_ms"]             = p->recorder_batch_idle_flush_ms;

    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    
    serializeJson(doc, p_out_buf, p_len);
    return true;
}
