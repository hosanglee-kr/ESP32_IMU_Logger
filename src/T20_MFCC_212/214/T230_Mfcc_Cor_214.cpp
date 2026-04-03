/* ============================================================================
 * File: T230_Mfcc_Cor_214.cpp
 * Summary: 메인 컨트롤러 및 RTOS 태스크 스케줄링 (v210 로직 통합)
 * * [v212 구현 및 점검 사항]
 * 1. Pimpl 패턴의 생성자/소멸자 및 자원 할당 관리
 * 2. Sensor(Core 0) / Process(Core 1) / Recorder(Core 1) 태스크 분산 배치
 * 3. 핑퐁 버퍼(Double Buffering) 인덱스 스위칭 로직 복구
 * 4. Alias Accessor를 통한 v210 상태 로직의 v212 구조체 매핑
 ============================================================================ */

#include "T221_Mfcc_Inter_214.h"

CL_T20_Mfcc* g_t20_instance = nullptr;

// --- 전방 선언(Forward Declaration) 추가 ---
void T20_sensorTask(void* p_arg);
void T20_processTask(void* p_arg);
void T20_recorderTask(void* p_arg);

// 클래스 생성자 및 소멸자 구현 확인
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

    // [Step 2] 동기화 객체 우선 생성 (Task 생성 전 필수)
    _impl->mutex = xSemaphoreCreateMutex();
    _impl->frame_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    _impl->recorder_queue = xQueueCreate(32, sizeof(ST_T20_RecorderVectorMessage_t)); // 마진 확보

    // [Step 3] 하드웨어 물리 연결 (SPI)
    _impl->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);
    
    // [Step 4] 센서 펌웨어 설정 적용 (앞에서 만든 함수 호출)
    if (!T20_bmi270_LoadProductionConfig(_impl)) {
        strlcpy(_impl->bmi270_status_text, "hw_init_fail", 48);
        return false;
    }

    // [Step 5] DSP 및 파일 시스템 준비
    T20_initDSP(_impl);
    T20_loadRuntimeConfigFile(_impl);
    T20_tryMountSdmmcRecorderBackend(_impl); // SD카드 마운트 시도

    _impl->initialized = true;
    _impl->bmi_state.init = EN_T20_STATE_DONE;
    return true;
}



bool CL_T20_Mfcc::start(void) {
    if (_impl == nullptr || !_impl->initialized || _impl->running) return false;

    // 버튼 핀 초기화
    pinMode(_impl->cfg.system.button_pin, INPUT_PULLUP);

    // 설정에 따른 초기 측정 상태 결정
    _impl->measurement_active = _impl->cfg.system.auto_start;

    // Core 분산 배치: 센서는 0번 코어, 연산 및 저장은 1번 코어
    xTaskCreatePinnedToCore(T20_sensorTask, "T20_Sens", G_T20_SENSOR_TASK_STACK, _impl, G_T20_SENSOR_TASK_PRIO, &_impl->sensor_task_handle, 0);
    xTaskCreatePinnedToCore(T20_processTask, "T20_Proc", G_T20_PROCESS_TASK_STACK, _impl, G_T20_PROCESS_TASK_PRIO, &_impl->process_task_handle, 1);
    xTaskCreatePinnedToCore(T20_recorderTask, "T20_Rec", G_T20_RECORDER_TASK_STACK, _impl, G_T20_RECORDER_TASK_PRIO, &_impl->recorder_task_handle, 1);

    _impl->running = true;
    _impl->bmi_state.runtime = EN_T20_STATE_RUNNING;
    return true;
}



void T20_sensorTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    T20_bmi270InstallDrdyHook(p);

    for (;;) {
        // DRDY 인터럽트 대기
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        
        if (!p->running) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        float sample = 0.0f;
        if (T20_bmi270ReadVectorSample(p, &sample)) {
            p->bmi270_sample_counter++;

            // [v213 개선] 순환 버퍼(Circular Buffer) 방식으로 샘플 저장
            // 256개 프레임 버퍼를 링버퍼처럼 활용
            uint16_t idx = p->active_sample_index % G_T20_FFT_SIZE;
            p->frame_buffer[0][idx] = sample; // 단일 대형 버퍼 모드로 운용 가능
            p->active_sample_index++;

            // Hop Size(예: 128) 마다 분석 태스크 통지
            // 50% Overlap 기준: 128샘플마다 256샘플 분석 수행
            if ((p->active_sample_index % p->cfg.feature.hop_size) == 0 && p->active_sample_index >= G_T20_FFT_SIZE) {
                ST_T20_FrameMessage_t msg;
                // 현재 쓰기가 완료된 위치 정보를 전달
                msg.frame_index = 0; 
                xQueueSend(p->frame_queue, &msg, 0);
            }
        }
    }
}



// 시스템 자원 초기화 로직
void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;
    p->running = false;
    p->recorder_record_count = 0;
    p->dropped_frames = 0;
    // 큐/세마포어 초기화는 begin()에서 수행하므로 여기선 상태값만 리셋
}

// 프로필 초기화
void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;
    for (int i = 0; i < G_T20_CFG_PROFILE_COUNT; i++) {
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



bool T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len) {
    if (!p || !p_frame) return false;

    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];

    // 1. 기본 MFCC 13차 추출
    T20_computeMFCC(p, p_frame, mfcc);
    
    // 2. 이력 버퍼 갱신 (Delta 계산용)
    T20_pushMfccHistory(p, mfcc, p->cfg.feature.mfcc_coeffs);
    
    // 3. Delta 및 Delta-Delta 연산
    T20_computeDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, 2, delta);
    T20_computeDeltaDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, delta2);

    // 4. 39차 벡터 통합
    T20_buildVector(mfcc, delta, delta2, p->cfg.feature.mfcc_coeffs, p->latest_feature.vector);

    // 5. 로깅 및 뷰어 업데이트 (Part 1 연결)
    if (p->recorder_enabled) {
        ST_T20_RecorderVectorMessage_t msg;
        msg.frame_id = p->viewer_last_frame_id;
        msg.vector_len = 39; // 39차 고정
        memcpy(msg.vector, p->latest_feature.vector, sizeof(msg.vector));
        
        // 더블 버퍼링 스테이지 투입
        T20_stageVectorToDmaSlot(p, &msg);
    }
    
    return true;
}








// [4-1] 39차 특징 벡터 Web 전송용 JSON 빌더
bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    if (p == nullptr || !p->latest_vector_valid) return false;

    JsonDocument doc;
    doc["frame_id"] = p->viewer_last_frame_id;
    doc["vector_len"] = p->latest_feature.vector_len; // 39

    // MFCC (0~12), Delta (13~25), Delta2 (26~38)를 분리하여 전송 (UI 시각화 용이성)
    JsonArray mfcc = doc["mfcc"].to<JsonArray>();
    for (int i = 0; i < 13; i++) mfcc.add(p->latest_feature.vector[i]);

    JsonArray delta = doc["delta"].to<JsonArray>();
    for (int i = 13; i < 26; i++) delta.add(p->latest_feature.vector[i]);

    JsonArray delta2 = doc["delta2"].to<JsonArray>();
    for (int i = 26; i < 39; i++) delta2.add(p->latest_feature.vector[i]);

    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

// [4-2] 세션 종료 리포트 빌더 (바이너리 무결성 확인용)
bool T20_buildRecorderFinalizeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    JsonDocument doc;
    doc["session_id"] = p->recorder_session_id;
    doc["final_count"] = p->recorder_record_count;
    doc["duration_ms"] = p->recorder_session_close_ms - p->recorder_session_open_ms;
    doc["status"] = (p->rec_state.write == EN_T20_STATE_ERROR) ? "fail" : "success";
    doc["last_error"] = p->recorder_last_error;

    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}


/* CL_T20_Mfcc 클래스 메서드 구현 */
void CL_T20_Mfcc::stop(void) {
    if (_impl != nullptr) {
        _impl->running = false;
        T20_stopTasks(_impl);
    }
}

// 링커 에러: T20_stopTasks 구현
void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p) {
    if (p->sensor_task_handle) { vTaskDelete(p->sensor_task_handle); p->sensor_task_handle = nullptr; }
    if (p->process_task_handle) { vTaskDelete(p->process_task_handle); p->process_task_handle = nullptr; }
    if (p->recorder_task_handle) { vTaskDelete(p->recorder_task_handle); p->recorder_task_handle = nullptr; }
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
    p->type_meta_enabled             = (bool)(doc["type_meta_enabled"] | p->type_meta_enabled);
    p->cfg.feature.hop_size          = (uint16_t)(doc["hop_size"] | p->cfg.feature.hop_size);
    p->cfg.feature.mfcc_coeffs       = (uint16_t)(doc["mfcc_coeffs"] | p->cfg.feature.mfcc_coeffs);
    p->cfg.output.sequence_frames    = (uint16_t)(doc["sequence_frames"] | p->cfg.output.sequence_frames);
    p->recorder_enabled              = (bool)(doc["recorder_enabled"] | p->recorder_enabled);
    p->selection_sync_enabled        = (bool)(doc["selection_sync_enabled"] | p->selection_sync_enabled);
    p->selection_sync_frame_from     = (uint32_t)(doc["selection_sync_frame_from"] | p->selection_sync_frame_from);
    p->selection_sync_frame_to       = (uint32_t)(doc["selection_sync_frame_to"] | p->selection_sync_frame_to);
    p->recorder_batch_watermark_low  = (uint16_t)(doc["batch_watermark_low"] | p->recorder_batch_watermark_low);
    p->recorder_batch_watermark_high = (uint16_t)(doc["batch_watermark_high"] | p->recorder_batch_watermark_high);
    p->recorder_batch_idle_flush_ms  = (uint32_t)(doc["batch_idle_flush_ms"] | p->recorder_batch_idle_flush_ms);
    
    if (p->recorder_batch_watermark_low == 0) p->recorder_batch_watermark_low = 1;
    if (p->recorder_batch_watermark_high < p->recorder_batch_watermark_low) {
        p->recorder_batch_watermark_high = p->recorder_batch_watermark_low;
    }

    if (strcmp(backend, "sdmmc") == 0)
        p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
    else
        p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;

    if (strcmp(output_mode, "sequence") == 0)
        p->cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;
    else
        p->cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;

    T20_applySdmmcProfileByName(p, sdmmc_profile);
    T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U));
    T20_configureRuntimeFilter(p);
    T20_syncDerivedViewState(p);
    return true;
}


void T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return;
    
    // 설정 변경에 따른 파생 상태 동기화 (예: 뷰어 필터 재설정 등)
    T20_updateSelectionSyncState(p);
    T20_updateTypeMetaAutoClassify(p);
}




// [1] 시퀀스 링버퍼 초기화
void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim) {
    if (p_rb == nullptr) return;
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
}

// 선택 영역 동기화 상태 업데이트
void T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return;

    if (!p->selection_sync_enabled) {
        p->selection_sync_range_valid     = false;
        p->selection_sync_effective_from = 0;
        p->selection_sync_effective_to     = 0;
        return;
    }

    if (p->selection_sync_frame_to < p->selection_sync_frame_from) {
        p->selection_sync_range_valid     = false;
        p->selection_sync_effective_from = p->selection_sync_frame_from;
        p->selection_sync_effective_to     = p->selection_sync_frame_to;
        return;
    }

    p->selection_sync_range_valid     = true;
    p->selection_sync_effective_from = p->selection_sync_frame_from;
    p->selection_sync_effective_to     = p->selection_sync_frame_to;
}




// [3] 데이터 타입 메타 자동 분류
// [2] 데이터 타입 메타 자동 분류 (39차 벡터 대응)
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
        if (p->latest_sequence_valid) {
            strlcpy(p->type_meta_auto_text, "sequence_ready", sizeof(p->type_meta_auto_text));
        } else {
            strlcpy(p->type_meta_auto_text, "sequence_warming_up", sizeof(p->type_meta_auto_text));
        }
    } else if (strcmp(p->type_meta_kind, "waveform") == 0) {
        strlcpy(p->type_meta_auto_text, "waveform_frame", sizeof(p->type_meta_auto_text));
    } else {
        strlcpy(p->type_meta_auto_text, "generic_meta", sizeof(p->type_meta_auto_text));
    }
}


bool T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    
    // [1] 시스템 기본 및 특징 추출 설정
    doc["version"]                         = G_T20_VERSION_STR;
    doc["profile_name"]                    = p->runtime_cfg_profile_name;
    doc["frame_size"]                      = p->cfg.feature.frame_size;
    doc["hop_size"]                        = p->cfg.feature.hop_size;
    doc["sample_rate_hz"]                  = p->cfg.feature.sample_rate_hz;
    doc["mfcc_coeffs"]                     = p->cfg.feature.mfcc_coeffs;
    doc["sequence_frames"]                 = p->cfg.output.sequence_frames;
    doc["output_mode"]                     = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";

    // [2] 레코더 및 스토리지 상태
    doc["recorder_backend"]                = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
    doc["recorder_enabled"]                = p->recorder_enabled;
    doc["sdmmc_profile"]                   = p->sdmmc_profile.profile_name;
    doc["sdmmc_profile_applied"]           = p->sdmmc_profile_applied;
    doc["sdmmc_last_apply_reason"]         = p->sdmmc_last_apply_reason;

    // [3] 뷰어 및 데이터 동기화 설정
    doc["selection_sync_enabled"]          = p->selection_sync_enabled;
    doc["selection_sync_frame_from"]       = p->selection_sync_frame_from;
    doc["selection_sync_frame_to"]         = p->selection_sync_frame_to;
    doc["selection_sync_name"]             = p->selection_sync_name;
    doc["selection_sync_range_valid"]      = p->selection_sync_range_valid;
    doc["selection_sync_effective_from"]   = p->selection_sync_effective_from;
    doc["selection_sync_effective_to"]     = p->selection_sync_effective_to;

    // [4] 메타데이터 및 분류 정보
    doc["type_meta_enabled"]               = p->type_meta_enabled;
    doc["type_meta_name"]                  = p->type_meta_name;
    doc["type_meta_kind"]                  = p->type_meta_kind;
    doc["type_meta_auto_text"]             = p->type_meta_auto_text;

    // [5] 로우레벨 레코더 파라미터 (디버깅용)
    doc["batch_flush_records"]             = G_T20_RECORDER_BATCH_FLUSH_RECORDS;
    doc["batch_flush_timeout_ms"]          = G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS;
    doc["batch_watermark_low"]             = p->recorder_batch_watermark_low;
    doc["batch_watermark_high"]            = p->recorder_batch_watermark_high;
    doc["batch_idle_flush_ms"]             = p->recorder_batch_idle_flush_ms;
    doc["dma_slot_count"]                  = G_T20_ZERO_COPY_DMA_SLOT_COUNT;
    doc["dma_slot_bytes"]                  = G_T20_ZERO_COPY_DMA_SLOT_BYTES;

    // [6] 버퍼 크기 검증 및 직렬화
    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    
    serializeJson(doc, p_out_buf, p_len);
    return true;
}





/* [v214] 시작/종료 제어 및 버튼 핸들링 */
void T20_handleControlInputs(CL_T20_Mfcc::ST_Impl* p) {
    if (!p) return;

    // 1. 수동 버튼 처리 (Active Low - GPIO 0)
    static uint32_t last_btn_ms = 0;
    if (digitalRead(p->cfg.system.button_pin) == LOW) {
        if (millis() - last_btn_ms > 500) { // 디바운스 500ms
            p->measurement_active = !p->measurement_active;
            last_btn_ms = millis();
            
            // 상태 변경 이벤트 기록
            T20_recorderWriteEvent(p, p->measurement_active ? "btn_start" : "btn_stop");
        }
    }
}




// [v214] 데이터 흐름 감시 
void T20_checkDataFlowWatchdog(CL_T20_Mfcc::ST_Impl* p) {
    static uint32_t last_counter = 0;
    static uint32_t last_check_ms = 0;

    if (millis() - last_check_ms > 1000) { // 1초마다 체크
        // 1초 동안 샘플 카운터가 변하지 않았다면 센서 인터페이스 정지로 판단
        if (p->measurement_active && p->bmi270_sample_counter == last_counter) {
            T20_recorderWriteEvent(p, "wdog_stall_reinit");
            T20_tryBMI270Reinit(p); // 센서 재초기화 시도
        }
        last_counter = p->bmi270_sample_counter;
        last_check_ms = millis();
    }
}

