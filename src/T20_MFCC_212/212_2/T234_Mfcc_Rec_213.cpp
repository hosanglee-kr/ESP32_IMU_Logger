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
    if (p == nullptr || p_slot_index >= G_T20_ZERO_COPY_DMA_SLOT_COUNT) return false;

    uint16_t used = p->recorder_dma_slot_used[p_slot_index];
    if (used == 0) return true;

    if (!T20_recorderOpenIfNeeded(p)) return false;

    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path, "a");
    if (!file) return false;

    // v212 핵심: DMA 정렬된 버퍼를 통째로 쓰기 (성능 극대화)
    p->rec_state.write = EN_T20_STATE_RUNNING;
    size_t written = file.write(p->recorder_dma_slots[p_slot_index], used);
    file.close();

    if (written != used) {
        T20_recorderSetLastError(p, "dma_write_incomplete");
        return false;
    }

    p->recorder_dma_slot_used[p_slot_index] = 0;
    p->rec_state.write = EN_T20_STATE_READY;
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




bool T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    // 설정된 보관 개수를 초과하면 가장 오래된 인덱스 파일 삭제
    if (p->recorder_index_count <= p->recorder_rotate_keep_max) return true;

    while (p->recorder_index_count > p->recorder_rotate_keep_max) {
        // 실제 파일 삭제 (예전 버전 기능 복구)
        const char* old_path = p->recorder_index_items[0].path;
        if (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) SD_MMC.remove(old_path);
        else LittleFS.remove(old_path);

        // 인덱스 리스트 한 칸씩 당기기
        for (uint16_t i = 1; i < p->recorder_index_count; ++i) {
            p->recorder_index_items[i - 1] = p->recorder_index_items[i];
        }
        p->recorder_index_count--;
    }
    return T20_saveRecorderIndex(p);
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

