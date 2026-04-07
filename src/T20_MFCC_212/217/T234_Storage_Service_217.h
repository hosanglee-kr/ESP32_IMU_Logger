/* ============================================================================
 * File: T234_Storage_Service_217.h
 * Summary: 데이터 로깅 및 스토리지 관리 엔진 (v217)
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include <FS.h>
#include <SD_MMC.h>
#include <LittleFS.h>

class CL_T20_StorageService {
public:
    CL_T20_StorageService();
    ~CL_T20_StorageService() = default;

    // 백엔드 준비 (SD_MMC 마운트 시도)
    bool begin(const ST_T20_SdmmcProfile_t& profile);
    
    // 로깅 세션 관리
    bool openSession(const ST_T20_RecorderBinaryHeader_t& header);
    void closeSession();
    
    // 데이터 기록 (DMA 슬롯 기반)
    bool writeRecord(const void* p_data, size_t len);
    
    // 인덱스 및 로테이션 관리
    void handleRotation(uint16_t keep_max);
    bool saveIndexJson();

private:
    File _active_file;
    EM_T20_StorageBackend_t _backend = EN_T20_STORAGE_LITTLEFS;
    uint32_t _record_count = 0;
    bool _session_open = false;
};

