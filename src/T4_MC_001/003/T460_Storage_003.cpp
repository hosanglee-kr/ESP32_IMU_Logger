/* ============================================================================
 * File: T460_Storage_003.cpp
 * Summary: Storage Engine Implementation with Pre-Trigger & Fail-Safe
 * Description: Zero-copy DMA 슬롯, Atomic File Rotation, SD 캐시 바운스 구현.
 * ========================================================================== */
#include "T460_Storage_003.hpp"
#include <ArduinoJson.h>
#include <Preferences.h>


T460_StorageManager::T460_StorageManager() {
    v_lock = xSemaphoreCreateRecursiveMutex(); 
    v_sessionOpen = false;
    v_ioError = false;
    v_recordCount = 0;
    v_dmaActiveSlot = 0;
    v_batchCount = 0;
    v_lastPushMs = 0;
    v_indexCount = 0;
    v_writtenBytes = 0;
    v_sessionStartMs = 0;

    memset(v_activePath, 0, sizeof(v_activePath));
    memset(v_activeRawPath, 0, sizeof(v_activeRawPath)); 
    memset(v_lastError, 0, sizeof(v_lastError));
    memset(v_currentPrefix, 0, sizeof(v_currentPrefix)); 
    memset(v_dmaSlotUsed, 0, sizeof(v_dmaSlotUsed));
    memset(v_indexItems, 0, sizeof(v_indexItems));
    
    v_preFeatBuf = nullptr;
    v_preRawBuf = nullptr;
    // v_preBuf = nullptr;
    v_bounceBuf = nullptr;
}

T460_StorageManager::~T460_StorageManager() {
    if (v_lock) vSemaphoreDelete(v_lock);
    if (v_preFeatBuf) heap_caps_free(v_preFeatBuf);
    if (v_preRawBuf)  heap_caps_free(v_preRawBuf);
    if (v_bounceBuf)  heap_caps_free(v_bounceBuf); // 소멸 시 해제
}

bool T460_StorageManager::init() {
    
    if (!LittleFS.begin(true)) {
        snprintf(v_lastError, sizeof(v_lastError), "LITTLEFS_MOUNT_FAIL");
        return false;
    }

    // 1. 기존 인덱스 로드 (누락 기능 복원)
    loadIndexJson();
    
    // 2. 파일 시퀀스 관리 (NVS)
    Preferences prefs;
    prefs.begin("SMEA_NVS", false);
    v_bootFileSeq = prefs.getUInt("file_seq", 1);
    prefs.putUInt("file_seq", v_bootFileSeq + 1);
    prefs.end();
    
    // 바운스 버퍼를 안전하게 1회만 정적 할당하도록 init()으로 이동
    if (!v_bounceBuf) {
        v_bounceBuf = (float*)heap_caps_malloc(SmeaConfig::FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        if (!v_bounceBuf) {
            snprintf(v_lastError, sizeof(v_lastError), "BOUNCE_BUF_OOM");
            return false;
        }
    }

    // 3. SDMMC 마운트 (T410 설정 참조)
    if (SD_MMC.begin("/sdcard", true)) { 
        if (!SD_MMC.exists(SmeaConfig::Storage::DIR_DATA)) SD_MMC.mkdir(SmeaConfig::Storage::DIR_DATA);
        if (!SD_MMC.exists(SmeaConfig::Storage::DIR_RAW))  SD_MMC.mkdir(SmeaConfig::Storage::DIR_RAW);
    } else {
        snprintf(v_lastError, sizeof(v_lastError), "SD_MOUNT_FAIL");
        return false;
    }

    allocatePreBuffer();
    return true;
}

void T460_StorageManager::allocatePreBuffer() {
    // 오버랩(Overlap) 환경에 맞는 정확한 1초당 프레임 수 도출 (1000ms / 10ms = 100 FPS)
    uint32_t v_fps = (uint32_t)(1000.0f / SmeaConfig::HOP_MS); 
    uint16_t v_reqCapacity = v_fps * SmeaConfig::Storage::PRE_TRIGGER_SEC; // 100 * 3 = 300 프레임 확보

    if (v_preCapacity != v_reqCapacity) {
        if (v_preFeatBuf) heap_caps_free(v_preFeatBuf);
        if (v_preRawBuf)  heap_caps_free(v_preRawBuf);

        // Feature(~60KB) + Raw(~2.4MB) PSRAM 할당
        v_preFeatBuf = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, v_reqCapacity * sizeof(SmeaType::FeatureSlot), MALLOC_CAP_SPIRAM);
        v_preRawBuf  = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, v_reqCapacity * sizeof(SmeaType::RawDataSlot), MALLOC_CAP_SPIRAM);
        
        if (v_preFeatBuf && v_preRawBuf) {
            v_preCapacity = v_reqCapacity;
            v_preHeadFeat = 0; v_preCountFeat = 0;
            v_preHeadRaw  = 0; v_preCountRaw  = 0;
        } else {
            v_preCapacity = 0;
        }
    }
}


bool T460_StorageManager::openSession(const char* p_prefix) {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    
    if (v_sessionOpen) {
        xSemaphoreGiveRecursive(v_lock);
        return false;
    }
    
    v_rotationSubSeq++; 
    v_ioError = false; 
    strlcpy(v_currentPrefix, p_prefix, sizeof(v_currentPrefix));

    snprintf(v_activePath, sizeof(v_activePath), "%s/%s_%04u_%03u.bin", 
             SmeaConfig::Storage::DIR_DATA, p_prefix, (unsigned int)v_bootFileSeq, v_rotationSubSeq);
    
    v_activeFile = SD_MMC.open(v_activePath, "w");
    if (!v_activeFile) {
        xSemaphoreGiveRecursive(v_lock);
        return false;
    }

    snprintf(v_activeRawPath, sizeof(v_activeRawPath), "%s/raw_%s_%04u_%03u.pcm", 
             SmeaConfig::Storage::DIR_RAW, p_prefix, (unsigned int)v_bootFileSeq, v_rotationSubSeq);
    
    v_rawFile = SD_MMC.open(v_activeRawPath, "w");

    v_recordCount = 0;
    v_writtenBytes = 0;
    v_sessionStartMs = millis();
    v_sessionOpen = true;
    
    xSemaphoreGiveRecursive(v_lock);
    return true;
}


void T460_StorageManager::closeSession(const char* p_reason) {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    
    if (!v_sessionOpen) {
        xSemaphoreGiveRecursive(v_lock);
        return;
    }
    
    flush();

    // v_ioError 여부와 관계없이 파일 객체가 유효하면 무조건 닫아서 핸들 누수(Leak) 원천 차단
    if (v_activeFile) v_activeFile.close();
    if (v_rawFile) v_rawFile.close();
    
    v_sessionOpen = false;
    strlcpy(v_lastError, p_reason, sizeof(v_lastError));
    
    // [방어] 원자적 파일 로테이션 (Append & Atomic Write 분리)
    appendIndexItem(); 
    writeIndexFileAtomic();
    handleRotation();
    
    xSemaphoreGiveRecursive(v_lock);
}

bool T460_StorageManager::pushFeatureSlot(const SmeaType::FeatureSlot* p_slot) {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    if (!p_slot) { xSemaphoreGiveRecursive(v_lock); return false; }

    if (!v_sessionOpen) {
        if (v_preCapacity > 0 && v_preFeatBuf) {
            memcpy(&v_preFeatBuf[v_preHeadFeat], p_slot, sizeof(SmeaType::FeatureSlot));
            v_preHeadFeat = (v_preHeadFeat + 1) % v_preCapacity;
            if (v_preCountFeat < v_preCapacity) v_preCountFeat++;
        }
        xSemaphoreGiveRecursive(v_lock);
        return true; 
    }

    if (v_preCountFeat > 0 || v_preCountRaw > 0) flushPreBuffer();

    bool v_rst = pushToDma(p_slot);
    xSemaphoreGiveRecursive(v_lock);
    return v_rst;
}



// 5. 듀얼 프리트리거 동기화 플러시
void T460_StorageManager::flushPreBuffer() {
    // A. Raw 데이터 플러시
    if (v_preCountRaw > 0 && v_preRawBuf) {
        uint16_t v_oldestRaw = (v_preCountRaw == v_preCapacity) ? v_preHeadRaw : 0;
        uint16_t v_countToFlushRaw = v_preCountRaw;
        v_preCountRaw = 0; v_preHeadRaw = 0;

        for (uint16_t i = 0; i < v_countToFlushRaw; i++) {
            uint16_t idx = (v_oldestRaw + i) % v_preCapacity;
            writeRawDirect(&v_preRawBuf[idx]);
        }
    }

    // B. 특징량 데이터 플러시
    if (v_preCountFeat > 0 && v_preFeatBuf) {
        uint16_t v_oldestFeat = (v_preCountFeat == v_preCapacity) ? v_preHeadFeat : 0;
        uint16_t v_countToFlushFeat = v_preCountFeat;
        v_preCountFeat = 0; v_preHeadFeat = 0;

        for (uint16_t i = 0; i < v_countToFlushFeat; i++) {
            uint16_t idx = (v_oldestFeat + i) % v_preCapacity;
            pushToDma(&v_preFeatBuf[idx]);
        }
    }
}


bool T460_StorageManager::pushToDma(const SmeaType::FeatureSlot* p_slot) {
    if (v_ioError) return false;
    
    const uint16_t v_structSize = sizeof(SmeaType::FeatureSlot);
    uint8_t v_slot = v_dmaActiveSlot;

    if ((v_dmaSlotUsed[v_slot] + v_structSize) > SmeaConfig::Storage::DMA_SLOT_BYTES) {
        if (!commitDmaSlot(v_slot)) return false;
        v_dmaActiveSlot = (v_slot + 1) % SmeaConfig::Storage::DMA_SLOT_COUNT;
        v_slot = v_dmaActiveSlot;
    }

    memcpy(&v_dmaSlots[v_slot][v_dmaSlotUsed[v_slot]], p_slot, v_structSize);
    
    v_dmaSlotUsed[v_slot] += v_structSize;
    v_writtenBytes += v_structSize;
    v_recordCount++;
    v_batchCount++;
    v_lastPushMs = millis();

    if (v_batchCount >= SmeaConfig::Storage::WATERMARK_HIGH) flush();
    return true;
}

bool T460_StorageManager::pushRawPcm(const SmeaType::RawDataSlot* p_rawSlot) {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    if (!p_rawSlot) { xSemaphoreGiveRecursive(v_lock); return false; }

    if (!v_sessionOpen) {
        if (v_preCapacity > 0 && v_preRawBuf) {
            memcpy(&v_preRawBuf[v_preHeadRaw], p_rawSlot, sizeof(SmeaType::RawDataSlot));
            v_preHeadRaw = (v_preHeadRaw + 1) % v_preCapacity;
            if (v_preCountRaw < v_preCapacity) v_preCountRaw++;
        }
        xSemaphoreGiveRecursive(v_lock);
        return true;
    }

    bool v_rst = writeRawDirect(p_rawSlot);
    xSemaphoreGiveRecursive(v_lock);
    return v_rst;
}


bool T460_StorageManager::flush() {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    if (!v_sessionOpen || v_ioError) {
        xSemaphoreGiveRecursive(v_lock);
        return false;
    }
    bool ok = commitDmaSlot(v_dmaActiveSlot);
    v_batchCount = 0;
    xSemaphoreGiveRecursive(v_lock);
    return ok;
}

void T460_StorageManager::checkIdleFlush() {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    if (v_sessionOpen && v_batchCount > 0 && (millis() - v_lastPushMs) >= SmeaConfig::Storage::IDLE_FLUSH_MS) {
        flush();
    }
    xSemaphoreGiveRecursive(v_lock);
}

bool T460_StorageManager::commitDmaSlot(uint8_t p_slotIdx) {
    uint16_t v_used = v_dmaSlotUsed[p_slotIdx];
    if (v_used == 0) return true;
    if (!v_activeFile) return false;
    
    size_t v_written = v_activeFile.write((const uint8_t*)v_dmaSlots[p_slotIdx], v_used);
    
    if (v_written != v_used) {
        v_ioError = true;
        v_activeFile.close();
        return false;
    }

    v_dmaSlotUsed[p_slotIdx] = 0;
    return true;
}

// [방어 2] 인덱스 원자적 갱신 로직
void T460_StorageManager::appendIndexItem() {
    if (v_indexCount < SmeaConfig::Storage::MAX_ROTATE_LIST) {
        strlcpy(v_indexItems[v_indexCount].path, v_activePath, 128);
        strlcpy(v_indexItems[v_indexCount].raw_path, v_activeRawPath, 128); 
        v_indexItems[v_indexCount].record_count = v_recordCount;

        // 파일 핸들을 변수로 받아 명시적으로 닫아주는 안전한 로직으로 교체
        uint32_t v_fSize = 0;
        if (!v_ioError) {
            File v_tempFile = SD_MMC.open(v_activePath, "r");
            if (v_tempFile) {
                v_fSize = v_tempFile.size();
                v_tempFile.close();
            }
        }
        v_indexItems[v_indexCount].size_bytes = v_fSize;
        
        v_indexItems[v_indexCount].created_ms = millis();
        v_indexCount++;
    }
}


bool T460_StorageManager::writeIndexFileAtomic() {
    File v_file = LittleFS.open(SmeaConfig::Storage::FILE_INDEX_TMP, "w");
    if (!v_file) return false;

    JsonDocument v_doc;
    v_doc["count"] = v_indexCount;
    JsonArray v_arr = v_doc["items"].to<JsonArray>();

    for (uint16_t i = 0; i < v_indexCount; i++) {
        JsonObject v_item = v_arr.add<JsonObject>();
        v_item["path"] = v_indexItems[i].path;
        v_item["raw_path"] = v_indexItems[i].raw_path;
        v_item["record_count"] = v_indexItems[i].record_count;
        v_item["size_bytes"] = v_indexItems[i].size_bytes;
        v_item["created_ms"] = v_indexItems[i].created_ms;
    }

    serializeJson(v_doc, v_file);
    v_file.close();

    // 정전 발생 시 파일 증발 방지 (Atomic Rename)
    LittleFS.rename(SmeaConfig::Storage::FILE_INDEX_TMP, SmeaConfig::Storage::FILE_INDEX_JSON);
    return true;
}


bool T460_StorageManager::loadIndexJson() {
    if (!LittleFS.exists(SmeaConfig::Storage::FILE_INDEX_JSON)) return false;
    File v_file = LittleFS.open(SmeaConfig::Storage::FILE_INDEX_JSON, "r");
    if (!v_file) return false;

    JsonDocument v_doc;
    DeserializationError v_err = deserializeJson(v_doc, v_file);
    v_file.close();
    if (v_err) return false;

    v_indexCount = v_doc["count"] | 0;
    JsonArray v_arr = v_doc["items"].as<JsonArray>();

    uint16_t i = 0;
    for (JsonObject v_item : v_arr) {
        if (i >= SmeaConfig::Storage::MAX_ROTATE_LIST) break;
        strlcpy(v_indexItems[i].path, v_item["path"] | "", 128);
        strlcpy(v_indexItems[i].raw_path, v_item["raw_path"] | "", 128); 
        v_indexItems[i].record_count = v_item["record_count"] | 0;
        v_indexItems[i].size_bytes = v_item["size_bytes"] | 0;
        v_indexItems[i].created_ms = v_item["created_ms"] | 0;
        i++;
    }
    return true;
}

void T460_StorageManager::handleRotation() {
    if (v_indexCount <= SmeaConfig::Storage::ROTATE_KEEP_MAX) return;

    bool v_anyDeleted = false;
    
    while (v_indexCount > SmeaConfig::Storage::ROTATE_KEEP_MAX) {
        char v_oldPath[128];
        char v_oldRawPath[128]; 
        strlcpy(v_oldPath, v_indexItems[0].path, sizeof(v_oldPath));
        strlcpy(v_oldRawPath, v_indexItems[0].raw_path, sizeof(v_oldRawPath));

        if (v_oldPath[0] != '\0') SD_MMC.remove(v_oldPath);
        if (v_oldRawPath[0] != '\0') SD_MMC.remove(v_oldRawPath); // Raw 동반 삭제
        
        for (uint16_t i = 1; i < v_indexCount; ++i) {
            v_indexItems[i - 1] = v_indexItems[i];
        }
        v_indexCount--;
        v_anyDeleted = true;
    }

    if (v_anyDeleted) writeIndexFileAtomic();
}

void T460_StorageManager::checkRotation() {
    xSemaphoreTakeRecursive(v_lock, portMAX_DELAY);
    if (!v_sessionOpen) {
        xSemaphoreGiveRecursive(v_lock);
        return;
    }

    bool v_doRotate = false;
    if (v_writtenBytes >= (SmeaConfig::Storage::ROTATE_MB * 1024 * 1024)) v_doRotate = true;
    if ((millis() - v_sessionStartMs) >= (SmeaConfig::Storage::ROTATE_MIN * 60000)) v_doRotate = true;

    if (v_doRotate) {
        closeSession("rotate");
        openSession(v_currentPrefix); // 출처 꼬리표 유지
    }
    xSemaphoreGiveRecursive(v_lock);
}

// 4. Raw 다이렉트 기록 헬퍼 함수
bool T460_StorageManager::writeRawDirect(const SmeaType::RawDataSlot* p_rawSlot) {
    if (!v_rawFile || v_ioError || !v_bounceBuf) return false;

    size_t v_totalBytes = SmeaConfig::FFT_SIZE * 2 * sizeof(float); 

    // Interleave 연산을 미리 할당된 클래스 멤버 버퍼에 수행 (Zero-Allocation)
    for (uint32_t i = 0; i < SmeaConfig::FFT_SIZE; i++) {
        v_bounceBuf[i * 2 + 0] = p_rawSlot->raw_L[i];
        v_bounceBuf[i * 2 + 1] = p_rawSlot->raw_R[i];
    }
    
    size_t v_written = v_rawFile.write((const uint8_t*)v_bounceBuf, v_totalBytes);
    
    if (v_written != v_totalBytes) {
        v_ioError = true;
        v_rawFile.close();
        return false;
    }
    v_writtenBytes += v_totalBytes;
    return true;
}

