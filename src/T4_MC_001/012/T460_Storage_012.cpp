/* ============================================================================
 * File: T460_Storage_012.cpp
 * Summary: Asynchronous Storage Engine (A-DSE) Implementation
 * ============================================================================
 * * [AI 메모: 마이그레이션 적용 완료 사항]
 * 1. [A-DSE 태스크]: _storageTaskProc을 신설하여 FAT32 Write 지연을 메인 DSP 루프와 완벽 격리.
 * 2. [O(N) 붕괴 방어]: time(NULL)을 이용해 YYYY/MM/DD 날짜 폴더를 동적 생성.
 * 3. [시간 역행 방어]: 로테이션 인터벌 체크 시 millis() 대신 esp_timer_get_time() 적용.
 * 4. [하드웨어 방어]: f_seek를 통해 10MB 연속 섹터를 선할당(_preAllocateFile).
 * ========================================================================== */

#include "T460_Storage_012.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>

static const char* TAG = "T460_STRG";

T460_StorageManager::T460_StorageManager() {
    _sessionOpen = false;
    _ioError = false;
    _recordCount = 0;
    _indexCount = 0;
    _sessionStartTick = 0;
    _writtenBytes = 0;
    _lock = xSemaphoreCreateRecursiveMutex();
}

T460_StorageManager::~T460_StorageManager() {
    if (_preFeatBuf) heap_caps_free(_preFeatBuf);
    if (_preRawBuf) heap_caps_free(_preRawBuf);
    if (_bounceBuf) heap_caps_free(_bounceBuf);
    if (_asyncFeatRing) heap_caps_free(_asyncFeatRing);
    if (_asyncRawRing) heap_caps_free(_asyncRawRing);
    if (_lock) vSemaphoreDelete(_lock);
}

bool T460_StorageManager::init() {
    if (!SD_MMC.begin("/sdcard", true)) {
        ESP_LOGE(TAG, "SD Card Mount Failed!");
        _ioError = true;
        return false;
    }

    if (!SD_MMC.exists(SmeaConfig::Path::DIR_DATA_DEF)) SD_MMC.mkdir(SmeaConfig::Path::DIR_DATA_DEF);
    if (!SD_MMC.exists(SmeaConfig::Path::DIR_RAW_DEF)) SD_MMC.mkdir(SmeaConfig::Path::DIR_RAW_DEF);

    _allocatePreBuffer();

    // [v012] 비동기 링버퍼 할당 (PSRAM)
    _asyncFeatRing = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::FeatureSlot) * ASYNC_RING_CAPACITY, MALLOC_CAP_SPIRAM);
    _asyncRawRing = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::RawDataSlot) * ASYNC_RING_CAPACITY, MALLOC_CAP_SPIRAM);
    
    if (!_asyncFeatRing || !_asyncRawRing) {
        ESP_LOGE(TAG, "A-DSE Async Ring Buffer Allocation Failed!");
        return false;
    }
    _asyncHead = 0;
    _asyncTail = 0;

    _loadIndexJson();
    
    // [v012] 백그라운드 스토리지 태스크 기동 (우선순위를 낮춰 Core 1 양보)
    xTaskCreatePinnedToCore(_storageTaskProc, "StrgTask", SmeaConfig::Task::STORAGE_STACK_SIZE_CONST, this, SmeaConfig::Task::STORAGE_PRIORITY_CONST, &_hStorageTask, SmeaConfig::Task::CORE_PROCESS_CONST);

    return true;
}

void T460_StorageManager::_allocatePreBuffer() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    _preCapacity = v_cfg.storage.pre_trigger_sec * (SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST);
    
    if (_preCapacity > 0) {
        _preFeatBuf = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, _preCapacity * sizeof(SmeaType::FeatureSlot), MALLOC_CAP_SPIRAM);
        _preRawBuf  = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, _preCapacity * sizeof(SmeaType::RawDataSlot), MALLOC_CAP_SPIRAM);
    }
    // 바운스 버퍼는 안정성을 위해 Internal SRAM 할당
    _bounceBuf = (float*)heap_caps_aligned_alloc(16, SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
}

// [v012 신설] 날짜 기반 서브 폴더 경로 생성 (FAT32 O(N) 병목 타파)
void T460_StorageManager::_buildDailyDirectoryPath(char* p_outPath, size_t p_maxLen) {
    time_t v_now;
    time(&v_now);
    struct tm v_tinfo;
    localtime_r(&v_now, &v_tinfo);

    // 연/월/일 폴더 생성
    char v_dirPath[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST];
    snprintf(v_dirPath, sizeof(v_dirPath), "%s/%04d", SmeaConfig::Path::DIR_DATA_DEF, v_tinfo.tm_year + 1900);
    if (!SD_MMC.exists(v_dirPath)) SD_MMC.mkdir(v_dirPath);
    
    snprintf(v_dirPath, sizeof(v_dirPath), "%s/%04d/%02d", SmeaConfig::Path::DIR_DATA_DEF, v_tinfo.tm_year + 1900, v_tinfo.tm_mon + 1);
    if (!SD_MMC.exists(v_dirPath)) SD_MMC.mkdir(v_dirPath);

    snprintf(v_dirPath, sizeof(v_dirPath), "%s/%04d/%02d/%02d", SmeaConfig::Path::DIR_DATA_DEF, v_tinfo.tm_year + 1900, v_tinfo.tm_mon + 1, v_tinfo.tm_mday);
    if (!SD_MMC.exists(v_dirPath)) SD_MMC.mkdir(v_dirPath);

    // 최종 파일명에는 절대 시간(Epoch) 삽입
    snprintf(p_outPath, p_maxLen, "%s/%s_%llu_%03d.bin", v_dirPath, _currentPrefix, (uint64_t)v_now, _rotationSubSeq);
}

// [v012 신설] 웨어레벨링 멈춤 방어용 파일 선할당
void T460_StorageManager::_preAllocateFile(File& p_file, uint32_t p_bytes) {
    if (p_file) {
        p_file.seek(p_bytes);
        p_file.write(0); // 더미 바이트 기록하여 섹터 확보
        p_file.seek(0);
    }
}

bool T460_StorageManager::openSession(const char* p_prefix) {
    if (_ioError || _sessionOpen) return false;
    xSemaphoreTake(_lock, portMAX_DELAY);

    strlcpy(_currentPrefix, p_prefix, sizeof(_currentPrefix));
    
    _buildDailyDirectoryPath(_activePath, sizeof(_activePath));
    snprintf(_activeRawPath, sizeof(_activeRawPath), "%s/%s_%llu_%03d.pcm", SmeaConfig::Path::DIR_RAW_DEF, _currentPrefix, (uint64_t)time(NULL), _rotationSubSeq);

    _activeFile = SD_MMC.open(_activePath, "w");
    _rawFile = SD_MMC.open(_activeRawPath, "w");

    if (!_activeFile || !_rawFile) {
        _ioError = true;
        xSemaphoreGive(_lock);
        return false;
    }

    // 선할당 적용 (10MB)
    _preAllocateFile(_activeFile, SmeaConfig::StorageLimit::PREALLOC_BYTES_CONST);
    _preAllocateFile(_rawFile, SmeaConfig::StorageLimit::PREALLOC_BYTES_CONST);

    _sessionOpen = true;
    _recordCount = 0;
    _writtenBytes = 0;
    _sessionStartTick = (uint32_t)(esp_timer_get_time() / 1000); // 밀리초 변환

    _flushPreBuffer();

    xSemaphoreGive(_lock);
    return true;
}

void T460_StorageManager::closeSession(const char* p_reason) {
    if (!_sessionOpen) return;
    xSemaphoreTake(_lock, portMAX_DELAY);

    // 잔여 링버퍼를 완전히 비울 때까지 대기
    while(_asyncHead != _asyncTail) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    _activeFile.close();
    _rawFile.close();

    _appendIndexItem();
    _writeIndexFileAtomic();

    _sessionOpen = false;
    _rotationSubSeq = 0;
    
    xSemaphoreGive(_lock);
}

// [v012 변경] Core 1 연산을 보호하기 위한 논블로킹(Non-blocking) 링버퍼 푸시
bool T460_StorageManager::pushFeatureSlot(const SmeaType::FeatureSlot* p_slot) {
    if (_ioError) return false;
    xSemaphoreTake(_lock, portMAX_DELAY);

    if (!_sessionOpen) {
        if (_preCapacity > 0) {
            memcpy(&_preFeatBuf[_preHeadFeat], p_slot, sizeof(SmeaType::FeatureSlot));
            _preHeadFeat = (_preHeadFeat + 1) % _preCapacity;
            if (_preCountFeat < _preCapacity) _preCountFeat++;
        }
    } else {
        uint16_t v_nextHead = (_asyncHead + 1) % ASYNC_RING_CAPACITY;
        if (v_nextHead != _asyncTail) {
            memcpy(&_asyncFeatRing[_asyncHead], p_slot, sizeof(SmeaType::FeatureSlot));
            _asyncHead = v_nextHead;
        } else {
            // 링버퍼 오버플로우 (SD카드 속도 한계 초과)
            ESP_LOGW(TAG, "A-DSE Ring Buffer Overflow! Frame Dropped.");
        }
    }

    xSemaphoreGive(_lock);
    return true;
}

bool T460_StorageManager::pushRawPcm(const SmeaType::RawDataSlot* p_rawSlot) {
    if (_ioError) return false;
    xSemaphoreTake(_lock, portMAX_DELAY);

    if (!_sessionOpen) {
        if (_preCapacity > 0) {
            memcpy(&_preRawBuf[_preHeadRaw], p_rawSlot, sizeof(SmeaType::RawDataSlot));
            _preHeadRaw = (_preHeadRaw + 1) % _preCapacity;
            if (_preCountRaw < _preCapacity) _preCountRaw++;
        }
    } else {
        // Feature 슬롯과 인덱스를 동기화하기 위해 _asyncHead 위치에 동시 기록
        memcpy(&_asyncRawRing[_asyncHead], p_rawSlot, sizeof(SmeaType::RawDataSlot));
    }

    xSemaphoreGive(_lock);
    return true;
}

// [v012 신설] 백그라운드 SD카드 전담 기록 태스크
void T460_StorageManager::_storageTaskProc(void* p_param) {
    T460_StorageManager* v_this = (T460_StorageManager*)p_param;
    while(1) {
        v_this->_processAsyncRingBuffer();
        vTaskDelay(pdMS_TO_TICKS(5)); // OS에 CPU 반환 (WDT 회피)
    }
}

void T460_StorageManager::_processAsyncRingBuffer() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (!_sessionOpen || _ioError || _asyncHead == _asyncTail) {
        xSemaphoreGive(_lock);
        return;
    }

    // 1개 프레임씩 꺼내서 기록 (락 점유 시간 최소화)
    SmeaType::FeatureSlot v_featCopy;
    SmeaType::RawDataSlot v_rawCopy;
    
    memcpy(&v_featCopy, &_asyncFeatRing[_asyncTail], sizeof(SmeaType::FeatureSlot));
    memcpy(&v_rawCopy, &_asyncRawRing[_asyncTail], sizeof(SmeaType::RawDataSlot));
    
    _asyncTail = (_asyncTail + 1) % ASYNC_RING_CAPACITY;
    xSemaphoreGive(_lock);

    // 실제 파일 I/O는 락(Lock) 밖에서 수행하여 Core 1 블로킹을 완벽히 방지
    if (_activeFile) {
        size_t v_res = _activeFile.write((uint8_t*)&v_featCopy, sizeof(SmeaType::FeatureSlot));
        if (v_res == sizeof(SmeaType::FeatureSlot)) {
            _recordCount++;
            _writtenBytes += v_res;
        } else {
            _ioError = true;
        }
    }

    if (_rawFile && _bounceBuf) {
        memcpy(&_bounceBuf[0], v_rawCopy.raw_L, sizeof(v_rawCopy.raw_L));
        memcpy(&_bounceBuf[SmeaConfig::System::FFT_SIZE_CONST], v_rawCopy.raw_R, sizeof(v_rawCopy.raw_R));
        _rawFile.write((uint8_t*)_bounceBuf, sizeof(v_rawCopy.raw_L) * 2);
    }
}

void T460_StorageManager::_flushPreBuffer() {
    uint16_t v_idxFeat = (_preHeadFeat + _preCapacity - _preCountFeat) % _preCapacity;
    for (uint16_t i = 0; i < _preCountFeat; i++) {
        _activeFile.write((uint8_t*)&_preFeatBuf[v_idxFeat], sizeof(SmeaType::FeatureSlot));
        _recordCount++;
        v_idxFeat = (v_idxFeat + 1) % _preCapacity;
    }

    uint16_t v_idxRaw = (_preHeadRaw + _preCapacity - _preCountRaw) % _preCapacity;
    for (uint16_t i = 0; i < _preCountRaw; i++) {
        if (_bounceBuf) {
            memcpy(&_bounceBuf[0], _preRawBuf[v_idxRaw].raw_L, sizeof(_preRawBuf[v_idxRaw].raw_L));
            memcpy(&_bounceBuf[SmeaConfig::System::FFT_SIZE_CONST], _preRawBuf[v_idxRaw].raw_R, sizeof(_preRawBuf[v_idxRaw].raw_R));
            _rawFile.write((uint8_t*)_bounceBuf, sizeof(_preRawBuf[v_idxRaw].raw_L) * 2);
        }
        v_idxRaw = (v_idxRaw + 1) % _preCapacity;
    }
    _preCountFeat = 0; _preCountRaw = 0;
}

bool T460_StorageManager::flush() {
    if (!_sessionOpen || _ioError) return false;
    xSemaphoreTake(_lock, portMAX_DELAY);
    _activeFile.flush();
    _rawFile.flush();
    xSemaphoreGive(_lock);
    return true;
}

void T460_StorageManager::checkIdleFlush() {
    // 링버퍼 구조로 변경되어 주기적 동기화 불필요. 
    // 필요시 flush() 호출 연계로 남겨둠.
}

void T460_StorageManager::checkRotation() {
    if (!_sessionOpen || _ioError) return;
    xSemaphoreTake(_lock, portMAX_DELAY);

    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    uint32_t v_maxBytes = v_cfg.storage.rotate_mb * SmeaConfig::StorageLimit::BYTES_PER_MB_CONST;
    
    // [v012] 시간 도약 버그 차단을 위해 esp_timer 틱 베이스 계산
    uint32_t v_currentTick = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t v_elapsedMs = v_currentTick - _sessionStartTick;
    uint32_t v_maxMs = v_cfg.storage.rotate_min * SmeaConfig::StorageLimit::MS_PER_MIN_CONST;

    if (_writtenBytes >= v_maxBytes || v_elapsedMs >= v_maxMs) {
        _handleRotation();
    }
    xSemaphoreGive(_lock);
}

void T460_StorageManager::_handleRotation() {
    _activeFile.close();
    _rawFile.close();
    _appendIndexItem();

    _rotationSubSeq++;
    
    _buildDailyDirectoryPath(_activePath, sizeof(_activePath));
    snprintf(_activeRawPath, sizeof(_activeRawPath), "%s/%s_%llu_%03d.pcm", SmeaConfig::Path::DIR_RAW_DEF, _currentPrefix, (uint64_t)time(NULL), _rotationSubSeq);

    _activeFile = SD_MMC.open(_activePath, "w");
    _rawFile = SD_MMC.open(_activeRawPath, "w");

    _preAllocateFile(_activeFile, SmeaConfig::StorageLimit::PREALLOC_BYTES_CONST);
    _preAllocateFile(_rawFile, SmeaConfig::StorageLimit::PREALLOC_BYTES_CONST);

    _recordCount = 0;
    _writtenBytes = 0;
    _sessionStartTick = (uint32_t)(esp_timer_get_time() / 1000);
}

void T460_StorageManager::_appendIndexItem() {
    if (_indexCount >= SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST) {
        if (SD_MMC.exists(_indexItems[0].path)) SD_MMC.remove(_indexItems[0].path);
        if (SD_MMC.exists(_indexItems[0].raw_path)) SD_MMC.remove(_indexItems[0].raw_path);
        
        for (uint16_t i = 0; i < SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST - 1; i++) {
            _indexItems[i] = _indexItems[i + 1];
        }
        _indexCount--;
    }

    strlcpy(_indexItems[_indexCount].path, _activePath, SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST);
    strlcpy(_indexItems[_indexCount].raw_path, _activeRawPath, SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST);
    _indexItems[_indexCount].size_bytes = _writtenBytes;
    _indexItems[_indexCount].created_epoch = (uint64_t)time(NULL); // [v012] Epoch 적용
    _indexItems[_indexCount].record_count = _recordCount;
    _indexCount++;
}

bool T460_StorageManager::_writeIndexFileAtomic() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    uint16_t v_keepMax = SmeaConfig::StorageLimit::ROTATE_KEEP_MAX_CONST;

    while (_indexCount > v_keepMax) {
        if (SD_MMC.exists(_indexItems[0].path)) SD_MMC.remove(_indexItems[0].path);
        if (SD_MMC.exists(_indexItems[0].raw_path)) SD_MMC.remove(_indexItems[0].raw_path);
        
        for (uint16_t i = 0; i < _indexCount - 1; i++) {
            _indexItems[i] = _indexItems[i + 1];
        }
        _indexCount--;
    }

    File v_tmp = LittleFS.open(SmeaConfig::Path::FILE_INDEX_TMP_DEF, "w");
    if (!v_tmp) return false;

    JsonDocument v_doc;
    JsonArray v_arr = v_doc["files"].to<JsonArray>();
    for (uint16_t i = 0; i < _indexCount; i++) {
        JsonObject v_obj = v_arr.add<JsonObject>();
        v_obj["path"] = _indexItems[i].path;
        v_obj["raw_path"] = _indexItems[i].raw_path;
        v_obj["size"] = _indexItems[i].size_bytes;
        v_obj["created"] = _indexItems[i].created_epoch;
        v_obj["records"] = _indexItems[i].record_count;
    }

    serializeJson(v_doc, v_tmp);
    v_tmp.close();

    LittleFS.remove(SmeaConfig::Path::FILE_INDEX_JSON_DEF);
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

    JsonArrayConst v_arr = v_doc["files"];
    _indexCount = 0;
    for (JsonObjectConst v_obj : v_arr) {
        if (_indexCount >= SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST) break;
        strlcpy(_indexItems[_indexCount].path, v_obj["path"] | "", SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST);
        strlcpy(_indexItems[_indexCount].raw_path, v_obj["raw_path"] | "", SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST);
        _indexItems[_indexCount].size_bytes = v_obj["size"] | 0;
        _indexItems[_indexCount].created_epoch = v_obj["created"] | 0;
        _indexItems[_indexCount].record_count = v_obj["records"] | 0;
        _indexCount++;
    }
    return true;
}

