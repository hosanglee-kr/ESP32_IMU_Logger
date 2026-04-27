/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. (실수) Feature Pool 크기를 DMA_SLOT_COUNT(3)로 지정하여 10ms 윈도우 병목 발생.
 * -> (방어) FSM 내부 메모리 풀과 이중 큐의 크기는 반드시 SmeaConfig::FEATURE_POOL_SIZE(10)를 사용.
 * ============================================================================
 * File: T450_FsmMgr_003.hpp
 * Summary: FSM Orchestrator & Multi-task Coordinator
 * ========================================================================== */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// [적발 4 보완] Include 파일명 _001 규칙 엄수
#include "T430_DspEng_003.hpp"
#include "T440_FeatExtra_003.hpp"
#include "T480_MicEng_003.hpp"
#include "T460_Storage_003.hpp"
#include "T445_SeqBd_003.hpp"
#include "T470_Commu_003.hpp"

class T450_FsmManager {
private:
    SystemState v_systemState = SystemState::INIT;
    
    TaskHandle_t v_hCaptureTask = nullptr;
    TaskHandle_t v_hProcessTask = nullptr;
    
    QueueHandle_t v_qFreeSlotIdx = nullptr;  
    QueueHandle_t v_qReadySlotIdx = nullptr; 

    T480_MicEngine           v_micEngine;
    T430_DspEngine           v_dspEngine;
    T440_FeatureExtractor    v_extractor;
    T460_StorageManager      v_storage;
    T445_SequenceBuilder     v_seqBuilder;
    T470_Communicator        v_communicator;

    SmeaType::FeatureSlot* v_featurePool = nullptr; 
    SmeaType::RawDataSlot* v_rawPool = nullptr;

    volatile bool v_isrTriggerActive = false; 
    
    // [보완 2] 멀티스레드(Web API - FSM) 간 변수 경합 방어 (캐싱 금지)
    volatile uint32_t v_recordStartMs = 0; 
    volatile bool v_isManualRecording = false; 
    
    uint8_t v_currentTrialNo = 0; // 3회 반복 검증 시나리오 카운터


public:
    static T450_FsmManager& getInstance() {
        static T450_FsmManager v_instance;
        return v_instance;
    }

    void begin();
    void setSystemState(SystemState p_nextState);
    void handleExternalTrigger(bool p_isActive);
    
    SystemState getCurrentState() const { return v_systemState; }
    void runMaintenanceTask();
    
    void dispatchCommand(SystemCommand p_cmd);


private:
    T450_FsmManager() = default;

    static void captureTask(void* p_param);
    static void processingTask(void* p_param);

    DetectionResult runHybridDecision(uint8_t p_slotIdx);
};

