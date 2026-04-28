/* ============================================================================
 * File: T460_Storage_005.cpp
 * Summary: Storage Engine Implementation with Pre-Trigger & Fail-Safe
 * Description: Zero-copy DMA 슬롯, Atomic File Rotation, SD 캐시 바운스 구현.
 * ========================================================================== */
#include "T460_Storage_005.hpp"
#include <ArduinoJson.h>
#include <Preferences.h>

T460_StorageManager::T460_StorageManager() {
    _lock = xSemaphoreCreateRecursiveMutex(); 
    _sessionOpen = false;
    _ioError = false;
    _recordCount = 0;
    _dmaActiveSlot = 0;
    _batchCount = 0;
    _lastPushMs = 0;
    _indexCount = 0;
    _writtenBytes = 0;
    _sessionStartMs = 0;

    memset(_activePath, 0, sizeof(_activePath));
    memset(_activeRawPath, 0, sizeof(_activeRawPath)); 
    memset(_lastError, 0, sizeof(_lastError));
    memset(_currentPrefix, 0, sizeof(_currentPrefix)); 
    memset(_dmaSlotUsed, 0, sizeof(_dmaSlotUsed));
    memset(_indexItems, 0, sizeof(_indexItems));
    
    _preFeatBuf = nullptr;
    _preRawBuf = nullptr;
    _bounceBuf = nullptr;
}

T460_StorageManager::~T460_StorageManager() {
    if (_lock) vSemaphoreDelete(_lock);
    if (_preFeatBuf) heap_caps_free(_preFeatBuf);
    if (_preRawBuf)  heap_caps_free(_preRawBuf);
    if (_bounceBuf)  heap_caps_free(_bounceBuf); 
}

bool T460_StorageManager::init() {
    if (!LittleFS.begin(true)) {
        snprintf(_lastError, sizeof(_lastError), "LITTLEFS_MOUNT_FAIL");
        return false;
    }

    // 1. 기존 인덱스 로드 
    _loadIndexJson();
    
    // 2. 파일 시퀀스 관리 (NVS)
    Preferences prefs;
    prefs.begin("SMEA_NVS", false);
    _bootFileSeq = prefs.getUInt("file_seq", 1);
    prefs.putUInt("file_seq", _bootFileSeq + 1);
    prefs.end();
    
    // 바운스 버퍼를 안전하게 1회만 정적 할당
    if (!_bounceBuf) {
        _bounceBuf = (float*)heap_caps_malloc(SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        if (!_bounceBuf) {
            snprintf(_lastError, sizeof(_lastError), "BOUNCE_BUF_OOM");
            return false;
        }
    }

    // 3. SDMMC 마운트 
    if (SD_MMC.begin("/sdcard", true)) { 
        if (!SD_MMC.exists(SmeaConfig::Path::DIR_DATA_DEF)) SD_MMC.mkdir(SmeaConfig::Path::DIR_DATA_DEF);
        if (!SD_MMC.exists(SmeaConfig::Path::DIR_RAW_DEF))  SD_MMC.mkdir(SmeaConfig::Path::DIR_RAW_DEF);
    } else {
        snprintf(_lastError, sizeof(_lastError), "SD_MOUNT_FAIL");
        return false;
    }

    _allocatePreBuffer();
    return true;
}

void T460_StorageManager::_allocatePreBuffer() {
    // 런타임 동적 설정 가져오기
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // 오버랩(Overlap) 환경에 맞는 정확한 1초당 프레임 수 도출
    uint32_t v_fps = (uint32_t)(1000.0f / v_cfg.dsp.hop_ms); 
    uint16_t v_reqCapacity = v_fps * v_cfg.storage.pre_trigger_sec; 

    // 동적으로 용량이 변경되었을 때만 재할당 수행
    if (_preCapacity != v_reqCapacity) {
        if (_preFeatBuf) heap_caps_free(_preFeatBuf);
        if (_preRawBuf)  heap_caps_free(_preRawBuf);

        // Feature + Raw PSRAM 할당
        _preFeatBuf = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, v_reqCapacity * sizeof(SmeaType::FeatureSlot), MALLOC_CAP_SPIRAM);
        _preRawBuf  = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, v_reqCapacity * sizeof(SmeaType::RawDataSlot), MALLOC_CAP_SPIRAM);
        
        if (_preFeatBuf && _preRawBuf) {
            _preCapacity = v_reqCapacity;
            _preHeadFeat = 0; _preCountFeat = 0;
            _preHeadRaw  = 0; _preCountRaw  = 0;
        } else {
            _preCapacity = 0;
        }
    }
}


bool T460_StorageManager::openSession(const char* p_prefix) {
    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    
    // 세션을 열 때 최신 동적 설정을 기반으로 프리트리거 용량이 변경되었는지 확인 및 적용
    _allocatePreBuffer(); 
    
    if (_sessionOpen) {
        xSemaphoreGiveRecursive(_lock);
        return false;
    }
    
    _rotationSubSeq++; 
    _ioError = false; 
    strlcpy(_currentPrefix, p_prefix, sizeof(_currentPrefix));

    snprintf(_activePath, sizeof(_activePath), "%s/%s_%04u_%03u.bin", 
             SmeaConfig::Path::DIR_DATA_DEF, p_prefix, (unsigned int)_bootFileSeq, _rotationSubSeq);
    
    _activeFile = SD_MMC.open(_activePath, "w");
    if (!_activeFile) {
        xSemaphoreGiveRecursive(_lock);
        return false;
    }

    snprintf(_activeRawPath, sizeof(_activeRawPath), "%s/raw_%s_%04u_%03u.pcm", 
             SmeaConfig::Path::DIR_RAW_DEF, p_prefix, (unsigned int)_bootFileSeq, _rotationSubSeq);
    
    _rawFile = SD_MMC.open(_activeRawPath, "w");

    _recordCount = 0;
    _writtenBytes = 0;
    _sessionStartMs = millis();
    _sessionOpen = true;
    
    xSemaphoreGiveRecursive(_lock);
    return true;
}


void T460_StorageManager::closeSession(const char* p_reason) {
    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    
    if (!_sessionOpen) {
        xSemaphoreGiveRecursive(_lock);
        return;
    }
    
    flush();

    // _ioError 여부와 관계없이 파일 객체가 유효하면 무조건 닫아서 핸들 누수 원천 차단
    if (_activeFile) _activeFile.close();
    if (_rawFile) _rawFile.close();
    
    _sessionOpen = false;
    strlcpy(_lastError, p_reason, sizeof(_lastError));
    
    // [방어] 원자적 파일 로테이션 (Append & Atomic Write 분리)
    _appendIndexItem(); 
    _writeIndexFileAtomic();
    _handleRotation();
    
    xSemaphoreGiveRecursive(_lock);
}

bool T460_StorageManager::pushFeatureSlot(const SmeaType::FeatureSlot* p_slot) {
    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    if (!p_slot) { xSemaphoreGiveRecursive(_lock); return false; }

    if (!_sessionOpen) {
        if (_preCapacity > 0 && _preFeatBuf) {
            memcpy(&_preFeatBuf[_preHeadFeat], p_slot, sizeof(SmeaType::FeatureSlot));
            _preHeadFeat = (_preHeadFeat + 1) % _preCapacity;
            if (_preCountFeat < _preCapacity) _preCountFeat++;
        }
        xSemaphoreGiveRecursive(_lock);
        return true; 
    }

    if (_preCountFeat > 0 || _preCountRaw > 0) _flushPreBuffer();

    bool v_rst = _pushToDma(p_slot);
    xSemaphoreGiveRecursive(_lock);
    return v_rst;
}



// 5. 듀얼 프리트리거 동기화 플러시
void T460_StorageManager::_flushPreBuffer() {
    // A. Raw 데이터 플러시
    if (_preCountRaw > 0 && _preRawBuf) {
        uint16_t v_oldestRaw = (_preCountRaw == _preCapacity) ? _preHeadRaw : 0;
        uint16_t v_countToFlushRaw = _preCountRaw;
        _preCountRaw = 0; _preHeadRaw = 0;

        for (uint16_t i = 0; i < v_countToFlushRaw; i++) {
            uint16_t idx = (v_oldestRaw + i) % _preCapacity;
            _writeRawDirect(&_preRawBuf[idx]);
        }
    }

    // B. 특징량 데이터 플러시
    if (_preCountFeat > 0 && _preFeatBuf) {
        uint16_t v_oldestFeat = (_preCountFeat == _preCapacity) ? _preHeadFeat : 0;
        uint16_t v_countToFlushFeat = _preCountFeat;
        _preCountFeat = 0; _preHeadFeat = 0;

        for (uint16_t i = 0; i < v_countToFlushFeat; i++) {
            uint16_t idx = (v_oldestFeat + i) % _preCapacity;
            _pushToDma(&_preFeatBuf[idx]);
        }
    }
}


bool T460_StorageManager::_pushToDma(const SmeaType::FeatureSlot* p_slot) {
    if (_ioError) return false;
    
    const uint16_t v_structSize = sizeof(SmeaType::FeatureSlot);
    uint8_t v_slot = _dmaActiveSlot;

    if ((_dmaSlotUsed[v_slot] + v_structSize) > SmeaConfig::StorageLimit::DMA_SLOT_BYTES_CONST) {
        if (!_commitDmaSlot(v_slot)) return false;
        _dmaActiveSlot = (v_slot + 1) % SmeaConfig::StorageLimit::DMA_SLOT_COUNT_CONST;
        v_slot = _dmaActiveSlot;
    }

    memcpy(&_dmaSlots[v_slot][_dmaSlotUsed[v_slot]], p_slot, v_structSize);
    
    _dmaSlotUsed[v_slot] += v_structSize;
    _writtenBytes += v_structSize;
    _recordCount++;
    _batchCount++;
    _lastPushMs = millis();

    if (_batchCount >= SmeaConfig::StorageLimit::WATERMARK_HIGH_CONST) flush();
    return true;
}

bool T460_StorageManager::pushRawPcm(const SmeaType::RawDataSlot* p_rawSlot) {
    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    if (!p_rawSlot) { xSemaphoreGiveRecursive(_lock); return false; }

    if (!_sessionOpen) {
        if (_preCapacity > 0 && _preRawBuf) {
            memcpy(&_preRawBuf[_preHeadRaw], p_rawSlot, sizeof(SmeaType::RawDataSlot));
            _preHeadRaw = (_preHeadRaw + 1) % _preCapacity;
            if (_preCountRaw < _preCapacity) _preCountRaw++;
        }
        xSemaphoreGiveRecursive(_lock);
        return true;
    }

    bool v_rst = _writeRawDirect(p_rawSlot);
    xSemaphoreGiveRecursive(_lock);
    return v_rst;
}


bool T460_StorageManager::flush() {
    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    if (!_sessionOpen || _ioError) {
        xSemaphoreGiveRecursive(_lock);
        return false;
    }
    bool ok = _commitDmaSlot(_dmaActiveSlot);
    _batchCount = 0;
    xSemaphoreGiveRecursive(_lock);
    return ok;
}

void T460_StorageManager::checkIdleFlush() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    if (_sessionOpen && _batchCount > 0 && (millis() - _lastPushMs) >= v_cfg.storage.idle_flush_ms) {
        flush();
    }
    xSemaphoreGiveRecursive(_lock);
}

bool T460_StorageManager::_commitDmaSlot(uint8_t p_slotIdx) {
    uint16_t v_used = _dmaSlotUsed[p_slotIdx];
    if (v_used == 0) return true;
    if (!_activeFile) return false;
    
    size_t v_written = _activeFile.write((const uint8_t*)_dmaSlots[p_slotIdx], v_used);
    
    if (v_written != v_used) {
        _ioError = true;
        _activeFile.close();
        return false;
    }

    _dmaSlotUsed[p_slotIdx] = 0;
    return true;
}

// [방어 2] 인덱스 원자적 갱신 로직
void T460_StorageManager::_appendIndexItem() {
    if (_indexCount < SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST) {
        strlcpy(_indexItems[_indexCount].path, _activePath, 128);
        strlcpy(_indexItems[_indexCount].raw_path, _activeRawPath, 128); 
        _indexItems[_indexCount].record_count = _recordCount;

        // 파일 핸들을 변수로 받아 명시적으로 닫아주는 안전한 로직
        uint32_t v_fSize = 0;
        if (!_ioError) {
            File v_tempFile = SD_MMC.open(_activePath, "r");
            if (v_tempFile) {
                v_fSize = v_tempFile.size();
                v_tempFile.close();
            }
        }
        _indexItems[_indexCount].size_bytes = v_fSize;
        
        _indexItems[_indexCount].created_ms = millis();
        _indexCount++;
    }
}


bool T460_StorageManager::_writeIndexFileAtomic() {
    File v_file = LittleFS.open(SmeaConfig::Path::FILE_INDEX_TMP_DEF, "w");
    if (!v_file) return false;

    JsonDocument v_doc;
    v_doc["count"] = _indexCount;
    JsonArray v_arr = v_doc["items"].to<JsonArray>();

    for (uint16_t i = 0; i < _indexCount; i++) {
        JsonObject v_item = v_arr.add<JsonObject>();
        v_item["path"] = _indexItems[i].path;
        v_item["raw_path"] = _indexItems[i].raw_path;
        v_item["record_count"] = _indexItems[i].record_count;
        v_item["size_bytes"] = _indexItems[i].size_bytes;
        v_item["created_ms"] = _indexItems[i].created_ms;
    }

    serializeJson(v_doc, v_file);
    v_file.close();

    // 정전 발생 시 파일 증발 방지 (Atomic Rename)
    LittleFS.rename(SmeaConfig::Path::FILE_INDEX_TMP_DEF, SmeaConfig::Path::FILE_INDEX_JSON_DEF);
    return true;
}


bool T460_StorageManager::_loadIndexJson() {
    if (!LittleFS.exists(SmeaConfig::Path::FILE_INDEX_JSON_DEF)) return false;
    File v_file = LittleFS.open(SmeaConfig::Path::FILE_INDEX_JSON_DEF, "r");
    if (!v_file) return false;

    JsonDocument v_doc;
    DeserializationError v_err = deserializeJson(v_doc, v_file);
    v_file.close();
    if (v_err) return false;

    _indexCount = v_doc["count"] | 0;
    JsonArray v_arr = v_doc["items"].as<JsonArray>();

    uint16_t i = 0;
    for (JsonObject v_item : v_arr) {
        if (i >= SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST) break;
        strlcpy(_indexItems[i].path, v_item["path"] | "", 128);
        strlcpy(_indexItems[i].raw_path, v_item["raw_path"] | "", 128); 
        _indexItems[i].record_count = v_item["record_count"] | 0;
        _indexItems[i].size_bytes = v_item["size_bytes"] | 0;
        _indexItems[i].created_ms = v_item["created_ms"] | 0;
        i++;
    }
    return true;
}

void T460_StorageManager::_handleRotation() {
    if (_indexCount <= SmeaConfig::StorageLimit::ROTATE_KEEP_MAX_CONST) return;

    bool v_anyDeleted = false;
    
    while (_indexCount > SmeaConfig::StorageLimit::ROTATE_KEEP_MAX_CONST) {
        char v_oldPath[128];
        char v_oldRawPath[128]; 
        strlcpy(v_oldPath, _indexItems[0].path, sizeof(v_oldPath));
        strlcpy(v_oldRawPath, _indexItems[0].raw_path, sizeof(v_oldRawPath));

        if (v_oldPath[0] != '\0') SD_MMC.remove(v_oldPath);
        if (v_oldRawPath[0] != '\0') SD_MMC.remove(v_oldRawPath); // Raw 동반 삭제
        
        for (uint16_t i = 1; i < _indexCount; ++i) {
            _indexItems[i - 1] = _indexItems[i];
        }
        _indexCount--;
        v_anyDeleted = true;
    }

    if (v_anyDeleted) _writeIndexFileAtomic();
}

void T460_StorageManager::checkRotation() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
    if (!_sessionOpen) {
        xSemaphoreGiveRecursive(_lock);
        return;
    }

    bool v_doRotate = false;
    if (_writtenBytes >= (v_cfg.storage.rotate_mb * 1024 * 1024)) v_doRotate = true;
    if ((millis() - _sessionStartMs) >= (v_cfg.storage.rotate_min * 60000)) v_doRotate = true;

    if (v_doRotate) {
        closeSession("rotate");
        openSession(_currentPrefix); // 출처 꼬리표 유지
    }
    xSemaphoreGiveRecursive(_lock);
}

// 4. Raw 다이렉트 기록 헬퍼 함수
bool T460_StorageManager::_writeRawDirect(const SmeaType::RawDataSlot* p_rawSlot) {
    if (!_rawFile || _ioError || !_bounceBuf) return false;

    size_t v_totalBytes = SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float); 

    // Interleave 연산을 미리 할당된 클래스 멤버 버퍼에 수행 (Zero-Allocation)
    for (uint32_t i = 0; i < SmeaConfig::System::FFT_SIZE_CONST; i++) {
        _bounceBuf[i * 2 + 0] = p_rawSlot->raw_L[i];
        _bounceBuf[i * 2 + 1] = p_rawSlot->raw_R[i];
    }
    
    size_t v_written = _rawFile.write((const uint8_t*)_bounceBuf, v_totalBytes);
    
    if (v_written != v_totalBytes) {
        _ioError = true;
        _rawFile.close();
        return false;
    }
    _writtenBytes += v_totalBytes;
    return true;
}
