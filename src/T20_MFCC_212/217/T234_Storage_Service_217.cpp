/* ============================================================================
 * File: T234_Storage_Service_217.cpp
 * Summary: Storage Engine Implementation (v217)
 * ========================================================================== */

#include "T234_Storage_Service_217.h"
#include <ArduinoJson.h>

CL_T20_StorageService::CL_T20_StorageService() {
    _session_open = false;
    _record_count = 0;
    _backend = EN_T20_STORAGE_LITTLEFS;
    _dma_active_slot = 0;
    _batch_count = 0;
    _last_push_ms = 0;
    _watermark_high = 8;     // T20::C10_Rec::BATCH_WMARK_HIGH
    _idle_flush_ms = 250;    // T20::C10_Rec::BATCH_IDLE_FLUSH_MS
    _index_count = 0;
    _rotate_keep_max = 8;    // T20::C10_Rec::ROTATE_KEEP_MAX
    
    memset(_active_path, 0, sizeof(_active_path));
    memset(_last_error, 0, sizeof(_last_error));
    memset(_dma_slot_used, 0, sizeof(_dma_slot_used));
    memset(_dma_slots, 0, sizeof(_dma_slots));
    memset(_index_items, 0, sizeof(_index_items));
}

bool CL_T20_StorageService::begin(const ST_T20_SdmmcProfile_t& profile) {
    _loadIndexJson();

    if (profile.clk_pin != 0xFFU) { // PIN_NOT_SET
        SD_MMC.setPins(profile.clk_pin, profile.cmd_pin, profile.d0_pin, 
                       profile.d1_pin, profile.d2_pin, profile.d3_pin);
    }

    if (SD_MMC.begin("/sdcard", profile.use_1bit_mode)) {
        _backend = EN_T20_STORAGE_SDMMC;
        if (!SD_MMC.exists("/t20_data")) SD_MMC.mkdir("/t20_data");
        if (!SD_MMC.exists("/t20_data/bin")) SD_MMC.mkdir("/t20_data/bin");
        writeEvent(profile.use_1bit_mode ? "sd_mount_1bit_ok" : "sd_mount_4bit_ok");
    } else {
        _backend = EN_T20_STORAGE_LITTLEFS;
        LittleFS.begin(true);
        if (!LittleFS.exists("/fallback")) LittleFS.mkdir("/fallback");
        writeEvent("sd_mount_fail_fallback_fs");
    }
    return true;
}

bool CL_T20_StorageService::openSession(const ST_T20_RecorderBinaryHeader_t& header) {
    if (_session_open) return false;

    // 경로 생성 로직 (Fallback 포함)
    if (_backend == EN_T20_STORAGE_SDMMC) {
        snprintf(_active_path, sizeof(_active_path), "/t20_data/bin/rec_%lu.bin", (unsigned long)millis());
    } else {
        snprintf(_active_path, sizeof(_active_path), "/fallback/rec_%lu.bin", (unsigned long)millis());
    }

    _active_file = (_backend == EN_T20_STORAGE_SDMMC) ? 
                   SD_MMC.open(_active_path, "w") : 
                   LittleFS.open(_active_path, "w");

    if (!_active_file) {
        setLastError("file_open_failed");
        return false;
    }

    if (_active_file.write((const uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        setLastError("header_write_failed");
        _active_file.close();
        return false;
    }

    _record_count = 0;
    _batch_count = 0;
    _dma_active_slot = 0;
    memset(_dma_slot_used, 0, sizeof(_dma_slot_used));
    
    _session_open = true;
    writeEvent("recorder_started");
    return true;
}

bool CL_T20_StorageService::pushVector(const ST_T20_FeatureVector_t* p_vec) {
    if (!_session_open || !p_vec) return false;

    uint8_t slot = _dma_active_slot;
    uint16_t msg_size = sizeof(p_vec->frame_id) + sizeof(p_vec->vector_len) + (sizeof(float) * p_vec->vector_len);

    // 슬롯 용량 초과 시 디스크 기록 후 슬롯 전환
    if ((_dma_slot_used[slot] + msg_size) > DMA_SLOT_BYTES) {
        if (!_commitSlot(slot)) return false;

        _dma_active_slot = (slot + 1) % DMA_SLOT_COUNT;
        slot = _dma_active_slot;

        if (_dma_slot_used[slot] > 0) {
            setLastError("dma_overflow_busy");
            return false; // I/O 지연으로 인한 프레임 드롭
        }
    }

    // 메모리 복사 (직렬화)
    uint8_t* target_ptr = &_dma_slots[slot][_dma_slot_used[slot]];
    memcpy(target_ptr, &p_vec->frame_id, sizeof(p_vec->frame_id));
    target_ptr += sizeof(p_vec->frame_id);
    
    memcpy(target_ptr, &p_vec->vector_len, sizeof(p_vec->vector_len));
    target_ptr += sizeof(p_vec->vector_len);
    
    memcpy(target_ptr, p_vec->vector, sizeof(float) * p_vec->vector_len);

    _dma_slot_used[slot] += msg_size;
    _record_count++;
    _batch_count++;
    _last_push_ms = millis();

    // High Watermark 도달 시 플러시
    if (_batch_count >= _watermark_high) {
        flush();
    }
    return true;
}

bool CL_T20_StorageService::flush() {
    if (!_session_open) return false;
    bool ok = _commitSlot(_dma_active_slot);
    _batch_count = 0;
    return ok;
}

void CL_T20_StorageService::checkIdleFlush() {
    if (_session_open && _batch_count > 0 && (millis() - _last_push_ms) >= _idle_flush_ms) {
        flush();
    }
}

bool CL_T20_StorageService::_commitSlot(uint8_t slot_idx) {
    uint16_t used = _dma_slot_used[slot_idx];
    if (used == 0) return true;
    if (!_active_file) return false;

    size_t written = _active_file.write((const uint8_t*)_dma_slots[slot_idx], used);
    if (written != used) {
        setLastError("dma_commit_write_failed");
        return false;
    }
    _dma_slot_used[slot_idx] = 0;
    return true;
}

void CL_T20_StorageService::closeSession(const char* reason) {
    if (!_session_open) return;

    flush();

    // 레코드 카운트 업데이트 (오프셋 20 = Magic(4)+Version(2)+HeaderSize(2)+SampleRate(4)+FFT(2)+MFCCDim(2)+Mel(2)+Seq(2))
    if (_active_file) {
        _active_file.seek(20);
        _active_file.write((const uint8_t*)&_record_count, sizeof(_record_count));
        _active_file.close();
    }

    _session_open = false;
    writeEvent(reason);
    
    _saveIndexJson();
    _handleRotation();
}

void CL_T20_StorageService::writeEvent(const char* event_msg) {
    if (event_msg) strlcpy(_last_error, event_msg, sizeof(_last_error));
}

void CL_T20_StorageService::setLastError(const char* err_msg) {
    if (err_msg) strlcpy(_last_error, err_msg, sizeof(_last_error));
}

void CL_T20_StorageService::_handleRotation() {
    if (_index_count <= _rotate_keep_max) return;

    bool any_deleted = false;
    while (_index_count > _rotate_keep_max) {
        char old_path[128];
        strlcpy(old_path, _index_items[0].path, sizeof(old_path));

        bool success = (_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.remove(old_path) : LittleFS.remove(old_path);
        
        if (success) {
            for (uint16_t i = 1; i < _index_count; ++i) {
                _index_items[i - 1] = _index_items[i];
            }
            _index_count--;
            any_deleted = true;
        } else {
            setLastError("rotate_del_fail");
            break;
        }
    }

    if (any_deleted) _saveIndexJson();
}

bool CL_T20_StorageService::_saveIndexJson() {
    if (_index_count < MAX_ROTATE_LIST) {
        strlcpy(_index_items[_index_count].path, _active_path, 128);
        _index_items[_index_count].record_count = _record_count;
        _index_items[_index_count].size_bytes = (_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(_active_path, "r").size() : LittleFS.open(_active_path, "r").size();
        _index_items[_index_count].created_ms = millis();
        _index_count++;
    }

    File file = LittleFS.open("/sys/recorder_index.json", "w");
    if (!file) return false;

    JsonDocument doc;
    doc["count"] = _index_count;
    JsonArray arr = doc["items"].to<JsonArray>();

    for (uint16_t i = 0; i < _index_count; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["path"] = _index_items[i].path;
        item["record_count"] = _index_items[i].record_count;
        item["size_bytes"] = _index_items[i].size_bytes;
        item["created_ms"] = _index_items[i].created_ms;
    }

    serializeJson(doc, file);
    file.close();
    return true;
}

bool CL_T20_StorageService::_loadIndexJson() {
    if (!LittleFS.exists("/sys/recorder_index.json")) return false;
    File file = LittleFS.open("/sys/recorder_index.json", "r");
    if (!file) return false;

    JsonDocument doc;
    if (deserializeJson(doc, file)) { file.close(); return false; }
    file.close();

    _index_count = doc["count"] | 0;
    JsonArray arr = doc["items"].as<JsonArray>();

    uint16_t i = 0;
    for (JsonObject item : arr) {
        if (i >= MAX_ROTATE_LIST) break;
        strlcpy(_index_items[i].path, item["path"] | "", 128);
        _index_items[i].record_count = item["record_count"] | 0;
        _index_items[i].size_bytes = item["size_bytes"] | 0;
        _index_items[i].created_ms = item["created_ms"] | 0;
        i++;
    }
    return true;
}
