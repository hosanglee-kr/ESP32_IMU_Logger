/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. (실수) Feature Pool 크기를 DMA_SLOT_COUNT(3)로 지정하여 10ms 윈도우 병목 발생.
 * -> (방어) FSM 내부 메모리 풀과 이중 큐의 크기는 반드시 SmeaConfig::System::FEATURE_POOL_SIZE_CONST(100)를 사용.
 * 2. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 3. [매직 넘버 철폐]: 태스크 지연 시간, 반복 검증 횟수, STA/LTA 임계치 등을
 * 정적/동적 설정으로 완전히 분리하여 중앙 통제력을 확보한다.
 * 4. [v012 고도화 R-TBO / T-LRC 적용]:
 * - 시계열(Timestamp)은 64비트 절대시간(Epoch)인 time(NULL)로 기록.
 * - 간격 및 델타 연산(_recordStartMs)은 상대시간인 esp_timer_get_time() 적용.
 * - 처리 루프 끝단에 esp_task_wdt_reset()을 삽입하여 FreeRTOS IDLE 기아 WDT 패닉 방어.
 * ============================================================================
 * File: T450_FsmMgr_012.hpp
 * Summary: FSM Orchestrator & Multi-task Coordinator
 * ========================================================================== */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "T410_Def_012.hpp"
#include "T415_ConfigMgr_012.hpp" 
#include "T430_DspEng_012.hpp"
#include "T440_FeatExtra_012.hpp"
#include "T480_MicEng_012.hpp"
#include "T460_Storage_012.hpp"
#include "T445_SeqBd_012.hpp"
#include "T470_Commu_012.hpp"

class T450_FsmManager {
private:
    SystemState _systemState = SystemState::INIT;

    TaskHandle_t _hCaptureTask = nullptr;
    TaskHandle_t _hProcessTask = nullptr;

    QueueHandle_t _qFreeSlotIdx = nullptr;
    QueueHandle_t _qReadySlotIdx = nullptr;

    T480_MicEngine           _micEngine;
    T430_DspEngine           _dspEngine;
    T440_FeatureExtractor    _extractor;
    T460_StorageManager      _storage;
    T445_SequenceBuilder     _seqBuilder;
    T470_Communicator        _communicator;

    SmeaType::FeatureSlot* _featurePool = nullptr;
    SmeaType::RawDataSlot* _rawPool = nullptr;

    volatile bool _isrTriggerActive = false;

    // [v012 교정] 절대 역행하지 않는 esp_timer 틱 사용 (ms 단위 저장)
    volatile uint32_t _recordStartTick = 0;
    volatile bool _isManualRecording = false;

    uint8_t _currentTrialNo = 0; 

public:
    static T450_FsmManager& getInstance() {
        static T450_FsmManager v_instance;
        return v_instance;
    }

    void begin();
    void setSystemState(SystemState p_nextState);
    void handleExternalTrigger(bool p_isActive);

    SystemState getCurrentState() const { return _systemState; }
    void runMaintenanceTask();

    void dispatchCommand(SystemCommand p_cmd);

private:
    T450_FsmManager() = default;

    static void _captureTask(void* p_param);
    static void _processingTask(void* p_param);

    DetectionResult _runHybridDecision(uint8_t p_slotIdx);
};

