/* ============================================================================
 * File: T234_Storage_Service_217.h
 * Summary: Data Logging & Storage Management Engine (v217)
 * Description: v216의 Zero-Copy DMA, 이벤트 트래킹, 인덱스 로테이션 완벽 복원
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include "T214_Def_Rec_217.h"
#include <FS.h>
#include <SD_MMC.h>
#include <LittleFS.h>

class CL_T20_StorageService {
public:
    CL_T20_StorageService();
    ~CL_T20_StorageService() = default;

    // 스토리지 마운트 및 초기화
    bool begin(const ST_T20_SdmmcProfile_t& profile);
    
    // 로깅 세션 제어
    bool openSession(const ST_T20_RecorderBinaryHeader_t& header);
    void closeSession(const char* reason = "end_normal");
    
    // Zero-Copy DMA 슬롯 기반 고속 기록
    bool pushVector(const ST_T20_FeatureVector_t* p_vec);
    
    // 타임아웃 및 강제 플러시
    bool flush();
    void checkIdleFlush();

    // 상태 및 감사(Audit) 트래킹 API
    void writeEvent(const char* event_msg);
    void setLastError(const char* err_msg);
    const char* getLastError() const { return _last_error; }
    
    bool isOpen() const { return _session_open; }
    uint32_t getRecordCount() const { return _record_count; }

private:
    bool _commitSlot(uint8_t slot_idx);
    void _handleRotation();
    bool _saveIndexJson();
    bool _loadIndexJson();

private:
    EM_T20_StorageBackend_t _backend;
    File     _active_file;
    bool     _session_open;
    uint32_t _record_count;
    char     _active_path[128];
    char     _last_error[128];

    // --- v216 DMA 버퍼링 복원 ---
    static constexpr uint8_t  DMA_SLOT_COUNT = 3;
    static constexpr uint32_t DMA_SLOT_BYTES = 1024;
    
    alignas(32) uint8_t _dma_slots[DMA_SLOT_COUNT][DMA_SLOT_BYTES];
    uint16_t _dma_slot_used[DMA_SLOT_COUNT];
    uint8_t  _dma_active_slot;
    
    // --- v216 배치 및 플러시 조건 복원 ---
    uint16_t _batch_count;
    uint16_t _watermark_high;
    uint32_t _last_push_ms;
    uint32_t _idle_flush_ms;

    // --- v216 파일 로테이션 인덱스 복원 ---
    static constexpr uint16_t MAX_ROTATE_LIST = 16;
    struct ST_IndexItem {
        char     path[128];
        uint32_t size_bytes;
        uint32_t created_ms;
        uint32_t record_count;
    } _index_items[MAX_ROTATE_LIST];
    
    uint16_t _index_count;
    uint16_t _rotate_keep_max;
};
