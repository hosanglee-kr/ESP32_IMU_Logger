
/* 
============================================================================
 * File: T234_Storage_Service_231.cpp
 * Summary: Storage Engine Implementation with Pre-Trigger Buffering
 * * [AI 메모: 제공 기능 요약]
 * 1. Zero-Copy DMA: 내부 SRAM 핑퐁 버퍼 3개를 순환하며 SD카드 기록 오버헤드를 극소화.
 * 2. 가변 차원 직렬화: 1축/3축 모드 및 MFCC 계수 크기에 따라 패딩을 제외한 순수 데이터만 추출(Packing)하여 파일 크기 최적화.
 * 3. Pre-Trigger Buffering: 세션이 닫혀 있을 때 PSRAM 링버퍼에 과거 N초간의 데이터를 
 * 상시 유지하다가 트리거 발생 시 일괄 기록(Flush)하여 사고 전 징후를 완벽히 캡처.
 * 4. 자동 파일 분할(Rotation): 설정된 용량(MB) 또는 시간(Min)에 도달하면 끊김 없이 
 * 새 파일로 분할하고 오래된 인덱스는 삭제.
 *
 * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. 프리-트리거 링버퍼(_pre_buf)는 크기가 크므로 반드시 MALLOC_CAP_SPIRAM(PSRAM)으로 할당해야 합니다.
 * 2. pushVector 함수는 레코딩 상태(_session_open)와 무관하게 상시 호출되어야 링버퍼가 정상 작동합니다.
 * 3. DMA 슬롯 커밋(_commitSlot) 도중 I/O 블로킹이 길어지면 FIFO 오버플로우가 날 수 있으므로 
 * idle_flush_ms 타임아웃 튜닝이 중요합니다.
 * ========================================================================== 
 */

#include "T234_Storage_Service_231.h"
#include "T219_Config_Json_231.h" // JSON 직렬화 엔진 포함
#include <ArduinoJson.h>
#include <Preferences.h> // NVS 사용
#include <time.h>        // 시간 포맷팅


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

    _written_bytes = 0;
    _session_start_ms = 0;

    memset(_active_path, 0, sizeof(_active_path));
    memset(_last_error, 0, sizeof(_last_error));
    memset(_dma_slot_used, 0, sizeof(_dma_slot_used));
    memset(_dma_slots, 0, sizeof(_dma_slots));
    memset(_index_items, 0, sizeof(_index_items));
}


// [신규 추가] 소멸자에서 PSRAM 링버퍼 메모리 반환
CL_T20_StorageService::~CL_T20_StorageService() {
    if (_pre_buf) {
        heap_caps_free(_pre_buf);
        _pre_buf = nullptr;
    }
}


bool CL_T20_StorageService::begin(const ST_T20_SdmmcProfile_t& profile) {
    _loadIndexJson();
    
    // 시스템 부팅 시 딱 한 번만 NVS를 업데이트하여 플래시 수명 파괴를 원천 차단
    Preferences prefs;
    prefs.begin(T20::C10_NVS::NAMESPACE, false);
    
    _boot_file_seq = prefs.getUInt(T20::C10_NVS::KEY_FILE_SEQ, 1);
    prefs.putUInt(T20::C10_NVS::KEY_FILE_SEQ, _boot_file_seq + 1);
    
    prefs.end();

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

void CL_T20_StorageService::setConfig(const ST_T20_Config_t& cfg) {
    _current_cfg = cfg;
    _allocatePreBuffer(); // 세션 오픈 전이라도, 설정이 주입되면 즉시 링버퍼를 메모리에 확보합니다.
}

// ============================================================================
// 1. 세션 오픈: 파일 생성 및 프리트리거 버퍼 점검
// ============================================================================

bool CL_T20_StorageService::openSession(const ST_T20_Config_t& cfg, const char* prefix) {

    if (_session_open) return false;
    _current_cfg = cfg;
    
    // 설정 갱신 및 버퍼 재확인 (이미 setConfig로 할당되어 있다면 무시됨)
    setConfig(cfg);
    
    // 매번 NVS를 읽고 쓰는 기존 코드 삭제
    _rotation_sub_seq++; // 세션이 열릴 때마다 서브 시퀀스만 증가

    // 파일명 생성 (타임스탬프 기반)
    struct tm timeinfo;
    char time_buffer[64];
    if (getLocalTime(&timeinfo, 10)) {
        // 파일명에 서브 시퀀스 번호 포함
        snprintf(time_buffer, sizeof(time_buffer), "%04lu_%03u_%04d%02d%02d_%02d%02d%02d",
                 (unsigned long)_boot_file_seq, _rotation_sub_seq,
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        snprintf(time_buffer, sizeof(time_buffer), "%04lu_%03u_notime", (unsigned long)_boot_file_seq, _rotation_sub_seq);
    }

     // 접두어(Prefix) 적용: rec_trg_0001_...bin 또는 rec_man_0002_...bin 형태
    if (_backend == EN_T20_STORAGE_SDMMC) {
        snprintf(_active_path, sizeof(_active_path), "%s/rec_%s_%s.bin", T20::C10_Path::SD_DIR_BIN, prefix, time_buffer);
    } else {
        snprintf(_active_path, sizeof(_active_path), "/fallback/rec_%s_%s.bin", prefix, time_buffer);
    }

    _active_file = (_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(_active_path, "w") : LittleFS.open(_active_path, "w");
    
    if (!_active_file) return false;

    // 확장된 바이너리 헤더 작성
    ST_T20_RecorderBinaryHeader_t header;
    memset(&header, 0, sizeof(header));
    header.magic = T20::C10_Rec::BINARY_MAGIC;
    header.version = 219; 
    header.header_size = sizeof(header);
    header.sample_rate_hz = (uint32_t)T20::C10_DSP::SAMPLE_RATE_HZ;
    header.fft_size = (uint16_t)cfg.feature.fft_size; 
    header.mfcc_dim = cfg.feature.mfcc_coeffs;       
    header.active_axes = (uint8_t)cfg.feature.axis_count; 

    String json_str;
    CL_T20_ConfigJson::buildJsonString(cfg, json_str);
    strlcpy(header.config_dump, json_str.c_str(), sizeof(header.config_dump));

    if (_active_file.write((const uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        _active_file.close();
        return false;
    }

    // Raw 저장 모드 시 3축 대응 파일 오픈
    if (cfg.storage.save_raw) {
        char raw_path[128];
        snprintf(raw_path, sizeof(raw_path), "/t20_data/raw/raw_%s_%s.bin", prefix, time_buffer);
        _raw_file = (_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(raw_path, "w") : LittleFS.open(raw_path, "w");
    }

    _record_count = 0;
    _written_bytes = sizeof(header);
    _session_start_ms = millis();
    _session_open = true;
    
    return true;
}


void CL_T20_StorageService::closeSession(const char* reason) {
    if (!_session_open) return;
    flush();

    if (_active_file) {
		_active_file.seek(offsetof(ST_T20_RecorderBinaryHeader_t, record_count));	// record_count 위치 오프셋, 안전한 offsetof 매크로 사용
		_active_file.write((const uint8_t*)&_record_count, sizeof(_record_count));

        _active_file.close();
    }
    if (_raw_file) _raw_file.close();

    _session_open = false;
    writeEvent(reason);
    _saveIndexJson();
    _handleRotation();
}



// ============================================================================
// 3. 진입점: 상시 데이터 수신 및 분기 처리
// ============================================================================
bool CL_T20_StorageService::pushVector(const ST_T20_FeatureVector_t* p_vec) {
    if (!p_vec) return false;

    // [상태 1] 레코딩 중이 아닐 때 -> 프리트리거 링버퍼에 상시 기록 (덮어쓰기)
    if (!_session_open) {
        if (_pre_capacity > 0 && _pre_buf) {
            memcpy(&_pre_buf[_pre_head], p_vec, sizeof(ST_T20_FeatureVector_t));
            _pre_head = (_pre_head + 1) % _pre_capacity;
            if (_pre_count < _pre_capacity) _pre_count++;
        }
        return true; 
    }

    // [상태 2] 세션이 열려있고, 과거 데이터가 쌓여있다면 -> 즉시 일괄 플러시
    // 트리거로 인해 방금 openSession이 호출된 직후의 첫 프레임 진입 시 동작함
    if (_pre_count > 0) {
        _flushPreBuffer();
    }

    // [상태 3] 현재 들어온 실시간 데이터 기록
    return _pushToDma(p_vec);
}

// Raw 데이터 기록 (3축 모드 시 3축 파형 통합 기록 대응 가능)
bool CL_T20_StorageService::pushRaw(const float* p_raw_x, const float* p_raw_y, const float* p_raw_z, uint16_t len, uint8_t active_axes) {
    if (!_session_open || !_raw_file || !p_raw_x) return false;

    if (active_axes == 1) {
        size_t bytes = len * sizeof(float);
        size_t w = _raw_file.write((const uint8_t*)p_raw_x, bytes);
        _written_bytes += w;
        return (w == bytes);
    } else {
        // [최적화됨] 12바이트씩 분할 쓰기 방지. 일괄 버퍼링 후 단일 I/O 기록으로 SD 병목 원천 차단.
        size_t total_bytes = len * 3 * sizeof(float);
        float* inter_buf = (float*)malloc(total_bytes);
        if (!inter_buf) return false; // 메모리 부족 시 안전 반환

        for (uint16_t i = 0; i < len; i++) {
            inter_buf[i * 3 + 0] = p_raw_x[i];
            inter_buf[i * 3 + 1] = p_raw_y[i];
            inter_buf[i * 3 + 2] = p_raw_z[i];
        }
        
        size_t total_written = _raw_file.write((const uint8_t*)inter_buf, total_bytes);
        free(inter_buf);
        
        _written_bytes += total_written;
        return (total_written == total_bytes);
    }
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

void CL_T20_StorageService::writeEvent(const char* event_msg) {
    if (event_msg) strlcpy(_last_error, event_msg, sizeof(_last_error));
}

void CL_T20_StorageService::setLastError(const char* err_msg) {
    if (err_msg) strlcpy(_last_error, err_msg, sizeof(_last_error));
}

void CL_T20_StorageService::_handleRotation() {
    if (_index_count <= _rotate_keep_max) return;

    bool any_deleted = false;
    
    // 0이면 무제한이므로 삭제 로직 수행 안함
    if (_rotate_keep_max == 0) return;
    
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


// 로테이션 감시 루틴]
void CL_T20_StorageService::checkRotation() {
    if (!_session_open) return;
    bool do_rotate = false;

    // 1. 용량 기반 (MB)
    if (_current_cfg.storage.rotation_mb > 0) {
        if (_written_bytes >= (_current_cfg.storage.rotation_mb * 1024 * 1024)) do_rotate = true;
    }
    // 2. 시간 기반 (Min)
    if (_current_cfg.storage.rotation_min > 0) {
        if ((millis() - _session_start_ms) >= (_current_cfg.storage.rotation_min * 60000)) do_rotate = true;
    }

    if (do_rotate) {
        closeSession("rotate");
        openSession(_current_cfg); // 즉시 끊김 없이 재시작
    }
}


// ============================================================================
// 2. 프리-트리거 링버퍼 메모리 동적 할당 관리
// ============================================================================
void CL_T20_StorageService::_allocatePreBuffer() {
    // pre_trigger_sec이 0으로 설정된 경우 기존 메모리 해제 로직 추가
    if (_current_cfg.storage.pre_trigger_sec == 0) {
        if (_pre_buf) {
            heap_caps_free(_pre_buf);
            _pre_buf = nullptr;
            _pre_capacity = 0;
            _pre_head = 0;
            _pre_count = 0;
        }
        return;
    }
    
    // 초당 프레임 수(FPS) = 1600Hz / Hop Size
    float fps = T20::C10_DSP::SAMPLE_RATE_HZ / (float)_current_cfg.feature.hop_size;
    uint16_t req_capacity = (uint16_t)(fps * _current_cfg.storage.pre_trigger_sec);

    // 용량이 변경되었거나 아직 할당되지 않은 경우 PSRAM에 재할당
    if (_pre_capacity != req_capacity) {
        if (_pre_buf) heap_caps_free(_pre_buf);
        // SIMD 구조체 호환을 위해 16바이트 정렬 할당 적용
        _pre_buf = (ST_T20_FeatureVector_t*)heap_caps_aligned_alloc(16, req_capacity * sizeof(ST_T20_FeatureVector_t), MALLOC_CAP_SPIRAM);
        
        if (_pre_buf) {
            _pre_capacity = req_capacity;
            _pre_head = 0;
            _pre_count = 0;
        } else {
            Serial.println(F("[Storage] Pre-Trigger PSRAM Allocation Failed!"));
            _pre_capacity = 0;
        }
    }
    
    // [상세 설명]
    // 포인터 초기화 (_pre_head = 0, _pre_count = 0)는 위처럼 메모리가 "새로 할당"될 때만 수행합니다.
    // 만약 이미 할당되어 있다면, 세션이 방금 열렸다 하더라도 버퍼 인덱스를 초기화하지 않고 그대로 둡니다.
    // 이유: 세션이 닫혀 있던 유휴 시간 동안 링버퍼에 과거의 정상/진동 데이터가 차곡차곡 쌓이고 있었으므로,
    // 세션이 열리는 즉시 이 '과거 기록 이력'을 그대로 보존하여 플러시(Flush)해야 사고 직전의 상황 분석이 가능하기 때문입니다.
}


// ============================================================================
// 4. 프리-트리거 과거 데이터 일괄 플러시
// ============================================================================
void CL_T20_StorageService::_flushPreBuffer() {
    if (_pre_count == 0 || !_pre_buf) return;

    // 링버퍼에서 가장 오래된 데이터의 인덱스 계산 (데이터가 꽉 찼으면 head가 가장 오래된 데이터)
    uint16_t oldest_idx = (_pre_count == _pre_capacity) ? _pre_head : 0;
    uint16_t count_to_flush = _pre_count;
    
    // 무한 루프나 중복 기록 방지를 위해 플래그 즉시 초기화
    _pre_count = 0; 
    _pre_head = 0;

    // 가장 오래된 과거 데이터부터 순서대로 DMA 기록 로직 태우기
    for (uint16_t i = 0; i < count_to_flush; i++) {
        uint16_t idx = (oldest_idx + i) % _pre_capacity;
        _pushToDma(&_pre_buf[idx]);
    }
    
    Serial.printf("[Storage] Flushed %d pre-trigger frames to SD.\n", count_to_flush);
}


// ============================================================================
// 5. DMA 슬롯 직렬화 및 물리적 기록 헬퍼
// ============================================================================
bool CL_T20_StorageService::_pushToDma(const ST_T20_FeatureVector_t* p_vec) {
    const uint8_t axes = p_vec->active_axes;
    const uint16_t dim_per_axis = _current_cfg.feature.mfcc_coeffs * 3; 
    const uint16_t feature_bytes = (dim_per_axis * axes * sizeof(float));
    
    // [수정됨] 누락되었던 status_flags, rms, band_energy 크기를 반영하여 총 프레임 사이즈 재계산
    const uint16_t total_frame_size = sizeof(p_vec->timestamp_ms) + 
                                      sizeof(p_vec->frame_id) + 
                                      sizeof(p_vec->active_axes) + 
                                      sizeof(p_vec->status_flags) + 
                                      sizeof(p_vec->rms) +
                                      sizeof(p_vec->band_energy) +
                                      feature_bytes;

    uint8_t slot = _dma_active_slot;
    if ((_dma_slot_used[slot] + total_frame_size) > DMA_SLOT_BYTES) {
        if (!_commitSlot(slot)) return false;
        _dma_active_slot = (slot + 1) % DMA_SLOT_COUNT;
        slot = _dma_active_slot;
        if (_dma_slot_used[slot] > 0) return false; 
    }

    uint8_t* ptr = &_dma_slots[slot][_dma_slot_used[slot]];
    
    // [수정됨] 분석 필수 데이터 누락 없이 모두 복사
    memcpy(ptr, &p_vec->timestamp_ms, sizeof(uint64_t)); ptr += sizeof(uint64_t);
    memcpy(ptr, &p_vec->frame_id, sizeof(uint32_t));     ptr += sizeof(uint32_t);
    memcpy(ptr, &p_vec->active_axes, sizeof(uint8_t));   ptr += sizeof(uint8_t);
    memcpy(ptr, &p_vec->status_flags, sizeof(uint8_t));  ptr += sizeof(uint8_t);
    memcpy(ptr, &p_vec->rms, sizeof(p_vec->rms));        ptr += sizeof(p_vec->rms);
    memcpy(ptr, &p_vec->band_energy, sizeof(p_vec->band_energy)); ptr += sizeof(p_vec->band_energy);
    
    for (uint8_t a = 0; a < axes; a++) {
        memcpy(ptr, &p_vec->features[a][0], dim_per_axis * sizeof(float));
        ptr += (dim_per_axis * sizeof(float));
    }

    _dma_slot_used[slot] += total_frame_size;
    _written_bytes += total_frame_size;
    _record_count++;
    _batch_count++;
    _last_push_ms = millis();

    if (_batch_count >= _watermark_high) flush();
    return true;
}

