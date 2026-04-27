/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. (실수) Feature Pool 크기를 DMA_SLOT_COUNT(3)로 지정하여 10ms 윈도우 병목 발생.
 * -> (방어) FSM 내부 메모리 풀과 이중 큐의 크기는 반드시 SmeaConfig::FEATURE_POOL_SIZE(10)를 사용.
 * 2. (실수) 큐(Ready)가 가득 찼을 때 실패한 슬롯 인덱스를 반환하지 않아 큐가 영구 고갈됨(Blackhole).
 * -> (방어) xQueueSend 실패 시 즉시 Free 큐로 롤백(Rollback)시켜 인덱스 증발을 원천 차단.
 * 3. (실수) 평시에 특징량 기록(pushFeatureSlot)을 생략하여 프리트리거 링버퍼가 영구 0프레임이 됨.
 * -> (방어) 상태와 무관하게 pushFeatureSlot은 무조건 호출하여 스토리지 내부 링버퍼를 살림.
 * 4. (실수) RECORDING에서 복귀할 때 큐를 박살 내어 연속된 오디오 시계열이 증발함.
 * -> (방어) 큐 및 필터 초기화는 반드시 READY -> MONITORING (최초 가동) 진입 시에만 수행.
 * ============================================================================
 * File: T450_FsmMgr_003.cpp
 * Summary: FSM Orchestrator & Multi-task Implementation
 * ========================================================================== */

#include "T450_FsmMgr_003.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "T450_FSM";

void T450_FsmManager::begin() {
    // 1. 모든 엔진 초기화
    if (!v_micEngine.init() || !v_dspEngine.init() || !v_extractor.init() || !v_storage.init()) {
        ESP_LOGE(TAG, "Engine Init Failed!");
        return;
    }
    
    v_featurePool = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::FeatureSlot) * SmeaConfig::FEATURE_POOL_SIZE, MALLOC_CAP_SPIRAM);
    v_rawPool     = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::RawDataSlot) * SmeaConfig::FEATURE_POOL_SIZE, MALLOC_CAP_SPIRAM);
    
    // 동적 할당 실패 시 즉각 중단 (패닉 방어)
    if (!v_featurePool || !v_rawPool) {
        ESP_LOGE(TAG, "Critical: PSRAM Pool Allocation Failed!");
        return;
    }

    
    // T445 SeqBuilder 초기화 (최대 128프레임, 39차원)
    v_seqBuilder.init(SmeaConfig::Ml::MAX_SEQUENCE_FRAMES, SmeaConfig::MFCC_TOTAL_DIM);
    
    // 통신 포트 오픈
    v_communicator.init("YOUR_SSID", "YOUR_PW"); 

    // 2. 이중 큐 시스템 생성 및 초기화 (프레임 드랍 차단을 위한 넉넉한 Pool Size 적용)
    v_qFreeSlotIdx = xQueueCreate(SmeaConfig::FEATURE_POOL_SIZE, sizeof(uint8_t));
    v_qReadySlotIdx = xQueueCreate(SmeaConfig::FEATURE_POOL_SIZE, sizeof(uint8_t));

    // 초기에 모든 슬롯(0~N)을 빈 슬롯(Free)으로 큐에 채워 넣음
    for (uint8_t i = 0; i < SmeaConfig::FEATURE_POOL_SIZE; i++) {
        xQueueSend(v_qFreeSlotIdx, &i, portMAX_DELAY);
    }

    // 3. 듀얼 코어 분산 태스크 생성
    xTaskCreatePinnedToCore(captureTask, "CapTask", SmeaConfig::Task::CAPTURE_STACK_SIZE, this, SmeaConfig::Task::CAPTURE_PRIORITY, &v_hCaptureTask, SmeaConfig::Task::CORE_CAPTURE);
    xTaskCreatePinnedToCore(processingTask, "PrcTask", SmeaConfig::Task::PROCESS_STACK_SIZE, this, SmeaConfig::Task::PROCESS_PRIORITY, &v_hProcessTask, SmeaConfig::Task::CORE_PROCESS);

    setSystemState(SystemState::READY);
}

void T450_FsmManager::setSystemState(SystemState p_nextState) {
    if (v_systemState == p_nextState) return;

    SystemState v_prevState = v_systemState; // 이전 상태 백업
    v_systemState = p_nextState;
    
    // [방어 및 최적화] 상태 전환에 따른 하드웨어/버퍼 제어 연동
    if (p_nextState == SystemState::READY) {
        // 대기 상태 진입 시 마이크 전력 절감 및 버퍼 수집 중지
        v_micEngine.pause();
        
    // (MONITORING 뿐만 아니라 RECORDING으로 직행할 때도 하드웨어를 깨우도록 수정)
    // Todo >= 조건 제거하고 명확한 상태조건으로 변경
    } 
    // MONITORING 또는 RECORDING 상태로 진입 시 (수동 녹음 다이렉트 진입 포함)
    else if (p_nextState == SystemState::MONITORING || p_nextState == SystemState::RECORDING) {
        if (v_prevState == SystemState::READY) {
            // [보완 1] 큐 강제 초기화(Reset/Refill) 코드 완전 삭제. 
            // In-flight 중인 슬롯의 데이터 오염(Race Condition)을 막고 자연 순환에 맡김.
            v_micEngine.resume();            
            v_dspEngine.resetFilterStates(); 
        }
    }
}

void T450_FsmManager::handleExternalTrigger(bool p_isActive) {
    v_isrTriggerActive = p_isActive;
}


void T450_FsmManager::captureTask(void* p_param) {
    T450_FsmManager* v_this = (T450_FsmManager*)p_param;
    
    // 슬라이딩 오버랩 구현을 위한 태스크 내부 고정 버퍼
    // Hop = 10ms(420 샘플), FFT = 1024 샘플.
    const uint32_t v_fftSize = SmeaConfig::FFT_SIZE;
    const uint32_t v_hopSamples = (uint32_t)(SmeaConfig::SAMPLING_RATE * (SmeaConfig::HOP_MS / 1000.0f)); 
    const uint32_t v_overlapSamples = v_fftSize - v_hopSamples; // 1024 - 420 = 604
    
    alignas(16) float v_windowL[v_fftSize] = {0};
    alignas(16) float v_windowR[v_fftSize] = {0};

    uint8_t v_slotIdx;
    SystemState v_lastState = SystemState::INIT; // 과거 상태 추적용

    while(1) {
        // ISR에서 넘겨받은 플래그로 안전하게 상태 전환을 대리 수행
        bool v_currentTrigger = v_this->v_isrTriggerActive;
        
        // 1. 외부 인터럽트(핀)가 LOW(꺼짐)로 떨어졌을 때 무조건 READY로 강제 강등 (Fail-safe)
        // [보완 1] 단, "수동 녹음 중(!v_this->v_isManualRecording)"이 아닐 때만 하드웨어 핀 제어가 개입하도록 수정
        if (!v_currentTrigger && v_this->v_systemState != SystemState::READY && !v_this->v_isManualRecording) {
            // 만약 자동 녹음 중이었다면 강제로 세션 종료
            if (v_this->v_systemState == SystemState::RECORDING) {
                v_this->v_storage.closeSession("trg_drop_force_end");
            }
            v_this->setSystemState(SystemState::READY);
        } 
        // 2. 외부 인터럽트가 HIGH(켜짐)이고 현재 READY 상태라면 모니터링 가동
        else if (v_currentTrigger && v_this->v_systemState == SystemState::READY) {
			v_this->setSystemState(SystemState::RECORDING); // MONITORING 거치지 않고 바로 검사 돌입
			v_this->v_currentTrialNo = 1;
			v_this->v_recordStartMs = millis();
			v_this->v_storage.openSession("trg_auto");
			v_this->v_extractor.setNoiseLearning(true); // 1회차 워밍업 시 노이즈 학습 활성화
				
            // v_this->setSystemState(SystemState::MONITORING);
        }

        
        if (v_this->v_systemState == SystemState::MONITORING || v_this->v_systemState == SystemState::RECORDING) {
            uint32_t v_readCount = 0;
            uint32_t v_targetCount = (v_lastState == SystemState::READY) ? v_fftSize : v_hopSamples;

            // 초기 펌핑(Priming): 막 모니터링 시작 시 1024 프레임을 꽉 채워 거대한 임펄스 계단파 오탐지를 방어
            if (v_lastState == SystemState::READY) {
                v_readCount = v_this->v_micEngine.readData(v_windowL, v_windowR, v_fftSize);
            } else {
                // 1. 기존 데이터 좌측 밀어내기 (Overlap)
                memmove(&v_windowL[0], &v_windowL[v_hopSamples], v_overlapSamples * sizeof(float));
                memmove(&v_windowR[0], &v_windowR[v_hopSamples], v_overlapSamples * sizeof(float));
                // 2. 우측 빈 공간(Hop)만큼 새 데이터 읽기
                v_readCount = v_this->v_micEngine.readData(&v_windowL[v_overlapSamples], &v_windowR[v_overlapSamples], v_hopSamples);
            }
            
            v_lastState = v_this->v_systemState;

            // 데이터가 정상적으로 완전히 채워진 경우에만 큐 전송 (I2S 유실 방어)
            if (v_readCount == v_targetCount) {
                if (xQueueReceive(v_this->v_qFreeSlotIdx, &v_slotIdx, 0) == pdTRUE) {
                    SmeaType::FeatureSlot& v_slot = v_this->v_featurePool[v_slotIdx];
                    SmeaType::RawDataSlot& v_raw  = v_this->v_rawPool[v_slotIdx];
                    
                    // Raw 풀에 데이터 분리 저장
                    memcpy(v_raw.raw_L, v_windowL, sizeof(v_windowL));
                    memcpy(v_raw.raw_R, v_windowR, sizeof(v_windowR));
                    v_slot.timestamp = millis();
                
                    if (xQueueSend(v_this->v_qReadySlotIdx, &v_slotIdx, 0) != pdTRUE) {
                        xQueueSend(v_this->v_qFreeSlotIdx, &v_slotIdx, 0); 
                    }
                } 
            }
            // I2S 데이터 유실 시 슬라이딩 윈도우 영구 오염을 막기 위한 자가 치유
            else {
                // 논리적인 상태(v_systemState)는 모니터링/녹음을 유지하되,
                // 다음 프레임에서 1024 샘플 전체를 다시 읽어(초기 펌핑) 윈도우를 깨끗하게 복구시킵니다.
                v_lastState = SystemState::READY; 
                ESP_LOGW(TAG, "I2S Drop Detected! Auto-healing sliding window...");
            }
        } else {
            v_lastState = v_this->v_systemState; // READY 등 대기 상태 갱신
        }
        vTaskDelay(pdMS_TO_TICKS(1)); // Watchdog Yield
    }
}


void T450_FsmManager::processingTask(void* p_param) {
    T450_FsmManager* v_this = (T450_FsmManager*)p_param;
    uint8_t v_slotIdx;
    alignas(16) float v_beamformedOutput[SmeaConfig::FFT_SIZE];


    while(1) {
 		
        // Ready 큐에 데이터가 들어올 때까지 대기 (최대 100ms)
        if (xQueueReceive(v_this->v_qReadySlotIdx, &v_slotIdx, pdMS_TO_TICKS(100))) {
            SmeaType::FeatureSlot& v_slot = v_this->v_featurePool[v_slotIdx];
            SmeaType::RawDataSlot& v_raw  = v_this->v_rawPool[v_slotIdx];
            
			// 현재 트라이얼 번호 기입
            v_slot.trial_no = v_this->v_currentTrialNo; 
			
            // 1. DSP 정제 (L, R -> 단일 빔포밍 파형 도출)
            v_this->v_dspEngine.process(v_raw.raw_L, v_raw.raw_R, v_beamformedOutput, SmeaConfig::FFT_SIZE);
            
            // 2. 피처 추출 (MFCC 39D, 위상, 첨도, 스펙트럼 중심 등 포괄)
            v_this->v_extractor.extract(v_beamformedOutput, v_raw.raw_L, v_raw.raw_R, SmeaConfig::FFT_SIZE, v_slot);
            
            // 3. ML 텐서 조립기(SeqBuilder)에 MFCC 39D 안전 밀어넣기
            v_this->v_seqBuilder.pushVector(v_slot.mfcc);

         
            // 4. 하이브리드(Rule/ML) 판정
            DetectionResult v_result = v_this->runHybridDecision(v_slotIdx);
            
            if (v_result != DetectionResult::PASS) {
                // 트리거가 들어오면 무조건 RECORDING으로 진입하도록 변경 (captureTask에서 처리됨)
                v_this->v_communicator.publishResultMqtt(v_slot, v_result);
            }
			/*
			if (v_result != DetectionResult::PASS) {
                if (v_this->v_systemState == SystemState::MONITORING) {
                    v_this->setSystemState(SystemState::RECORDING);
                    v_this->v_storage.openSession("trg_ng"); 
                    v_this->v_recordStartMs = millis(); 
                    v_this->v_communicator.publishResultMqtt(v_slot, v_result);
                } 
                // [보완 3] 이미 자동 녹음 중인데 계속 불량이 나면 녹음 시간 연장 (Retrigger)
                else if (v_this->v_systemState == SystemState::RECORDING && !v_this->v_isManualRecording) {
                    v_this->v_recordStartMs = millis(); 
                }
            }
			*/   
            
            // 6. 특징량 로깅 (상태 무관 무조건 호출)
            v_this->v_storage.pushFeatureSlot(&v_slot);
            v_this->v_storage.pushRawPcm(&v_raw);
            
            // 7. 스토리지 로깅 제어
			//    3회 반복 수집 시나리오: 워밍업(학습) -> 2회차/3회차(교차 검증)
            if (v_this->v_systemState == SystemState::RECORDING) {
                if (!v_this->v_isManualRecording && (millis() - v_this->v_recordStartMs > (SmeaConfig::VALID_END_SEC * 1000))) {
                    if (v_this->v_currentTrialNo < 3) {
                        v_this->v_currentTrialNo++;
                        v_this->v_recordStartMs = millis(); 
                        // 1회차는 배경 노이즈 학습 수행, 2회차부터 본 검사 돌입
                        v_this->v_extractor.setNoiseLearning(v_this->v_currentTrialNo == 1);
                    } else {
                        v_this->v_storage.closeSession("trg_end_trials");
                        v_this->setSystemState(v_this->v_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
                        v_this->v_currentTrialNo = 0;
                    }
                }
            }            
			/*
			if (v_this->v_systemState == SystemState::RECORDING) {
                // [보완 2] 수동 녹음(v_isManualRecording) 중일 때는 0.5초 자동 종료 타이머 무시!
                if (!v_this->v_isManualRecording && (millis() - v_this->v_recordStartMs > (SmeaConfig::VALID_END_SEC * 1000))) {
                    v_this->v_storage.closeSession("trg_end");
                    v_this->setSystemState(v_this->v_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
                }
            }
			*/
			
            
            // 8. 웹/앱 실시간 차트용 바이너리 브로드캐스트
            v_this->v_communicator.broadcastBinary(v_slot.mfcc, sizeof(v_slot.mfcc));
            // 처리가 끝난 슬롯을 다시 Free 큐로 반납하여 선순환
            xQueueSend(v_this->v_qFreeSlotIdx, &v_slotIdx, 0); 
        }
    }
}


DetectionResult T450_FsmManager::runHybridDecision(uint8_t p_slotIdx) {
    SmeaType::FeatureSlot& v_slot = v_featurePool[p_slotIdx];
    
	// Priority 0: 하드웨어 단선 / 마이크 에러 감지 (Test NG)
    if (v_slot.energy < SmeaConfig::TEST_NG_MIN_ENERGY) return DetectionResult::TEST_NG;
	
    // Priority 1: Rule NG 검문
    if (v_slot.energy > SmeaConfig::RULE_ENRG_THRESHOLD) return DetectionResult::RULE_NG;
	
    if (v_slot.pooling_stddev_min > SmeaConfig::RULE_STDDEV_THRESHOLD) return DetectionResult::RULE_NG;
    
    // [방어] 임펄스/타격음 오탐지 감별용 단기/장기 에너지 비율(STA/LTA) 검사 복원
    if (v_slot.sta_lta_ratio > 3.0f) return DetectionResult::RULE_NG; 
    
    // Priority 2: ML Prediction (추후 TFLite 연동)
    return DetectionResult::PASS;
}

void T450_FsmManager::runMaintenanceTask() {
    v_communicator.runNetwork();
    v_storage.checkIdleFlush();
    v_storage.checkRotation();
}


void T450_FsmManager::dispatchCommand(SystemCommand p_cmd) {
    switch (p_cmd) {
        case SystemCommand::CMD_MANUAL_RECORD_START:
            if (v_systemState == SystemState::MONITORING || v_systemState == SystemState::READY) {
                v_isManualRecording = true; // [보완 2] 수동 플래그 ON
                setSystemState(SystemState::RECORDING);
                v_storage.openSession("man"); 
                v_recordStartMs = millis();   
            }
            break;
        case SystemCommand::CMD_MANUAL_RECORD_STOP:
            if (v_systemState == SystemState::RECORDING && v_isManualRecording) {
                v_isManualRecording = false; 
                v_storage.closeSession("man_end");
                // [보완 2] 수동 녹음 종료 시 무조건 MONITORING이 아닌, ISR 핀 상태에 맞춰 지능적 복귀
                setSystemState(v_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
            }
            break;
        case SystemCommand::CMD_LEARN_NOISE:
            v_extractor.setNoiseLearning(true); // 학습 트리거
            break;
        case SystemCommand::CMD_REBOOT:
            v_storage.closeSession("reboot");
            delay(500);
            ESP.restart();
            break;
    }
}


