/* ============================================================================
 * File: T234_Mfcc_Rec_212.cpp
 * Summary: SD_MMC 기반 고속 바이너리 로깅 및 파일 관리 (v210 복구)
 * * [v212 구현 및 점검 사항]
 * 1. 32-byte Aligned DMA Slot을 통한 Zero-copy Write 구현
 * 2. v210의 파일 로테이션(Rotate/Prune) 및 Fallback 로직 완전 복구
 * 3. 바이너리 헤더 및 인덱스(JSON) 자동 갱신 최적화
 * 4. SDMMC 마운트 실패 시 LittleFS로 즉시 전환되는 안정성 확보
 ============================================================================ */

#include "T221_Mfcc_Inter_213.h"

/* [v212 추가] DMA 안전 쓰기를 위한 버퍼 정렬 체크 헬퍼 */
static inline bool T20_isDmaSafe(const void* p_ptr) {
    return ((uintptr_t)p_ptr % 32 == 0);
}

bool T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || p->recorder_file_opened) return true;

    char active_path[128] = {0};
    T20_recorderSelectActivePath(p, active_path, sizeof(active_path));

    // v210 로직: 파일 존재 여부 확인 후 'a'(Append) 또는 'w'(Write) 결정
    bool exists = p->recorder_storage_backend == EN_T20_STORAGE_SDMMC ? SD_MMC.exists(active_path) : LittleFS.exists(active_path);

    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, active_path, exists ? "a" : "w");
    if (!file) {
        T20_recorderSetLastError(p, "file_open_failed");
        p->rec_state.file_io = EN_T20_STATE_ERROR;
        return false;
    }

    // 새 파일이면 v210 규격의 바이너리 헤더 작성
    if (!exists) {
        T20_writeRecorderBinaryHeader(file, &p->cfg);
    }

    file.close();
    p->recorder_file_opened = true;
    p->rec_state.file_io = EN_T20_STATE_READY;
    return true;
}

bool T20_commitDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p, uint8_t p_slot_index) {
    if (p == nullptr) return false;
    if (p_slot_index >= G_T20_ZERO_COPY_DMA_SLOT_COUNT) return false;

    uint16_t used = p->recorder_dma_slot_used[p_slot_index];
    if (used == 0) return true;

    if (!T20_recorderOpenIfNeeded(p)) return false;

    // 경로 선택 로직 보강
    const char* path = p->recorder_active_path[0] ? p->recorder_active_path : p->recorder_file_path;
    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, path, "a");
    if (!file) {
        T20_recorderSetLastError(p, "dma commit open failed");
        p->recorder_file_opened = false;
        return false;
    }

    size_t written = file.write((const uint8_t*)p->recorder_dma_slots[p_slot_index], used);
    file.close();

    if (written != used) {
        T20_recorderSetLastError(p, "dma commit write failed");
        return false;
    }

    p->recorder_dma_slot_used[p_slot_index] = 0;
    return true;
}

void T20_recorderTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_RecorderVectorMessage_t msg;

    for (;;) {
        if (p == nullptr || !p->recorder_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 큐에서 특징 벡터 수신 (200ms 타임아웃)
        if (xQueueReceive(p->recorder_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            T20_recorderBatchPush(p, &msg);

            // 실시간 상태 업데이트 (v210 호환)
            p->recorder_record_count++;

            // 워터마크 또는 명시적 플러시 요청 시 파일 저장
            if (p->recorder_flush_requested || p->recorder_batch_count >= p->recorder_batch_watermark_high) {
                T20_recorderFlushNow(p);
                T20_saveRecorderIndex(p);
            }
        } else {
            // Idle 상태에서 일정 시간 경과 시 자동 플러시 (Data Loss 방지)
            if (p->recorder_batch_count > 0 && (millis() - p->recorder_batch_last_push_ms) >= p->recorder_batch_idle_flush_ms) {
                T20_recorderFlushNow(p);
            }
        }
    }
}



bool T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
    if (!p || !p_msg) return false;
    if (p->recorder_batch_count < G_T20_RECORDER_BATCH_VECTOR_MAX) {
        p->recorder_batch_vectors[p->recorder_batch_count++] = *p_msg;
        p->recorder_batch_last_push_ms = millis();
        return true;
    }
    return false;
}

bool T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    
    // 1. 현재 배치 데이터를 DMA 슬롯을 통해 파일로 Commit
    bool ok = T20_recorderBatchFlush(p);
    
    // 2. 메타데이터(세션 시간, 레코드 수) 갱신
    p->recorder_last_flush_ms = millis();
    T20_recorderWriteMetadataHeartbeat(p);
    
    // 3. 파일 로테이션 체크 (v210 기능 보강)
    T20_recorderRotateIfNeeded(p);
    
    p->recorder_flush_requested = false;
    return ok;
}

/* ============================================================================
 * Function: T20_recorderRotateIfNeeded
 * Summary: 파일 보관 개수를 초과할 경우 가장 오래된 데이터부터 자동 삭제
 * [v213 수정 사항]
 * 1. ST_Impl의 복구된 recorder_index_count 및 recorder_index_items 참조
 * 2. 삭제된 파일에 대한 Event Logging 추가
 * 3. 리스트 Shift 후 잔여 메모리 초기화(memset)로 무결성 확보
 ============================================================================ */
bool T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. 설정된 보관 개수(Keep Max) 확인 및 안전장치
    uint16_t max_keep = p->recorder_rotate_keep_max;
    if (max_keep == 0) max_keep = G_T20_RECORDER_ROTATE_KEEP_MAX; // 기본값(8) 적용

    // 현재 저장된 파일 개수가 한계치 이하이면 즉시 종료
    if (p->recorder_index_count <= max_keep) return true;

    bool any_deleted = false;

    // 2. 한계치를 초과하는 만큼 루프를 돌며 삭제
    while (p->recorder_index_count > max_keep) {
        // 가장 오래된 항목(인덱스 0)의 경로 확보
        char old_path[128] = {0};
        strlcpy(old_path, p->recorder_index_items[0].path, sizeof(old_path));

        if (old_path[0] != 0 && old_path[0] != ' ') {
            // 실제 스토리지(SDMMC 또는 LittleFS)에서 파일 제거
            bool success = false;
            if (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) {
                success = SD_MMC.remove(old_path);
            } else {
                success = LittleFS.remove(old_path);
            }

            // 삭제 결과 기록 (디버깅 및 웹 UI 확인용)
            if (success) {
                char log_msg[160];
                snprintf(log_msg, sizeof(log_msg), "rotate_del_ok:%s", old_path);
                T20_recorderWriteEvent(p, log_msg);
            } else {
                T20_recorderSetLastError(p, "rotate_del_fail");
            }
        }

        // 3. 인덱스 리스트를 한 칸씩 앞으로 당김 (Shift Left)
        for (uint16_t i = 1; i < p->recorder_index_count; ++i) {
            p->recorder_index_items[i - 1] = p->recorder_index_items[i];
        }

        // 4. 당겨지고 남은 마지막 칸 초기화
        uint16_t last_idx = p->recorder_index_count - 1;
        memset(&p->recorder_index_items[last_idx], 0, sizeof(p->recorder_index_items[0]));

        p->recorder_index_count--;
        any_deleted = true;
    }

    // 5. 변경사항이 있다면 인덱스 파일(JSON)을 즉시 갱신
    if (any_deleted) {
        return T20_saveRecorderIndex(p);
    }

    return true;
}


bool T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || !p->recorder_session_open) return false;

    // 1. 남은 배치 데이터 강제 플러시
    T20_recorderFlushNow(p);

    // 2. 바이너리 헤더 업데이트 (Record Count 쓰기)
    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path, "r+");
    if (file) {
        // record_count 필드 위치로 이동 (ST_T20_RecorderBinaryHeader_t 구조체 참조)
        // magic(4) + version(2) + header_size(2) + sample_rate(4) + fft(2) + mfcc(2) + mel(2) + seq(2) = 20바이트 뒤
        file.seek(20); 
        uint32_t final_count = p->recorder_record_count;
        file.write((const uint8_t*)&final_count, sizeof(final_count));
        file.close();
    }

    // 3. 세션 마감
    p->recorder_enabled = false;
    return T20_recorderCloseSession(p, "end_normal");
}



// [A-1] Zero-Copy Double DMA 버퍼링 핵심 로직
bool T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
    if (p == nullptr || p_msg == nullptr) return false;

    // 현재 활성화된 슬롯 확인
    uint8_t slot = p->recorder_dma_active_slot;
    uint16_t msg_size = sizeof(p_msg->frame_id) + sizeof(p_msg->vector_len) + (sizeof(float) * p_msg->vector_len);
    
    // 현재 슬롯 용량 초과 시 슬롯 전환 및 Commit
    if ((p->recorder_dma_slot_used[slot] + msg_size) > G_T20_ZERO_COPY_DMA_SLOT_BYTES) {
        // 현재 슬롯 저장 예약
        T20_commitDmaSlotToFile(p, slot);
        
        // 다음 슬롯으로 전환 (Double Buffering)
        p->recorder_dma_active_slot = (slot + 1) % G_T20_ZERO_COPY_DMA_SLOT_COUNT;
        slot = p->recorder_dma_active_slot;
        
        // 새 슬롯이 아직 비워지지 않았다면(이전 쓰기 진행 중) 에러 처리
        if (p->recorder_dma_slot_used[slot] > 0) {
            T20_recorderSetLastError(p, "dma_overflow_busy");
            return false;
        }
    }

    // 슬롯에 데이터 복사
    uint8_t* target_ptr = &p->recorder_dma_slots[slot][p->recorder_dma_slot_used[slot]];
    memcpy(target_ptr, &p_msg->frame_id, sizeof(p_msg->frame_id));
    target_ptr += sizeof(p_msg->frame_id);
    memcpy(target_ptr, &p_msg->vector_len, sizeof(p_msg->vector_len));
    target_ptr += sizeof(p_msg->vector_len);
    memcpy(target_ptr, p_msg->vector, sizeof(float) * p_msg->vector_len);
    
    p->recorder_dma_slot_used[slot] += msg_size;
    return true;
}

// [A-2] 파일 로테이션: 오래된 파일 자동 삭제 (Prune)
void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || p->recorder_index_count <= G_T20_RECORDER_ROTATE_KEEP_MAX) return;

    // 보관 개수(8개)를 초과한 가장 오래된 파일 식별
    char old_path[128];
    strlcpy(old_path, p->recorder_index_items[0].path, sizeof(old_path));

    // 실제 파일 삭제
    bool deleted = false;
    if (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) {
        deleted = SD_MMC.remove(old_path);
    } else {
        deleted = LittleFS.remove(old_path);
    }

    if (deleted) {
        // 인덱스 리스트 Shift
        for (uint16_t i = 1; i < p->recorder_index_count; i++) {
            p->recorder_index_items[i - 1] = p->recorder_index_items[i];
        }
        p->recorder_index_count--;
        T20_saveRecorderIndex(p);
    }
}


// [1] 런타임 설정 로드 (상수명 수정: _FILE_ 제거)
bool T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    
    // 에러 수정: G_T20_RECORDER_RUNTIME_CFG_FILE_PATH -> G_T20_RECORDER_RUNTIME_CFG_PATH
    if (!LittleFS.exists(G_T20_RECORDER_RUNTIME_CFG_PATH)) return false;

    File f = LittleFS.open(G_T20_RECORDER_RUNTIME_CFG_PATH, "r");
    if (!f) return false;
    
    String json_text = f.readString();
    f.close();

    return T20_applyRuntimeConfigJsonText(p, json_text.c_str());
}

bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p) {
    if (SD_MMC.begin("/sdcard", true)) { // 1-bit mode
        p->recorder_sdmmc_mounted = true;
        p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
        return true;
    }
    return false;
}

// [2] 활성 경로 선택 (반환 타입 수정: void -> bool)
bool T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len) {
    if (p == nullptr || p_out == nullptr || p_len == 0) return false;

    if (p->recorder_file_path[0] != 0) {
        strlcpy(p_out, p->recorder_file_path, p_len);
    } else if (p->recorder_fallback_active) {
        strlcpy(p_out, G_T20_RECORDER_FALLBACK_PATH, p_len);
    } else {
        strlcpy(p_out, G_T20_RECORDER_DEFAULT_FILE_PATH, p_len);
    }

    strlcpy(p->recorder_active_path, p_out, sizeof(p->recorder_active_path));
    return true; // 헤더 선언(bool)에 맞춰 반환값 추가
}


File T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode) {
    if (p_backend == EN_T20_STORAGE_SDMMC) return SD_MMC.open(p_path, p_mode);
    return LittleFS.open(p_path, p_mode);
}

// [3] 이벤트 기록 (반환 타입 수정: void -> bool)
bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
    if (p == nullptr || p_text == nullptr) return false;

    strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
    
    // 향후 실제 파일 로그 작성이 필요하면 여기에 추가
    return true; 
}


// [5] 에러 메시지 설정 (기존 void 유지 - 헤더 선언 확인 필요)
void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
    if (p == nullptr) return;
    strlcpy(p->recorder_last_error, (p_text ? p_text : "unknown"), sizeof(p->recorder_last_error));
}

// [6] 인덱스 저장 로직 보강
bool T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    File file = LittleFS.open(G_T20_RECORDER_INDEX_FILE_PATH, "w");
    if (!file) return false;

    JsonDocument doc;
    doc["count"] = p->recorder_index_count;
    JsonArray arr = doc["items"].to<JsonArray>();
    
    for (uint16_t i = 0; i < p->recorder_index_count; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["path"] = p->recorder_index_items[i].path;
        item["record_count"] = p->recorder_index_items[i].record_count;
    }

    serializeJson(doc, file);
    file.close();
    return true;
}

bool T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p) {
    return T20_commitActiveDmaSlotToFile(p);
}

// [4] 메타데이터 하트비트 (반환 타입 수정: void -> bool)
bool T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    p->recorder_last_flush_ms = millis();
    
    // 세션 정보 갱신
    if (p->recorder_session_open && p->recorder_session_open_ms == 0) {
        p->recorder_session_open_ms = millis();
    }
    
    return true;
}

bool T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg) {
    ST_T20_RecorderBinaryHeader_t hdr = { .magic = G_T20_BINARY_MAGIC, .version = G_T20_BINARY_VERSION };
    return p_file.write((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
}



bool T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    // 현재 쓰고 있는 슬롯의 직전 슬롯(이미 다 찬 슬롯)을 flush
    uint8_t last_filled = (uint8_t)((p->recorder_dma_active_slot + G_T20_ZERO_COPY_DMA_SLOT_COUNT - 1U) % G_T20_ZERO_COPY_DMA_SLOT_COUNT);
    return T20_commitDmaSlotToFile(p, last_filled);
}













