/* ============================================================================
 * File: T460_Storage_011.hpp
 * Summary:
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 본 주석은 프로젝트 전반에 걸쳐 절대 삭제되어서는 안 되며,
 * 코드를 수정/확장할 때마다 항상 최신 상태로 유지하고 점검해야 합니다.
 * ============================================================================
 * [1. 시스템 아키텍처 및 하드웨어 구현 원칙]
 * 1. 매직넘버 철폐: 모든 설정은 SmeaConfig 네임스페이스 상수화.
 * 2. 16-Byte 정렬(SIMD 최적화): alignas(16) 강제 정렬.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 4. [동적/정적 상수 승격]: 경로 길이, 단위 변환 상수 등 모든 매직넘버를
 * 제거하고 T410/T415 중앙 설정으로 위임한다.
 * 5. [자료형 엄수]: 일반 int 대신 <cstdint> 기반의 고정 크기 정수형(uint32_t 등) 사용.
 *
 * [2. AI 가 자주 반복하는 실수 및 방어 원칙 (Self-Reflection)]
 * 1. (실수) 다중 스레드(FSM / Web) 동시 접근 시 상태 파괴 (Race Condition).
 * -> (방어) 스토리지 클래스 내부에 xSemaphoreCreateRecursiveMutex 적용.
 * 2. (실수) LittleFS 파일 "w" 쓰기 시 정전 발생 -> 0바이트 파괴 (벽돌화).
 * -> (방어) 인덱스 파일은 무조건 .tmp로 기록 후 rename()하는 원자적 쓰기(Atomic Write) 적용.
 * 3. (실수) SD 카드 탈거 시 무한 I/O 재시도로 인한 Task Watchdog 패닉.
 * -> (방어) I/O 에러 시 _ioError 플래그를 세워 I/O 시도를 원천 차단.
 * 4. (실수) PSRAM 데이터(Raw)를 SD 카드로 직결하여 DMA 캐시 충돌 패닉.
 * -> (방어) Raw 기록 시 MALLOC_CAP_INTERNAL(내부 SRAM) Bounce Buffer 할당 후 경유.
 *
 * [3. 기능 축소/누락 점검 및 보완 원칙 (Anti-Reduction)]
 * 1. T20의 Pre-trigger, Auto Rotation, Raw Pair 삭제 기능을 누락 없이 SMEA-100에 통합.
 *
 * [WARNING: 스토리지 엔진 유지보수 시 주의사항]
 * 1. _preBuf(프리트리거)는 크기가 크므로 반드시 MALLOC_CAP_SPIRAM 으로 할당.
 * 2. 파일 로테이션 시 Raw 파일 누수를 막기 위해 IndexItem에 raw_path 동반 삭제 유지.
 * ========================================================================== */
#pragma once

#include "T410_Def_011.hpp"
#include "T420_Types_011.hpp"
#include "T415_ConfigMgr_011.hpp" 
#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>

class T460_StorageManager {
public:
    T460_StorageManager();
    ~T460_StorageManager();

    bool init();

    /**
     * @brief 파일 세션 오픈 (트리거 또는 수동)
     * @param p_prefix "trg" (트리거), "man" (수동) 등 파일 접두어
     */
    bool openSession(const char* p_prefix = "trg");

    /**
     * @brief 파일 세션 종료 및 인덱스 파일 갱신
     */
    void closeSession(const char* p_reason = "end_normal");

    /**
     * @brief 특징량 슬롯 고속 기록 (Zero-Copy DMA 핑퐁)
     * 세션이 닫혀 있을 때는 PSRAM 프리트리거 링버퍼에 저장.
     */
    bool pushFeatureSlot(const SmeaType::FeatureSlot* p_slot);

    /**
     * @brief Raw 파형(42kHz) 안전 기록 (내부 SRAM 바운스 버퍼 경유)
     */
    bool pushRawPcm(const SmeaType::RawDataSlot* p_rawSlot);

    /**
     * @brief 잔여 버퍼 SD카드 강제 플러시
     */
    bool flush();
    void checkIdleFlush();
    void checkRotation();

    bool isOpen() const { return _sessionOpen; }
    const char* getLastError() const { return _lastError; }

private:
    bool _commitDmaSlot(uint8_t p_slotIdx);
    void _handleRotation();

    // 로테이션 인덱스 관리 (A2 방어)
    void _appendIndexItem();
    bool _writeIndexFileAtomic();
    bool _loadIndexJson();

    // 프리트리거 관리
    bool _pushToDma(const SmeaType::FeatureSlot* p_slot);
    void _flushPreBuffer();
    void _allocatePreBuffer();

    // Raw 데이터 다이렉트 파일 기록 (Bounce Buffer 처리)
    bool _writeRawDirect(const SmeaType::RawDataSlot* p_rawSlot);

private:
    File _activeFile;     // 특징량(.bin)
    File _rawFile;        // 원본 파형(.pcm)

    bool _sessionOpen;
    bool _ioError;        // 핫플러그 방어 플래그

    uint32_t _recordCount;
    char _activePath[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST];
    char _activeRawPath[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST]; // Raw 쌍(Pair) 삭제 트래킹
    char _lastError[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST];
    char _currentPrefix[SmeaConfig::StorageLimit::MAX_PREFIX_LEN_CONST];

    uint32_t _sessionStartMs;
    uint32_t _writtenBytes;

    // --- DMA 버퍼링 (SIMD/DMA 정렬 엄수) ---
    alignas(16) uint8_t _dmaSlots[SmeaConfig::StorageLimit::DMA_SLOT_COUNT_CONST][SmeaConfig::StorageLimit::DMA_SLOT_BYTES_CONST];
    uint16_t _dmaSlotUsed[SmeaConfig::StorageLimit::DMA_SLOT_COUNT_CONST];
    uint8_t  _dmaActiveSlot;

    uint16_t _batchCount;
    uint32_t _lastPushMs;

    // --- 파일 로테이션 트래킹 구조체 ---
    struct IndexItem {
        char path[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST];
        char raw_path[SmeaConfig::StorageLimit::MAX_PATH_LEN_CONST];
        uint32_t size_bytes;
        uint32_t created_ms;
        uint32_t record_count;
    } _indexItems[SmeaConfig::StorageLimit::MAX_ROTATE_LIST_CONST];

    uint16_t _indexCount;

    SemaphoreHandle_t _lock; // 스레드 경합 방어 (Recursive Mutex)

    // --- Pre-trigger 듀얼 링버퍼 ---
    SmeaType::FeatureSlot* _preFeatBuf = nullptr;
    SmeaType::RawDataSlot* _preRawBuf  = nullptr; // 2.4MB PSRAM 할당 필요

    float* _bounceBuf = nullptr; // 8KB Internal SRAM 전용 버퍼 추가

    uint16_t _preCapacity = 0;
    uint16_t _preHeadFeat = 0, _preCountFeat = 0;
    uint16_t _preHeadRaw  = 0, _preCountRaw  = 0;

    uint32_t _bootFileSeq = 0;
    uint16_t _rotationSubSeq = 0;
};
