/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. (실수) Feature Pool 크기를 DMA_SLOT_COUNT(3)로 지정하여 10ms 윈도우 병목 발생.
 * -> (방어) FSM 내부 메모리 풀과 이중 큐의 크기는 반드시 SmeaConfig::System::FEATURE_POOL_SIZE_CONST(100)를 사용.
 * 2. (실수) 큐(Ready)가 가득 찼을 때 실패한 슬롯 인덱스를 반환하지 않아 큐가 영구 고갈됨(Blackhole).
 * -> (방어) xQueueSend 실패 시 즉시 Free 큐로 롤백(Rollback)시켜 인덱스 증발을 원천 차단.
 * 3. (실수) 평시에 특징량 기록(pushFeatureSlot)을 생략하여 프리트리거 링버퍼가 영구 0프레임이 됨.
 * -> (방어) 상태와 무관하게 pushFeatureSlot은 무조건 호출하여 스토리지 내부 링버퍼를 살림.
 * 4. (실수) RECORDING에서 복귀할 때 큐를 박살 내어 연속된 오디오 시계열이 증발함.
 * -> (방어) 큐 및 필터 초기화는 반드시 READY -> MONITORING (최초 가동) 진입 시에만 수행.
 * ============================================================================
 * File: T450_FsmMgr_008.cpp
 * Summary: FSM Orchestrator & Multi-task Implementation
 * ========================================================================== */

#include "T450_FsmMgr_007.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "T450_FSM";

void T450_FsmManager::begin() {
    // 1. 모든 엔진 초기화
    if (!_micEngine.init() || !_dspEngine.init() || !_extractor.init() || !_storage.init()) {
        ESP_LOGE(TAG, "Engine Init Failed!");
        return;
    }
    
    _featurePool = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::FeatureSlot) * SmeaConfig::System::FEATURE_POOL_SIZE_CONST, MALLOC_CAP_SPIRAM);
    _rawPool     = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::RawDataSlot) * SmeaConfig::System::FEATURE_POOL_SIZE_CONST, MALLOC_CAP_SPIRAM);
    
    // 동적 할당 실패 시 즉각 중단 (패닉 방어)
    if (!_featurePool || !_rawPool) {
        ESP_LOGE(TAG, "Critical: PSRAM Pool Allocation Failed!");
        return;
    }

    // T445 SeqBuilder 초기화 (최대 128프레임, 39차원)
    _seqBuilder.init(SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST, SmeaConfig::System::MFCC_TOTAL_DIM_CONST);
    
    // 통신 포트 오픈
    _communicator.init("YOUR_SSID", "YOUR_PW"); 

    // 2. 이중 큐 시스템 생성 및 초기화 (프레임 드랍 차단을 위한 넉넉한 Pool Size 적용)
    _qFreeSlotIdx = xQueueCreate(SmeaConfig::System::FEATURE_POOL_SIZE_CONST, sizeof(uint8_t));
    _qReadySlotIdx = xQueueCreate(SmeaConfig::System::FEATURE_POOL_SIZE_CONST, sizeof(uint8_t));

    // 초기에 모든 슬롯(0~N)을 빈 슬롯(Free)으로 큐에 채워 넣음
    for (uint8_t i = 0; i < SmeaConfig::System::FEATURE_POOL_SIZE_CONST; i++) {
        xQueueSend(_qFreeSlotIdx, &i, portMAX_DELAY);
    }

    // 3. 듀얼 코어 분산 태스크 생성
    xTaskCreatePinnedToCore(_captureTask, "CapTask", SmeaConfig::Task::CAPTURE_STACK_SIZE_CONST, this, SmeaConfig::Task::CAPTURE_PRIORITY_CONST, &_hCaptureTask, SmeaConfig::Task::CORE_CAPTURE_CONST);
    xTaskCreatePinnedToCore(_processingTask, "PrcTask", SmeaConfig::Task::PROCESS_STACK_SIZE_CONST, this, SmeaConfig::Task::PROCESS_PRIORITY_CONST, &_hProcessTask, SmeaConfig::Task::CORE_PROCESS_CONST);

    setSystemState(SystemState::READY);
}

void T450_FsmManager::setSystemState(SystemState p_nextState) {
    if (_systemState == p_nextState) return;

    SystemState v_prevState = _systemState; // 이전 상태 백업
    _systemState = p_nextState;
    
    // [방어 및 최적화] 상태 전환에 따른 하드웨어/버퍼 제어 연동
    if (p_nextState == SystemState::READY) {
        // 대기 상태 진입 시 마이크 전력 절감 및 버퍼 수집 중지
        _micEngine.pause();
        
    } 
    // MONITORING 또는 RECORDING 상태로 진입 시 (수동 녹음 다이렉트 진입 포함)
    else if (p_nextState == SystemState::MONITORING || p_nextState == SystemState::RECORDING) {
        if (v_prevState == SystemState::READY) {
            // 큐 강제 초기화(Reset/Refill) 코드 완전 삭제. 
            // In-flight 중인 슬롯의 데이터 오염(Race Condition)을 막고 자연 순환에 맡김.
            _micEngine.resume();            
            _dspEngine.resetFilterStates(); 
        }
    }
}

void T450_FsmManager::handleExternalTrigger(bool p_isActive) {
    _isrTriggerActive = p_isActive;
}


void T450_FsmManager::_captureTask(void* p_param) {
    T450_FsmManager* v_this = (T450_FsmManager*)p_param;
    
    // 슬라이딩 오버랩 구현을 위한 태스크 내부 고정 버퍼
    const uint32_t v_fftSize = SmeaConfig::System::FFT_SIZE_CONST;
    
    alignas(16) float v_windowL[SmeaConfig::System::FFT_SIZE_CONST] = {0};
    alignas(16) float v_windowR[SmeaConfig::System::FFT_SIZE_CONST] = {0};

    uint8_t v_slotIdx;
    SystemState v_lastState = SystemState::INIT; // 과거 상태 추적용

    while(1) {
        // 루프가 시작될 때마다 최신 런타임 설정을 로드하여 Hop 크기 동적 계산
        DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
        const uint32_t v_hopSamples = (uint32_t)(SmeaConfig::System::SAMPLING_RATE_CONST * (v_cfg.dsp.hop_ms / SmeaConfig::System::MS_PER_SEC_CONST)); 
        const uint32_t v_overlapSamples = v_fftSize - v_hopSamples; 

        // ISR에서 넘겨받은 플래그로 안전하게 상태 전환을 대리 수행
        bool v_currentTrigger = v_this->_isrTriggerActive;
        
        // 1. 외부 인터럽트(핀)가 LOW(꺼짐)로 떨어졌을 때 무조건 READY로 강제 강등 (Fail-safe)
        // 단, "수동 녹음 중(!v_this->_isManualRecording)"이 아닐 때만 하드웨어 핀 제어가 개입하도록 수정
        if (!v_currentTrigger && v_this->_systemState != SystemState::READY && !v_this->_isManualRecording) {
            // 만약 자동 녹음 중이었다면 강제로 세션 종료
            if (v_this->_systemState == SystemState::RECORDING) {
                v_this->_storage.closeSession("trg_drop_force_end");
            }
            v_this->setSystemState(SystemState::READY);
        } 
        // 2. 외부 인터럽트가 HIGH(켜짐)이고 현재 READY 상태라면 모니터링 가동
        else if (v_currentTrigger && v_this->_systemState == SystemState::READY) {
            v_this->setSystemState(SystemState::RECORDING); // MONITORING 거치지 않고 바로 검사 돌입
            v_this->_currentTrialNo = 1;
            v_this->_recordStartMs = millis();
            v_this->_storage.openSession("trg_auto");
            v_this->_extractor.setNoiseLearning(true); // 1회차 워밍업 시 노이즈 학습 활성화
        }
        
        if (v_this->_systemState == SystemState::MONITORING || v_this->_systemState == SystemState::RECORDING) {
            uint32_t v_readCount = 0;
            uint32_t v_targetCount = (v_lastState == SystemState::READY) ? v_fftSize : v_hopSamples;

            // 초기 펌핑(Priming): 막 모니터링 시작 시 1024 프레임을 꽉 채워 거대한 임펄스 계단파 오탐지를 방어
            if (v_lastState == SystemState::READY) {
                v_readCount = v_this->_micEngine.readData(v_windowL, v_windowR, v_fftSize);
            } else {
                // 1. 기존 데이터 좌측 밀어내기 (Overlap)
                memmove(&v_windowL[0], &v_windowL[v_hopSamples], v_overlapSamples * sizeof(float));
                memmove(&v_windowR[0], &v_windowR[v_hopSamples], v_overlapSamples * sizeof(float));
                // 2. 우측 빈 공간(Hop)만큼 새 데이터 읽기
                v_readCount = v_this->_micEngine.readData(&v_windowL[v_overlapSamples], &v_windowR[v_overlapSamples], v_hopSamples);
            }
            
            v_lastState = v_this->_systemState;

            // 데이터가 정상적으로 완전히 채워진 경우에만 큐 전송 (I2S 유실 방어)
            if (v_readCount == v_targetCount) {
                if (xQueueReceive(v_this->_qFreeSlotIdx, &v_slotIdx, 0) == pdTRUE) {
                    SmeaType::FeatureSlot& v_slot = v_this->_featurePool[v_slotIdx];
                    SmeaType::RawDataSlot& v_raw  = v_this->_rawPool[v_slotIdx];
                    
                    // Raw 풀에 데이터 분리 저장
                    memcpy(v_raw.raw_L, v_windowL, sizeof(v_windowL));
                    memcpy(v_raw.raw_R, v_windowR, sizeof(v_windowR));
                    v_slot.timestamp = millis();
                
                    if (xQueueSend(v_this->_qReadySlotIdx, &v_slotIdx, 0) != pdTRUE) {
                        xQueueSend(v_this->_qFreeSlotIdx, &v_slotIdx, 0); 
                    }
                } 
            }
            // I2S 데이터 유실 시 슬라이딩 윈도우 영구 오염을 막기 위한 자가 치유
            else {
                // 논리적인 상태(_systemState)는 모니터링/녹음을 유지하되,
                // 다음 프레임에서 1024 샘플 전체를 다시 읽어(초기 펌핑) 윈도우를 깨끗하게 복구시킴
                v_lastState = SystemState::READY; 
                ESP_LOGW(TAG, "I2S Drop Detected! Auto-healing sliding window...");
            }
        } else {
            v_lastState = v_this->_systemState; // READY 등 대기 상태 갱신
        }
        vTaskDelay(pdMS_TO_TICKS(SmeaConfig::Task::WDG_YIELD_MS_CONST)); // Watchdog Yield
    }
}


void T450_FsmManager::_processingTask(void* p_param) {
    T450_FsmManager* v_this = (T450_FsmManager*)p_param;
    uint8_t v_slotIdx;
    alignas(16) float v_beamformedOutput[SmeaConfig::System::FFT_SIZE_CONST];

    while(1) {
        // Ready 큐에 데이터가 들어올 때까지 대기
        if (xQueueReceive(v_this->_qReadySlotIdx, &v_slotIdx, pdMS_TO_TICKS(SmeaConfig::Task::QUEUE_BLOCK_MS_CONST))) {
            
            // 큐에서 빠져나올 때 최신 런타임 설정 로드
            DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

            SmeaType::FeatureSlot& v_slot = v_this->_featurePool[v_slotIdx];
            SmeaType::RawDataSlot& v_raw  = v_this->_rawPool[v_slotIdx];
            
            // 현재 트라이얼 번호 기입
            v_slot.trial_no = v_this->_currentTrialNo; 
            
            // 1. DSP 정제 (L, R -> 단일 빔포밍 파형 도출)
            v_this->_dspEngine.process(v_raw.raw_L, v_raw.raw_R, v_beamformedOutput, SmeaConfig::System::FFT_SIZE_CONST);
            
            // 2. 피처 추출 (MFCC 39D, 위상, 첨도, 스펙트럼 중심 등 포괄)
            v_this->_extractor.extract(v_beamformedOutput, v_raw.raw_L, v_raw.raw_R, SmeaConfig::System::FFT_SIZE_CONST, v_slot);
            
            // 3. ML 텐서 조립기(SeqBuilder)에 MFCC 39D 안전 밀어넣기
            v_this->_seqBuilder.pushVector(v_slot.mfcc);
         
            // 4. 하이브리드(Rule/ML) 판정
            DetectionResult v_result = v_this->_runHybridDecision(v_slotIdx);
            
            if (v_result != DetectionResult::PASS) {
                if (v_this->_systemState == SystemState::MONITORING) {
                    v_this->setSystemState(SystemState::RECORDING);
                    v_this->_storage.openSession("trg_ng_detect"); // 모니터링 중 적발
                    v_this->_recordStartMs = millis(); 
                }
                // MQTT 브로드캐스트는 유지
                v_this->_communicator.publishResultMqtt(v_slot, v_result);
            }
            
            // 6. 특징량 로깅 (상태 무관 무조건 호출)
            v_this->_storage.pushFeatureSlot(&v_slot);
            v_this->_storage.pushRawPcm(&v_raw);
            
            // 7. 스토리지 로깅 제어
            //    3회 반복 수집 시나리오: 워밍업(학습) -> 2회차/3회차(교차 검증)
            if (v_this->_systemState == SystemState::RECORDING) {
                // 동적 설정의 valid_end_sec 참조
                if (!v_this->_isManualRecording && (millis() - v_this->_recordStartMs > (v_cfg.decision.valid_end_sec * SmeaConfig::System::MS_PER_SEC_CONST))) {
                    if (v_this->_currentTrialNo < SmeaConfig::DecisionLimit::MAX_TRIAL_COUNT_CONST) {
                        v_this->_currentTrialNo++;
                        v_this->_recordStartMs = millis(); 
                        // 1회차는 배경 노이즈 학습 수행, 2회차부터 본 검사 돌입
                        v_this->_extractor.setNoiseLearning(v_this->_currentTrialNo == 1);
                    } else {
                        v_this->_storage.closeSession("trg_end_trials");
                        v_this->setSystemState(v_this->_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
                        v_this->_currentTrialNo = 0;
                    }
                }
            }            
            
            // =================================================================
            // 8. 웹/앱 실시간 차트용 다중화(Multiplexed) 바이너리 브로드캐스트
            // [방어] 로컬 스택에 할당하여 메모리 파편화를 완벽하게 회피
            // =================================================================
            SmeaType::PktTelemetry v_telemetry;
            
            // 헤더 조립
            v_telemetry.header.magic = 0xA5;
            v_telemetry.header.type = (uint8_t)SmeaType::StreamType::TELEMETRY;
            v_telemetry.header.length = sizeof(SmeaType::PktTelemetry) - sizeof(SmeaType::WsHeader);
            
            // 데이터 매핑
            v_telemetry.sys_state = (uint8_t)v_this->_systemState;
            v_telemetry.trial_no = v_this->_currentTrialNo;
            v_telemetry.rms = v_slot.rms;
            v_telemetry.sta_lta_ratio = v_slot.sta_lta_ratio;
            v_telemetry.kurtosis = v_slot.kurtosis;
            v_telemetry.spectral_centroid = v_slot.spectral_centroid;
            
            // 구조체 내 배열은 고속 블록 카피(memcpy)로 SIMD 효율 유지
            memcpy(v_telemetry.band_rms, v_slot.band_rms, sizeof(v_slot.band_rms));
            memcpy(v_telemetry.top_peaks, v_slot.top_peaks, sizeof(v_slot.top_peaks)); // 5개 100% 복사됨
            memcpy(v_telemetry.mfcc, v_slot.mfcc, sizeof(v_slot.mfcc));

            // 소켓 브로드캐스트 전송
            v_this->_communicator.broadcastBinary(&v_telemetry, sizeof(v_telemetry));
            
            // (참고: 스펙트럼과 파형은 대역폭 보호를 위해 향후 10프레임 당 1회씩 쏘는 로직으로 확장 가능)

            // 처리가 끝난 슬롯을 다시 Free 큐로 반납하여 선순환
            xQueueSend(v_this->_qFreeSlotIdx, &v_slotIdx, 0); 
            
        }
    }
}


DetectionResult T450_FsmManager::_runHybridDecision(uint8_t p_slotIdx) {
    SmeaType::FeatureSlot& v_slot = _featurePool[p_slotIdx];
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig(); // 런타임 설정 동기화
    
    // Priority 0: 하드웨어 단선 / 마이크 에러 감지 (Test NG)
    if (v_slot.energy < v_cfg.decision.test_ng_min_energy) return DetectionResult::TEST_NG;
    
    // Priority 1: Rule NG 검문
    if (v_slot.energy > v_cfg.decision.rule_enrg_threshold) return DetectionResult::RULE_NG;
    
    if (v_slot.pooling_stddev_min > v_cfg.decision.rule_stddev_threshold) return DetectionResult::RULE_NG;
    
    // [방어] 임펄스/타격음 오탐지 감별용 단기/장기 에너지 비율(STA/LTA) 검사 복원 (동적 런타임 설정 매핑 완료)
    if (v_slot.sta_lta_ratio > v_cfg.decision.sta_lta_threshold) return DetectionResult::RULE_NG; 
    
    // Priority 2: ML Prediction (추후 TFLite 연동)
    return DetectionResult::PASS;
}

void T450_FsmManager::runMaintenanceTask() {
    _communicator.runNetwork();
    _storage.checkIdleFlush();
    _storage.checkRotation();
}


void T450_FsmManager::dispatchCommand(SystemCommand p_cmd) {
    switch (p_cmd) {
        case SystemCommand::CMD_MANUAL_RECORD_START:
            if (_systemState == SystemState::MONITORING || _systemState == SystemState::READY) {
                _isManualRecording = true; // 수동 플래그 ON
                setSystemState(SystemState::RECORDING);
                _storage.openSession("man"); 
                _recordStartMs = millis();   
            }
            break;
        case SystemCommand::CMD_MANUAL_RECORD_STOP:
            if (_systemState == SystemState::RECORDING && _isManualRecording) {
                _isManualRecording = false; 
                _storage.closeSession("man_end");
                // 수동 녹음 종료 시 무조건 MONITORING이 아닌, ISR 핀 상태에 맞춰 지능적 복귀
                setSystemState(_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
            }
            break;
        case SystemCommand::CMD_LEARN_NOISE:
            _extractor.setNoiseLearning(true); // 학습 트리거
            break;
        case SystemCommand::CMD_REBOOT:
            _storage.closeSession("reboot");
            delay(SmeaConfig::Task::REBOOT_DELAY_MS_CONST);
            ESP.restart();
            break;
    }
}
