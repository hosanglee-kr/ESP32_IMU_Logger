/* ============================================================================
 * File: T450_FsmMgr_012.cpp
 * Summary: FSM Orchestrator & Multi-task Implementation
 * ============================================================================
 * * [AI 메모: 마이그레이션 적용 사항]
 * 1. [이슈 #6, #14 타임스탬프 붕괴 방어]: v_slot.timestamp = (uint64_t)time(NULL) 적용.
 * 구간 계산은 esp_timer_get_time()을 사용하여 NTP 점프 버그 원천 차단.
 * 2. [이슈 #9 WDT 패닉 방어]: _processingTask while(1) 끝단에 esp_task_wdt_reset() 포함.
 * 3. [이슈 #15 데드락 방어]: MAINTENANCE 상태 진입 시 마이크 일시정지, SD카드
 * 세션 강제 종료를 통해 OTA 플래시 쓰기 버스 충돌 완벽 회피.
 * ========================================================================== */

#include "T450_FsmMgr_012.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h" // [v012 신규] 와치독 방어용 헤더
#include <sys/time.h>
#include <cstring>

static const char* TAG = "T450_FSM";

void T450_FsmManager::begin() {
    if (!_micEngine.init() || !_dspEngine.init() || !_extractor.init() || !_storage.init()) {
        ESP_LOGE(TAG, "Engine Init Failed!");
        return;
    }

    _featurePool = (SmeaType::FeatureSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::FeatureSlot) * SmeaConfig::System::FEATURE_POOL_SIZE_CONST, MALLOC_CAP_SPIRAM);
    _rawPool     = (SmeaType::RawDataSlot*)heap_caps_aligned_alloc(16, sizeof(SmeaType::RawDataSlot) * SmeaConfig::System::FEATURE_POOL_SIZE_CONST, MALLOC_CAP_SPIRAM);

    if (!_featurePool || !_rawPool) {
        ESP_LOGE(TAG, "Critical: PSRAM Pool Allocation Failed!");
        return;
    }

    _seqBuilder.init(SmeaConfig::MlLimit::MAX_SEQUENCE_FRAMES_CONST, SmeaConfig::System::MFCC_TOTAL_DIM_CONST);
    _communicator.init("YOUR_SSID", "YOUR_PW");

    _qFreeSlotIdx = xQueueCreate(SmeaConfig::System::FEATURE_POOL_SIZE_CONST, sizeof(uint8_t));
    _qReadySlotIdx = xQueueCreate(SmeaConfig::System::FEATURE_POOL_SIZE_CONST, sizeof(uint8_t));

    for (uint8_t i = 0; i < SmeaConfig::System::FEATURE_POOL_SIZE_CONST; i++) {
        xQueueSend(_qFreeSlotIdx, &i, portMAX_DELAY);
    }

    xTaskCreatePinnedToCore(_captureTask, "CapTask", SmeaConfig::Task::CAPTURE_STACK_SIZE_CONST, this, SmeaConfig::Task::CAPTURE_PRIORITY_CONST, &_hCaptureTask, SmeaConfig::Task::CORE_CAPTURE_CONST);
    xTaskCreatePinnedToCore(_processingTask, "PrcTask", SmeaConfig::Task::PROCESS_STACK_SIZE_CONST, this, SmeaConfig::Task::PROCESS_PRIORITY_CONST, &_hProcessTask, SmeaConfig::Task::CORE_PROCESS_CONST);

    setSystemState(SystemState::READY);
}

void T450_FsmManager::setSystemState(SystemState p_nextState) {
    if (_systemState == p_nextState) return;

    SystemState v_prevState = _systemState; 
    _systemState = p_nextState;

    if (p_nextState == SystemState::READY) {
        _micEngine.pause();
    }
    // [v012 신규] OTA 안전 업데이트 및 플래시/SPI 버스 락 해제
    else if (p_nextState == SystemState::MAINTENANCE) {
        _micEngine.pause();             // I2S DMA 정지
        _storage.closeSession("ota");   // SD 카드 FAT 쓰기 정지
        ESP_LOGI(TAG, "System locked into MAINTENANCE for OTA. Bus isolated.");
    }
    else if (p_nextState == SystemState::MONITORING || p_nextState == SystemState::RECORDING) {
        if (v_prevState == SystemState::READY) {
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

    const uint32_t v_fftSize = SmeaConfig::System::FFT_SIZE_CONST;
    alignas(16) float v_windowL[SmeaConfig::System::FFT_SIZE_CONST] = {0};
    alignas(16) float v_windowR[SmeaConfig::System::FFT_SIZE_CONST] = {0};

    uint8_t v_slotIdx;
    SystemState v_lastState = SystemState::INIT; 

    while(1) {
        DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
        const uint32_t v_hopSamples = (uint32_t)(SmeaConfig::System::SAMPLING_RATE_CONST * (v_cfg.dsp.hop_ms / SmeaConfig::System::MS_PER_SEC_CONST));
        const uint32_t v_overlapSamples = v_fftSize - v_hopSamples;

        bool v_currentTrigger = v_this->_isrTriggerActive;

        // [v012 교정] MAINTENANCE 상태에서는 어떠한 조작이나 상태 변경도 허용하지 않음
        if (v_this->_systemState != SystemState::MAINTENANCE) {
            if (!v_currentTrigger && v_this->_systemState != SystemState::READY && !v_this->_isManualRecording) {
                if (v_this->_systemState == SystemState::RECORDING) {
                    v_this->_storage.closeSession("trg_drop_force_end");
                }
                v_this->setSystemState(SystemState::READY);
            }
            else if (v_currentTrigger && v_this->_systemState == SystemState::READY) {
                v_this->setSystemState(SystemState::RECORDING); 
                v_this->_currentTrialNo = 1;
                v_this->_recordStartTick = (uint32_t)(esp_timer_get_time() / 1000); // NTP 무관 독립 틱
                v_this->_storage.openSession("trg_auto");
                v_this->_extractor.setNoiseLearning(true); 
            }
        }

        if (v_this->_systemState == SystemState::MONITORING || v_this->_systemState == SystemState::RECORDING) {
            uint32_t v_readCount = 0;
            uint32_t v_targetCount = (v_lastState == SystemState::READY) ? v_fftSize : v_hopSamples;

            if (v_lastState == SystemState::READY) {
                v_readCount = v_this->_micEngine.readData(v_windowL, v_windowR, v_fftSize);
            } else {
                memmove(&v_windowL[0], &v_windowL[v_hopSamples], v_overlapSamples * sizeof(float));
                memmove(&v_windowR[0], &v_windowR[v_hopSamples], v_overlapSamples * sizeof(float));
                v_readCount = v_this->_micEngine.readData(&v_windowL[v_overlapSamples], &v_windowR[v_overlapSamples], v_hopSamples);
            }

            v_lastState = v_this->_systemState;

            if (v_readCount == v_targetCount) {
                if (xQueueReceive(v_this->_qFreeSlotIdx, &v_slotIdx, 0) == pdTRUE) {
                    SmeaType::FeatureSlot& v_slot = v_this->_featurePool[v_slotIdx];
                    SmeaType::RawDataSlot& v_raw  = v_this->_rawPool[v_slotIdx];

                    memcpy(v_raw.raw_L, v_windowL, sizeof(v_windowL));
                    memcpy(v_raw.raw_R, v_windowR, sizeof(v_windowR));
                    
                    // [v012 핵심] 시계열 데이터 무결성을 위한 64비트 Epoch 절대 시간 기록
                    v_slot.timestamp = (uint64_t)time(NULL);

                    if (xQueueSend(v_this->_qReadySlotIdx, &v_slotIdx, 0) != pdTRUE) {
                        xQueueSend(v_this->_qFreeSlotIdx, &v_slotIdx, 0);
                    }
                }
            }
            else {
                v_lastState = SystemState::READY;
                ESP_LOGW(TAG, "I2S Drop Detected! Auto-healing sliding window...");
            }
        } else {
            v_lastState = v_this->_systemState; 
        }
        vTaskDelay(pdMS_TO_TICKS(SmeaConfig::Task::WDG_YIELD_MS_CONST)); 
    }
}

void T450_FsmManager::_processingTask(void* p_param) {
    T450_FsmManager* v_this = (T450_FsmManager*)p_param;
    uint8_t v_slotIdx;
    alignas(16) float v_beamformedOutput[SmeaConfig::System::FFT_SIZE_CONST];

    while(1) {
        if (xQueueReceive(v_this->_qReadySlotIdx, &v_slotIdx, pdMS_TO_TICKS(SmeaConfig::Task::QUEUE_BLOCK_MS_CONST))) {

            DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

            SmeaType::FeatureSlot& v_slot = v_this->_featurePool[v_slotIdx];
            SmeaType::RawDataSlot& v_raw  = v_this->_rawPool[v_slotIdx];

            v_slot.trial_no = v_this->_currentTrialNo;

            v_this->_dspEngine.process(v_raw.raw_L, v_raw.raw_R, v_beamformedOutput, SmeaConfig::System::FFT_SIZE_CONST);
            v_this->_extractor.extract(v_beamformedOutput, v_raw.raw_L, v_raw.raw_R, SmeaConfig::System::FFT_SIZE_CONST, v_slot);
            v_this->_seqBuilder.pushVector(v_slot.mfcc);

            DetectionResult v_result = v_this->_runHybridDecision(v_slotIdx);

            if (v_result != DetectionResult::PASS) {
                if (v_this->_systemState == SystemState::MONITORING) {
                    v_this->setSystemState(SystemState::RECORDING);
                    v_this->_storage.openSession("trg_ng_detect"); 
                    v_this->_recordStartTick = (uint32_t)(esp_timer_get_time() / 1000);
                }
                v_this->_communicator.publishResultMqtt(v_slot, v_result);
            }

            v_this->_storage.pushFeatureSlot(&v_slot);
            v_this->_storage.pushRawPcm(&v_raw);

            if (v_this->_systemState == SystemState::RECORDING) {
                uint32_t v_currentTick = (uint32_t)(esp_timer_get_time() / 1000);
                if (!v_this->_isManualRecording && (v_currentTick - v_this->_recordStartTick > (v_cfg.decision.valid_end_sec * SmeaConfig::System::MS_PER_SEC_CONST))) {
                    if (v_this->_currentTrialNo < SmeaConfig::DecisionLimit::MAX_TRIAL_COUNT_CONST) {
                        v_this->_currentTrialNo++;
                        v_this->_recordStartTick = v_currentTick;
                        v_this->_extractor.setNoiseLearning(v_this->_currentTrialNo == 1);
                    } else {
                        v_this->_storage.closeSession("trg_end_trials");
                        v_this->setSystemState(v_this->_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
                        v_this->_currentTrialNo = 0;
                    }
                }
            }

            SmeaType::PktTelemetry v_telemetry;
            v_telemetry.header.magic = 0xA5;
            v_telemetry.header.type = (uint8_t)SmeaType::StreamType::TELEMETRY;
            v_telemetry.header.length = sizeof(SmeaType::PktTelemetry) - sizeof(SmeaType::WsHeader);

            v_telemetry.sys_state = (uint8_t)v_this->_systemState;
            v_telemetry.trial_no = v_this->_currentTrialNo;
            v_telemetry.rms = v_slot.rms;
            v_telemetry.sta_lta_ratio = v_slot.sta_lta_ratio;
            v_telemetry.kurtosis = v_slot.kurtosis;
            v_telemetry.spectral_centroid = v_slot.spectral_centroid;

            memcpy(v_telemetry.band_rms, v_slot.band_rms, sizeof(v_slot.band_rms));
            for (uint8_t i = 0; i < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
                v_telemetry.peak_freqs[i] = v_slot.top_peaks[i].frequency;
                v_telemetry.peak_amps[i]  = v_slot.top_peaks[i].amplitude;
            }
			memcpy(v_telemetry.mfcc, v_slot.mfcc, sizeof(v_slot.mfcc));

            v_this->_communicator.broadcastBinary(&v_telemetry, sizeof(v_telemetry));
            xQueueSend(v_this->_qFreeSlotIdx, &v_slotIdx, 0);
        }

        // [v012 신규] FreeRTOS IDLE Task Starvation WDT Panic 원천 방어 
        vTaskDelay(pdMS_TO_TICKS(1));
#ifdef ESP_IDF_VERSION
        esp_task_wdt_reset();
#endif
    }
}

DetectionResult T450_FsmManager::_runHybridDecision(uint8_t p_slotIdx) {
    SmeaType::FeatureSlot& v_slot = _featurePool[p_slotIdx];
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig(); 

    if (v_slot.energy < v_cfg.decision.test_ng_min_energy) return DetectionResult::TEST_NG;
    if (v_slot.energy > v_cfg.decision.rule_enrg_threshold) return DetectionResult::RULE_NG;
    if (v_slot.pooling_stddev_min > v_cfg.decision.rule_stddev_threshold) return DetectionResult::RULE_NG;
    if (v_slot.sta_lta_ratio > v_cfg.decision.sta_lta_threshold) return DetectionResult::RULE_NG;

    return DetectionResult::PASS;
}

void T450_FsmManager::runMaintenanceTask() {
    _communicator.runNetwork();
    _storage.checkRotation();
    T415_ConfigManager::getInstance().checkLazyWrite(); // [v012] 설정 플래시 지연 쓰기 체크 추가
}

void T450_FsmManager::dispatchCommand(SystemCommand p_cmd) {
    // MAINTENANCE(OTA 등) 중에는 CMD_REBOOT와 CMD_OTA_END 외에는 명령 거부
    if (_systemState == SystemState::MAINTENANCE && p_cmd != SystemCommand::CMD_REBOOT && p_cmd != SystemCommand::CMD_OTA_END) {
        return;
    }

    switch (p_cmd) {
        case SystemCommand::CMD_MANUAL_RECORD_START:
            if (_systemState == SystemState::MONITORING || _systemState == SystemState::READY) {
                _isManualRecording = true; 
                setSystemState(SystemState::RECORDING);
                _storage.openSession("man");
                _recordStartTick = (uint32_t)(esp_timer_get_time() / 1000);
            }
            break;
        case SystemCommand::CMD_MANUAL_RECORD_STOP:
            if (_systemState == SystemState::RECORDING && _isManualRecording) {
                _isManualRecording = false;
                _storage.closeSession("man_end");
                setSystemState(_isrTriggerActive ? SystemState::MONITORING : SystemState::READY);
            }
            break;
        case SystemCommand::CMD_LEARN_NOISE:
            _extractor.setNoiseLearning(true); 
            break;
        case SystemCommand::CMD_REBOOT:
            _storage.closeSession("reboot");
            delay(SmeaConfig::Task::REBOOT_DELAY_MS_CONST);
            ESP.restart();
            break;
        case SystemCommand::CMD_OTA_START: // [v012 추가] OTA 진입 
            setSystemState(SystemState::MAINTENANCE);
            break;
        case SystemCommand::CMD_OTA_END: // [v012 추가] OTA 비정상/정상 종료 복구
            if (_systemState == SystemState::MAINTENANCE) {
                setSystemState(SystemState::READY);
            }
            break;
    }
}

