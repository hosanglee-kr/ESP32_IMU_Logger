/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. (실수) Feature Pool 크기를 DMA_SLOT_COUNT(3)로 지정하여 10ms 윈도우 병목 발생.
 * -> (방어) FSM 내부 메모리 풀과 이중 큐의 크기는 반드시 SmeaConfig::System::FEATURE_POOL_SIZE_CONST(100)를 사용.
 * 2. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * ============================================================================
 * File: T450_FsmMgr_006.hpp
 * Summary: FSM Orchestrator & Multi-task Coordinator
 * ========================================================================== */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "T410_Config_006.hpp"
#include "T415_ConfigMgr_006.hpp" // 동적 설정 매니저 추가
#include "T430_DspEng_006.hpp"
#include "T440_FeatExtra_006.hpp"
#include "T480_MicEng_006.hpp"
#include "T460_Storage_006.hpp"
#include "T445_SeqBd_006.hpp"
#include "T470_Commu_006.hpp"

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
    
    // 멀티스레드(Web API - FSM) 간 변수 경합 방어 (캐싱 금지)
    volatile uint32_t _recordStartMs = 0; 
    volatile bool _isManualRecording = false; 
    
    uint8_t _currentTrialNo = 0; // 3회 반복 검증 시나리오 카운터


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
