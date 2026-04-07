/* ============================================================================
 * File: T234_Storage_Service_217.cpp
 * Summary: Data Logging & Storage Management Engine (v217)
 * Compiler: gnu++17 / Zero-Copy DMA Optimized
 * ========================================================================== */

#include "T234_Storage_Service_217.h"
#include <ArduinoJson.h>

CL_T20_StorageService::CL_T20_StorageService() {
    _session_open = false;
    _record_count = 0;
    _backend = EN_T20_STORAGE_LITTLEFS;
    memset(_active_path, 0, sizeof(_active_path));
}

bool CL_T20_StorageService::begin(const ST_T20_SdmmcProfile_t& profile) {
    // [1] SD_MMC 핀 설정 (ESP32-S3 전용)
    if (profile.clk_pin != T20::C10_Sys::PIN_NOT_SET) {
        SD_MMC.setPins(profile.clk_pin, profile.cmd_pin, profile.d0_pin, 
                       profile.d1_pin, profile.d2_pin, profile.d3_pin);
    }

    // [2] SD_MMC 마운트 시도 (1-bit / 4-bit 모드 반영)
    if (SD_MMC.begin(T20::C10_Path::MOUNT_SD, profile.use_1bit_mode)) {
        _backend = EN_T20_STORAGE_SDMMC;
        
        // 필수 폴더 구조 생성
        if (!SD_MMC.exists(T20::C10_Path::SD_DIR_BIN)) {
            SD_MMC.mkdir("/t20_data");
            SD_MMC.mkdir(T20::C10_Path::SD_DIR_BIN);
        }
    } else {
        // SD 마운트 실패 시 LittleFS로 Fallback
        _backend = EN_T20_STORAGE_LITTLEFS;
        LittleFS.begin(true);
    }

    return true;
}

bool CL_T20_StorageService::openSession(const ST_T20_RecorderBinaryHeader_t& header) {
    if (_session_open) return false;

    // [1] 파일 경로 생성 (현재 시간 기반 밀리초 접미사)
    snprintf(_active_path, sizeof(_active_path), "%s%lu.bin", 
             T20::C10_Path::SD_PREFIX_BIN, (unsigned long)millis());

    // [2] 파일 열기
    _active_file = (_backend == EN_T20_STORAGE_SDMMC) ? 
                   SD_MMC.open(_active_path, FILE_WRITE) : 
                   LittleFS.open(_active_path, FILE_WRITE);

    if (!_active_file) return false;

    // [3] 바이너리 헤더 작성
    size_t written = _active_file.write((const uint8_t*)&header, sizeof(header));
    if (written != sizeof(header)) {
        _active_file.close();
        return false;
    }

    _record_count = 0;
    _session_open = true;
    return true;
}

bool CL_T20_StorageService::writeRecord(const void* p_data, size_t len) {
    if (!_session_open || !_active_file || !p_data) return false;

    // SD_MMC 및 LittleFS의 쓰기 효율을 위해 DMA 정렬 상태 유지 권장
    size_t written = _active_file.write((const uint8_t*)p_data, len);
    
    if (written == len) {
        _record_count++;
        return true;
    }
    return false;
}

void CL_T20_StorageService::closeSession() {
    if (!_session_open) return;

    // [1] 헤더의 레코드 카운트 업데이트 (v216 로직 정합성 유지)
    // T210에 정의된 오프셋 위치로 이동하여 최종 카운트 기록
    uint32_t header_record_offset = 20U; // version, sample_rate 등 이후 위치
    _active_file.seek(header_record_offset);
    _active_file.write((const uint8_t*)&_record_count, sizeof(_record_count));

    _active_file.close();
    _session_open = false;

    // [2] 인덱스 파일 갱신
    saveIndexJson();
}

void CL_T20_StorageService::handleRotation(uint16_t keep_max) {
    // LittleFS의 recorder_index.json을 읽어 오래된 파일 삭제
    if (!LittleFS.exists(T20::C10_Path::FILE_REC_IDX)) return;

    File idx_file = LittleFS.open(T20::C10_Path::FILE_REC_IDX, "r");
    if (!idx_file) return;

    JsonDocument doc;
    deserializeJson(doc, idx_file);
    idx_file.close();

    JsonArray items = doc["items"].as<JsonArray>();
    
    // 개수 초과 시 가장 오래된 것(앞부분) 삭제
    while (items.size() >= keep_max && items.size() > 0) {
        const char* old_path = items[0]["path"];
        if (_backend == EN_T20_STORAGE_SDMMC) SD_MMC.remove(old_path);
        else LittleFS.remove(old_path);
        
        items.remove(0);
    }

    // 변경된 인덱스 다시 저장
    File save_file = LittleFS.open(T20::C10_Path::FILE_REC_IDX, "w");
    if (save_file) {
        serializeJson(doc, save_file);
        save_file.close();
    }
}

bool CL_T20_StorageService::saveIndexJson() {
    // 현재 열려있는 파일의 정보를 인덱스에 추가
    JsonDocument doc;
    if (LittleFS.exists(T20::C10_Path::FILE_REC_IDX)) {
        File f = LittleFS.open(T20::C10_Path::FILE_REC_IDX, "r");
        deserializeJson(doc, f);
        f.close();
    }

    JsonArray items = doc["items"].to<JsonArray>();
    
    // 새 아이템 추가
    JsonObject newItem = items.add<JsonObject>();
    newItem["path"] = _active_path;
    newItem["record_count"] = _record_count;
    newItem["size_bytes"] = (_backend == EN_T20_STORAGE_SDMMC) ? 
                             SD_MMC.open(_active_path).size() : 
                             LittleFS.open(_active_path).size();
    newItem["created_ms"] = millis();

    File f = LittleFS.open(T20::C10_Path::FILE_REC_IDX, "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
        return true;
    }
    return false;
}

