/* ============================================================================
 * File: T234_Mfcc_Rec_216.cpp
 * Summary: SD_MMC 기반 고속 바이너리 로깅 및 파일 관리 (C++17 Namespace & 무결성 복구)
 * ========================================================================== */

#include "T221_Mfcc_Inter_216.h"




/* DMA 안전 쓰기를 위한 버퍼 정렬 체크 헬퍼 */
static inline bool T20_isDmaSafe(const void* p_ptr) {
    return ((uintptr_t)p_ptr % 32 == 0);
}

/* ============================================================================
 * 1. 레코더 라이프사이클 관리
 * ========================================================================== */

bool T20_recorderBegin(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || p->recorder_enabled) return false;

    // 로테이션 파일명 생성 (현재 시간 기반)
    snprintf(p->recorder_file_path, sizeof(p->recorder_file_path), "%s%lu%s",
         T20::C10_Path::SD_PREFIX_BIN, (unsigned long)millis(), T20::C10_Path::SD_EXT_BIN);
    // snprintf(p->recorder_file_path, sizeof(p->recorder_file_path), "%s%lu%s",
    //         T20::C10_Rec::ROTATE_PREFIX, (unsigned long)millis(), T20::C10_Rec::ROTATE_EXT);

    // 세션 및 버퍼 상태 초기화
    p->recorder_record_count = 0;
    p->recorder_batch_count = 0;
    memset(p->recorder_dma_slot_used, 0, sizeof(p->recorder_dma_slot_used));
    p->recorder_dma_active_slot = 0;

    p->recorder_session_open = true;
    p->recorder_session_open_ms = millis();
    p->recorder_file_opened = false; // 오픈은 T20_recorderOpenIfNeeded에서 지연 처리

    // 인덱스 리스트에 새 파일 등록
    if (p->recorder_index_count < T20::C10_Rec::MAX_ROTATE_LIST) {
        strlcpy(p->recorder_index_items[p->recorder_index_count].path, p->recorder_file_path, 128);
        p->recorder_index_items[p->recorder_index_count].size_bytes = 0;
        p->recorder_index_items[p->recorder_index_count].created_ms = p->recorder_session_open_ms;
        p->recorder_index_items[p->recorder_index_count].record_count = 0;
        p->recorder_index_count++;

        // 시작하자마자 인덱스 갱신
        T20_saveRecorderIndex(p);
    }

    p->recorder_enabled = true;
    T20_recorderWriteEvent(p, "recorder_started");
    return true;
}

bool T20_recorderCloseSession(CL_T20_Mfcc::ST_Impl* p, const char* p_reason) {
    if (p == nullptr) return false;

    p->recorder_session_open = false;
    p->recorder_session_close_ms = millis();
    p->recorder_file_opened = false;

    T20_recorderWriteEvent(p, p_reason ? p_reason : "session_closed");
    return true;
}

bool T20_recorderEnd(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || !p->recorder_session_open) return false;

    // 1. 남은 배치 데이터 강제 플러시
    T20_recorderFlushNow(p);

    // 2. 바이너리 헤더 업데이트 및 최종 파일 크기 동기화
    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, p->recorder_active_path, "r+");
    if (file) {
        file.seek(T20::C10_Rec::HEADER_RECORD_OFFSET);
        uint32_t final_count = p->recorder_record_count;
        file.write((const uint8_t*)&final_count, sizeof(final_count));

        // 파일 닫기 직전 실제 용량을 가져와 인덱스 최신화
        uint32_t final_size = file.size();
        file.close();

        if (p->recorder_index_count > 0) {
            p->recorder_index_items[p->recorder_index_count - 1].size_bytes = final_size;
            p->recorder_index_items[p->recorder_index_count - 1].record_count = final_count;
            T20_saveRecorderIndex(p);
        }
    }

    // 3. 세션 마감
    p->recorder_enabled = false;
    return T20_recorderCloseSession(p, "end_normal");
}

/* ============================================================================
 * 2. 디스크 I/O 및 DMA 슬롯 커밋
 * ========================================================================== */

bool T20_recorderOpenIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || p->recorder_file_opened) return true;

    char active_path[128] = {0};
    T20_recorderSelectActivePath(p, active_path, sizeof(active_path));

    bool exists = p->recorder_storage_backend == EN_T20_STORAGE_SDMMC ? SD_MMC.exists(active_path) : LittleFS.exists(active_path);

    File file = T20_openRecorderFileByBackend(p->recorder_storage_backend, active_path, exists ? "a" : "w");
    if (!file) {
        T20_recorderSetLastError(p, "file_open_failed");
        p->rec_state.file_io = EN_T20_STATE_ERROR;
        return false;
    }

    // 새 파일이면 바이너리 헤더 작성
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
    if (p_slot_index >= T20::C10_Rec::DMA_SLOT_COUNT) return false;

    uint16_t used = p->recorder_dma_slot_used[p_slot_index];
    if (used == 0) return true;

    if (!T20_recorderOpenIfNeeded(p)) return false;

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

bool T20_commitActiveDmaSlotToFile(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    uint8_t last_filled = (uint8_t)((p->recorder_dma_active_slot + T20::C10_Rec::DMA_SLOT_COUNT - 1U) % T20::C10_Rec::DMA_SLOT_COUNT);
    return T20_commitDmaSlotToFile(p, last_filled);
}

/* ============================================================================
 * 3. 최적화된 레코더 태스크 (Zero-Copy DMA 라우팅)
 * ========================================================================== */

void T20_recorderTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_RecorderVectorMessage_t msg;

    for (;;) {
        if (p == nullptr || !p->recorder_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 큐에서 39차 특징 벡터 수신 (200ms 타임아웃)
        if (xQueueReceive(p->recorder_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {

            if (T20_stageVectorToDmaSlot(p, &msg)) {
                p->recorder_record_count++;
                p->recorder_batch_count++;
                p->recorder_batch_last_push_ms = millis();
            }

            if (p->recorder_flush_requested || p->recorder_batch_count >= p->recorder_batch_watermark_high) {
                T20_recorderFlushNow(p);
                // 파일 로테이션/크기 변경 사항 동기화는 FlushNow 내부의 RotateIfNeeded에서 처리됨
            }
        } else {
            // Idle Flush (데이터 유실 방지)
            if (p->recorder_batch_count > 0 && (millis() - p->recorder_batch_last_push_ms) >= p->recorder_batch_idle_flush_ms) {
                T20_recorderFlushNow(p);
            }
        }
    }
}

// Zero-Copy Double DMA 버퍼링 핵심 로직
bool T20_stageVectorToDmaSlot(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
    if (p == nullptr || p_msg == nullptr) return false;

    uint8_t slot = p->recorder_dma_active_slot;
    uint16_t msg_size = sizeof(p_msg->frame_id) + sizeof(p_msg->vector_len) + (sizeof(float) * p_msg->vector_len);

    if ((p->recorder_dma_slot_used[slot] + msg_size) > T20::C10_Rec::DMA_SLOT_BYTES) {
        T20_commitDmaSlotToFile(p, slot);

        p->recorder_dma_active_slot = (slot + 1) % T20::C10_Rec::DMA_SLOT_COUNT;
        slot = p->recorder_dma_active_slot;

        if (p->recorder_dma_slot_used[slot] > 0) {
            T20_recorderSetLastError(p, "dma_overflow_busy");
            return false;
        }
    }

    uint8_t* target_ptr = &p->recorder_dma_slots[slot][p->recorder_dma_slot_used[slot]];
    memcpy(target_ptr, &p_msg->frame_id, sizeof(p_msg->frame_id));
    target_ptr += sizeof(p_msg->frame_id);
    memcpy(target_ptr, &p_msg->vector_len, sizeof(p_msg->vector_len));
    target_ptr += sizeof(p_msg->vector_len);
    memcpy(target_ptr, p_msg->vector, sizeof(float) * p_msg->vector_len);

    p->recorder_dma_slot_used[slot] += msg_size;
    return true;
}

bool T20_recorderBatchPush(CL_T20_Mfcc::ST_Impl* p, const ST_T20_RecorderVectorMessage_t* p_msg) {
    if (!p || !p_msg) return false;
    if (p->recorder_batch_count < T20::C10_Rec::BATCH_VECTOR_MAX) {
        p->recorder_batch_vectors[p->recorder_batch_count++] = *p_msg;
        p->recorder_batch_last_push_ms = millis();
        return true;
    }
    return false;
}

bool T20_recorderBatchFlush(CL_T20_Mfcc::ST_Impl* p) {
    return T20_commitActiveDmaSlotToFile(p);
}

bool T20_recorderFlushNow(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    bool ok = T20_recorderBatchFlush(p);

    p->recorder_last_flush_ms = millis();
    T20_recorderWriteMetadataHeartbeat(p);
    T20_recorderRotateIfNeeded(p);

    p->recorder_flush_requested = false;
    p->recorder_batch_count = 0;
    return ok;
}

/* ============================================================================
 * 4. 파일 로테이션 (오래된 데이터 관리)
 * ========================================================================== */

bool T20_recorderRotateIfNeeded(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    uint16_t max_keep = p->recorder_rotate_keep_max;
    if (max_keep == 0) max_keep = T20::C10_Rec::ROTATE_KEEP_MAX;

    if (p->recorder_index_count <= max_keep) return true;

    bool any_deleted = false;

    while (p->recorder_index_count > max_keep) {
        char old_path[128] = {0};
        strlcpy(old_path, p->recorder_index_items[0].path, sizeof(old_path));

        if (old_path[0] != 0 && old_path[0] != ' ') {
            bool success = false;
            if (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) {
                success = SD_MMC.remove(old_path);
            } else {
                success = LittleFS.remove(old_path);
            }

            if (success) {
                char log_msg[160];
                snprintf(log_msg, sizeof(log_msg), "rotate_del_ok:%s", old_path);
                T20_recorderWriteEvent(p, log_msg);
            } else {
                T20_recorderSetLastError(p, "rotate_del_fail");
            }
        }

        for (uint16_t i = 1; i < p->recorder_index_count; ++i) {
            p->recorder_index_items[i - 1] = p->recorder_index_items[i];
        }

        uint16_t last_idx = p->recorder_index_count - 1;
        memset(&p->recorder_index_items[last_idx], 0, sizeof(p->recorder_index_items[0]));

        p->recorder_index_count--;
        any_deleted = true;
    }

    if (any_deleted) {
        return T20_saveRecorderIndex(p);
    }
    return true;
}

void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr || p->recorder_index_count <= T20::C10_Rec::ROTATE_KEEP_MAX) return;

    char old_path[128];
    strlcpy(old_path, p->recorder_index_items[0].path, sizeof(old_path));

    bool deleted = false;
    if (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) {
        deleted = SD_MMC.remove(old_path);
    } else {
        deleted = LittleFS.remove(old_path);
    }

    if (deleted) {
        for (uint16_t i = 1; i < p->recorder_index_count; i++) {
            p->recorder_index_items[i - 1] = p->recorder_index_items[i];
        }
        p->recorder_index_count--;
        T20_saveRecorderIndex(p);
    }
}

/* ============================================================================
 * 5. 환경 설정 및 인덱스 관리 유틸리티
 * ========================================================================== */

// 시스템 재부팅 시 기존 저장된 파일 목록을 읽어오는 로직
bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    if (!LittleFS.exists(T20::C10_Path::LFS_FILE_REC_IDX_JSON)) return false;

    File file = LittleFS.open(T20::C10_Path::LFS_FILE_REC_IDX_JSON, "r");
    if (!file) return false;

    String json_text = file.readString();
    file.close();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_text);
    if (err) return false;

    p->recorder_index_count = doc["count"] | 0;
    JsonArray arr = doc["items"].as<JsonArray>();

    uint16_t i = 0;
    for (JsonObject item : arr) {
        if (i >= T20::C10_Rec::MAX_ROTATE_LIST) break;
        strlcpy(p->recorder_index_items[i].path, item["path"] | "", sizeof(p->recorder_index_items[i].path));
        p->recorder_index_items[i].record_count = item["record_count"] | 0;
        p->recorder_index_items[i].size_bytes = item["size_bytes"] | 0;
        p->recorder_index_items[i].created_ms = item["created_ms"] | 0;
        i++;
    }
    return true;
}

bool T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    File file = LittleFS.open(T20::C10_Path::LFS_FILE_REC_IDX_JSON, "w");
    if (!file) return false;

    JsonDocument doc;
    doc["count"] = p->recorder_index_count;
    JsonArray arr = doc["items"].to<JsonArray>();

    for (uint16_t i = 0; i < p->recorder_index_count; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["path"] = p->recorder_index_items[i].path;
        item["record_count"] = p->recorder_index_items[i].record_count;
        item["size_bytes"] = p->recorder_index_items[i].size_bytes;
        item["created_ms"] = p->recorder_index_items[i].created_ms;
    }

    serializeJson(doc, file);
    file.close();
    return true;
}

bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) {
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["count"] = p->recorder_index_count;
    JsonArray arr = doc["items"].to<JsonArray>();

    for (uint16_t i = 0; i < p->recorder_index_count; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["path"] = p->recorder_index_items[i].path;
        item["record_count"] = p->recorder_index_items[i].record_count;
        item["size_bytes"] = p->recorder_index_items[i].size_bytes;
        item["created_ms"] = p->recorder_index_items[i].created_ms;
    }

    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;

    serializeJson(doc, p_out_buf, p_len);
    return true;
}

bool T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    if (!LittleFS.exists(T20::C10_Path::LFS_FILE_CFG_JSON)) return false;
    // if (!LittleFS.exists(T20::C10_Rec::RUNTIME_CFG_PATH)) return false;

    File f = LittleFS.open(T20::C10_Path::LFS_FILE_CFG_JSON, "r");
    // File f = LittleFS.open(T20::C10_Rec::RUNTIME_CFG_PATH, "r");
    if (!f) return false;

    String json_text = f.readString();
    f.close();

    return T20_applyRuntimeConfigJsonText(p, json_text.c_str());
}

bool T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    char json[T20::C10_Web::JSON_BUF_SIZE] = {0};
    if (!T20_buildRuntimeConfigJsonText(p, json, sizeof(json))) return false;

    File file = LittleFS.open(T20::C10_Path::LFS_FILE_REC_IDX_JSON, "w");
    // File file = LittleFS.open(T20::C10_Rec::RUNTIME_CFG_PATH, "w");

    if (!file) return false;

    file.print(json);
    file.close();
    return true;
}



bool T20_recorderSelectActivePath(CL_T20_Mfcc::ST_Impl* p, char* p_out, uint16_t p_len) {
    if (p == nullptr || p_out == nullptr || p_len == 0) return false;

    if (p->recorder_file_path[0] != 0) {
        strlcpy(p_out, p->recorder_file_path, p_len);
    } else if (p->recorder_fallback_active) {
        strlcpy(p_out, T20::C10_Path::LFS_FILE_FALLBACK, p_len);
    } else {
        strlcpy(p_out, T20::C10_Path::LFS_FILE_DEFAULT, p_len);
    }


    /*
    } else if (p->recorder_fallback_active) {
        strlcpy(p_out, T20::C10_Rec::FALLBACK_PATH, p_len);
    } else {
        strlcpy(p_out, T20::C10_Rec::DEFAULT_PATH, p_len);
    }
    */

    strlcpy(p->recorder_active_path, p_out, sizeof(p->recorder_active_path));
    return true;
}

File T20_openRecorderFileByBackend(EM_T20_StorageBackend_t p_backend, const char* p_path, const char* p_mode) {
    if (p_backend == EN_T20_STORAGE_SDMMC) return SD_MMC.open(p_path, p_mode);
    return LittleFS.open(p_path, p_mode);
}

bool T20_recorderWriteEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
    if (p == nullptr || p_text == nullptr) return false;
    strlcpy(p->recorder_last_error, p_text, sizeof(p->recorder_last_error));
    return true;
}

void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text) {
    if (p == nullptr) return;
    strlcpy(p->recorder_last_error, (p_text ? p_text : "unknown"), sizeof(p->recorder_last_error));
}

bool T20_recorderWriteMetadataHeartbeat(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    p->recorder_last_flush_ms = millis();

    if (p->recorder_session_open && p->recorder_session_open_ms == 0) {
        p->recorder_session_open_ms = millis();
    }
    return true;
}

bool T20_writeRecorderBinaryHeader(File& p_file, const ST_T20_Config_t* p_cfg) {
    if (!p_file || !p_cfg) return false;

    ST_T20_RecorderBinaryHeader_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic = T20::C10_Rec::BINARY_MAGIC;
    hdr.version = T20::C10_Rec::BINARY_VERSION;
    hdr.header_size = sizeof(ST_T20_RecorderBinaryHeader_t);

    hdr.sample_rate_hz = (uint32_t)p_cfg->feature.sample_rate_hz;
    hdr.fft_size = p_cfg->feature.fft_size;
    hdr.mfcc_dim = p_cfg->feature.mfcc_coeffs;
    hdr.mel_filters = p_cfg->feature.mel_filters;
    hdr.sequence_frames = p_cfg->output.sequence_frames;

    hdr.record_count = 0;

    return p_file.write((const uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
}

bool T20_applySdmmcProfileByName(CL_T20_Mfcc::ST_Impl* p, const char* p_name) {
    if (p == nullptr || p_name == nullptr) return false;
    for (uint16_t i = 0; i < T20::C10_Rec::SDMMC_PROFILE_COUNT; ++i) {
        if (p->sdmmc_profiles[i].enabled && strcmp(p->sdmmc_profiles[i].profile_name, p_name) == 0) {
            p->sdmmc_profile = p->sdmmc_profiles[i];
            bool ok = T20_applySdmmcProfilePins(p);
            T20_saveRuntimeConfigFile(p);
            return ok;
        }
    }
    p->sdmmc_profile_applied = false;
    strlcpy(p->sdmmc_last_apply_reason, "profile not found", sizeof(p->sdmmc_last_apply_reason));
    return false;
}

bool T20_applySdmmcProfilePins(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    bool valid_basic =
        (p->sdmmc_profile.clk_pin == T20::C10_Pin::PIN_UNASSIGNED &&
         p->sdmmc_profile.cmd_pin == T20::C10_Pin::PIN_UNASSIGNED &&
         p->sdmmc_profile.d0_pin == T20::C10_Pin::PIN_UNASSIGNED) ||
        (p->sdmmc_profile.clk_pin != T20::C10_Pin::PIN_UNASSIGNED &&
         p->sdmmc_profile.cmd_pin != T20::C10_Pin::PIN_UNASSIGNED &&
         p->sdmmc_profile.d0_pin != T20::C10_Pin::PIN_UNASSIGNED);

    if (!valid_basic) {
        p->sdmmc_profile_applied = false;
        strlcpy(p->sdmmc_last_apply_reason, "invalid basic pin trio", sizeof(p->sdmmc_last_apply_reason));
        return false;
    }

    if (!p->sdmmc_profile.use_1bit_mode) {
        bool valid_4bit =
            (p->sdmmc_profile.d1_pin != T20::C10_Pin::PIN_UNASSIGNED) &&
            (p->sdmmc_profile.d2_pin != T20::C10_Pin::PIN_UNASSIGNED) &&
            (p->sdmmc_profile.d3_pin != T20::C10_Pin::PIN_UNASSIGNED);
        if (!valid_4bit) {
            p->sdmmc_profile_applied = false;
            strlcpy(p->sdmmc_last_apply_reason, "4bit pins incomplete", sizeof(p->sdmmc_last_apply_reason));
            return false;
        }
    }

    p->sdmmc_profile_applied = true;
    strlcpy(p->sdmmc_last_apply_reason, "profile accepted", sizeof(p->sdmmc_last_apply_reason));
    return true;
}

bool T20_tryMountSdmmcRecorderBackend(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    if (p->sdmmc_profile_applied && p->sdmmc_profile.clk_pin != T20::C10_Pin::PIN_UNASSIGNED) {
        SD_MMC.setPins(
               p->sdmmc_profile.clk_pin,
               p->sdmmc_profile.cmd_pin,
               p->sdmmc_profile.d0_pin,
               p->sdmmc_profile.d1_pin,
               p->sdmmc_profile.d2_pin,
               p->sdmmc_profile.d3_pin
        );
    }

    // 설정된 모드(1-bit 또는 4-bit)를 반영하여 마운트 시도
    if (SD_MMC.begin(T20::C10_Path::MOUNT_SDMMC, p->sdmmc_profile.use_1bit_mode)) {
        p->recorder_sdmmc_mounted = true;
        p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;

        // SD 카드 초기 폴더 트리 생성
        if (!SD_MMC.exists(T20::C10_Path::SD_DIR_ROOT)) SD_MMC.mkdir(T20::C10_Path::SD_DIR_ROOT);
        if (!SD_MMC.exists(T20::C10_Path::SD_DIR_BIN))  SD_MMC.mkdir(T20::C10_Path::SD_DIR_BIN);
        if (!SD_MMC.exists(T20::C10_Path::SD_DIR_CSV))  SD_MMC.mkdir(T20::C10_Path::SD_DIR_CSV);
        if (!SD_MMC.exists(T20::C10_Path::SD_DIR_LOG))  SD_MMC.mkdir(T20::C10_Path::SD_DIR_LOG);

        // 현재 모드에 맞게 명확한 마운트 성공 로그 기록
        if (p->sdmmc_profile.use_1bit_mode) {
            T20_recorderWriteEvent(p, "sd_mount_1bit_ok");
        } else {
            T20_recorderWriteEvent(p, "sd_mount_4bit_ok");
        }
        return true;
    }

    p->recorder_fallback_active = true;
    T20_recorderWriteEvent(p, "sd_mount_fail_fallback_fs");
    return false;
}
