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

