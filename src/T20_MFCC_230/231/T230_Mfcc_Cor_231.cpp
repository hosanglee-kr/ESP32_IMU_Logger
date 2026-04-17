/* ============================================================================
 * File: T230_Mfcc_Cor_231.cpp
 * Summary: Main Task Logic, FSM Orchestration & Trigger Evaluator
 * ========================================================================== 
 * * [AI 메모: 제공 기능 요약]
 * 1. FSM 기반의 상태 전이 및 모듈 생명주기(Lifecycle) 동기화 적용.
 * 2. 수동 연속 레코딩(Manual) 및 스마트 트리거(Auto) 의도 자동 판별 기능.
 * 3. [최적화] TinyML 텐서 출력 프레임 정렬(Alignment) 버그 수정.
 * 4. [최적화] 태스크 Graceful Exit 적용을 통한 힙 메모리 누수(Leak) 원천 차단.
 * 5. [최적화] 프레임 드롭 시 연속성 보장을 위한 memmove 오버랩 보존 로직 적용.
  * 1. [정합성] 웹소켓 페이로드 크기 오계산 수정 (가변 MFCC 계수 대응).
 * 2. [실시간성] 고속 연산 시 시스템 타스크 기아(Starvation) 방지를 위한 taskYIELD 최적화.
 * 3. [메모리 무결성] Task 종료 시 큐 잔여 데이터를 모두 기록하는 Drain(털어내기) 로직 추가.
 * 4. [신뢰성] 프레임 드롭 시 위상 연속성 보장을 위한 memmove 오버랩 복원 적용.

 * ========================================================================== */

#include "T220_Mfcc_231.h"
#include "T221_Mfcc_Inter_231.h"
#include "T210_Def_231.h"

CL_T20_Mfcc* g_t20 = nullptr;
static TaskHandle_t g_isr_sensor_task = nullptr;

static void IRAM_ATTR T20_bmi_isr_handler() {
    BaseType_t woken = pdFALSE;
    if(g_isr_sensor_task) {
        vTaskNotifyGiveFromISR(g_isr_sensor_task, &woken);
        if (woken) portYIELD_FROM_ISR();
    }
}


// ============================================================================
// [트리거 판별기 캡슐화] 수동 판별 로직 제거 (FSM에서 제어)
// ============================================================================
static void _evaluateTriggers(ST_T20_TriggerCtx_t* ctx, const ST_T20_FeatureVector_t* p_feature, 
                              const ST_T20_Config_t& cfg, const float* max_band_energy) {
    bool condition_met = false;
    const uint8_t axis_cnt = (uint8_t)cfg.feature.axis_count;

    // 1. RMS 트리거 판별
    if (cfg.trigger.sw_event.use_rms) {
        for (uint8_t a = 0; a < axis_cnt; a++) {
            if (p_feature->rms[a] > cfg.trigger.sw_event.rms_threshold_power) {
                condition_met = true;
                ctx->active_source = EN_T20_TRIG_SRC_SW_RMS;
                break;
            }
        }
    }

    // 2. 다중 밴드(Freq) 에너지 트리거 판별
    if (!condition_met) {
        for (int b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) {
            if (cfg.trigger.sw_event.bands[b].enable && max_band_energy[b] > cfg.trigger.sw_event.bands[b].threshold) {
                condition_met = true;
                ctx->active_source = (EM_T20_TriggerSource_t)(EN_T20_TRIG_SRC_SW_BAND_0 + b);
                break;
            }
        }
    }

    // [정합성 보완] 3. 수동(Manual) 모드는 이 함수(evaluate)를 무시하고 
    // FSM 메인 루프에서 ctx->is_triggered를 강제 유지하므로 여기서는 스마트 트리거만 판별합니다.

    // 4. 런타임 컨텍스트 갱신 (Hold Time 연장)
    uint32_t current_tick = xTaskGetTickCount();
    if (condition_met) {
        ctx->is_triggered = true;
        ctx->hold_end_tick = current_tick + pdMS_TO_TICKS(cfg.trigger.sw_event.hold_time_ms);
    } else {
        if (ctx->is_triggered && current_tick >= ctx->hold_end_tick && ctx->active_source != EN_T20_TRIG_SRC_MANUAL) {
            ctx->is_triggered = false;
            ctx->active_source = EN_T20_TRIG_SRC_NONE;
        }
    }
}



/* ============================================================================
 * [Task 1] Core 0: 센서 데이터 수집 (정합성 및 버퍼 오버플로우 방어 최적화)
 * ========================================================================== */
 void T20_sensorTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    g_isr_sensor_task = xTaskGetCurrentTaskHandle();

    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), T20_bmi_isr_handler, RISING);

    alignas(16) float batch_x[32], batch_y[32], batch_z[32];

    while (p->running) { // [최적화] for(;;)를 while(p->running)으로 변경하여 안전한 종료 유도
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->running) break;
        
        if (p->current_state == EN_T20_STATE_READY) {
            p->sensor.readFifoBatch(batch_x, batch_y, batch_z, 32, p->cfg.feature.axis_count, p->cfg.sensor.axis);
            continue;
        }

        uint16_t read_cnt = p->sensor.readFifoBatch(batch_x, batch_y, batch_z, 32, p->cfg.feature.axis_count, p->cfg.sensor.axis);

        for (uint16_t i = 0; i < read_cnt; i++) {
            p->sample_counter++;
            uint8_t  slot = p->active_fill_buffer;
            uint16_t idx  = p->active_sample_index;

            p->raw_buffer[0][slot][idx] = batch_x[i];
            if (p->cfg.feature.axis_count == EN_T20_AXIS_TRIPLE) {
                p->raw_buffer[1][slot][idx] = batch_y[i];
                p->raw_buffer[2][slot][idx] = batch_z[i];
            }
            p->active_sample_index++;

            const uint16_t fft_sz = (uint16_t)p->cfg.feature.fft_size;
            const uint16_t hop_sz = p->cfg.feature.hop_size;

            if (p->active_sample_index >= fft_sz) {
                uint8_t ready_idx = p->active_fill_buffer;
                uint8_t next_slot = (p->active_fill_buffer + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                uint16_t overlap_samples = fft_sz - hop_sz;
                
                if (xQueueSend(p->frame_queue, &ready_idx, 0) == pdPASS) {
                    if (overlap_samples > 0) {
                        for (uint8_t a = 0; a < (uint8_t)p->cfg.feature.axis_count; a++) {
                            memcpy(p->raw_buffer[a][next_slot], &p->raw_buffer[a][ready_idx][hop_sz], overlap_samples * sizeof(float));
                        }
                    }
                    p->active_sample_index = overlap_samples;
                    p->active_fill_buffer = next_slot; 
                } else {
                    // [정합성 보완] 큐 전송 실패 시(Frame Drop), 다음 프레임의 위상 연속성을 보장하기 위해
                    // 슬롯을 전진시키지 않고 현재 슬롯 내부에서 오버랩 데이터를 맨 앞으로 당겨옵니다.
                    if (overlap_samples > 0) {
                        for (uint8_t a = 0; a < (uint8_t)p->cfg.feature.axis_count; a++) {
                            memmove(p->raw_buffer[a][ready_idx], &p->raw_buffer[a][ready_idx][hop_sz], overlap_samples * sizeof(float));
                        }
                    }
                    p->active_sample_index = overlap_samples;
                    // active_fill_buffer는 그대로 유지하여 다음 루프에서 덮어쓰기 유도
                }
            }
        }
    }
    g_isr_sensor_task = nullptr;
    vTaskDelete(nullptr);
}

/* ============================================================================
 * [Task 2] Core 1: DSP 연산 및 유한 상태 머신 (FSM)
 * ========================================================================== */
void T20_processTask(void* p_arg) {
    auto* p = static_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    if (!p) { vTaskDelete(nullptr); return; }

    uint32_t frame_id = 0;
    uint32_t seq_push_cnt = 0;

    const uint8_t  axis_cnt     = (uint8_t)p->cfg.feature.axis_count;
    const uint16_t mfcc_coeffs  = p->cfg.feature.mfcc_coeffs;
    const uint16_t mfcc_dim_raw = mfcc_coeffs * 3 * axis_cnt; // 패딩 없는 순수 데이터 차원
    const uint16_t N            = p->cfg.feature.fft_size;
    const uint16_t bins         = (N / 2) + 1;

    // [정합성] 웹소켓 페이로드 길이 동적 계산: (Wave + Spec + MFCC) * axis_cnt
    const size_t ws_payload_len = (N * axis_cnt) + (bins * axis_cnt) + mfcc_dim_raw;

    ST_T20_FeatureVector_t* p_feature = (ST_T20_FeatureVector_t*)heap_caps_aligned_alloc(16, sizeof(ST_T20_FeatureVector_t), MALLOC_CAP_INTERNAL);
    float* seq_buffer = (float*)heap_caps_aligned_alloc(16, T20::C10_Sys::SEQUENCE_FRAMES_MAX * mfcc_dim_raw * sizeof(float), MALLOC_CAP_INTERNAL);
    float* ws_payload = (float*)heap_caps_aligned_alloc(16, ws_payload_len * sizeof(float), MALLOC_CAP_INTERNAL);

    // [연산성능/메모리 무결성] 메모리 할당 실패 시 성공한 자원만 깔끔하게 정리 후 종료
    if (!p_feature || !seq_buffer || !ws_payload) {
        Serial.println(F("[Critical] DSP Task OOM! Cleaning up..."));
        if (p_feature) heap_caps_free(p_feature);
        if (seq_buffer) heap_caps_free(seq_buffer);
        if (ws_payload) heap_caps_free(ws_payload);
        vTaskDelete(nullptr); return;
    }

    ST_T20_TriggerCtx_t trig_ctx = {false, EN_T20_TRIG_SRC_NONE, 0};

    // [메모리 무결성] 실행 중이거나, 태스크 종료 시점(running=false)이라도 큐에 잔여 프레임이 있으면 끝까지 털어냄(Drain)
    while (p->running || uxQueueMessagesWaiting(p->frame_queue) > 0) {
        
        // -------------------------------------------------------------
        // [FSM 1단계] 외부 명령(Command) 수신 및 모듈 생명주기 초기화
        // -------------------------------------------------------------
        EM_T20_Command_t cmd;
        if (p->running && xQueueReceive(p->cmd_queue, &cmd, 0) == pdTRUE) {
            switch(cmd) {
            	case EN_T20_CMD_START:
                    if (p->current_state == EN_T20_STATE_READY) {
                        p->current_state = EN_T20_STATE_MONITORING;
                        p->dsp.resetFilterStates(); 
                        p->seq_builder.reset();
                        seq_push_cnt = 0; 
                        frame_id = 0;
                        p->sensor.resume(); 

                        memset(ws_payload, 0, ws_payload_len * sizeof(float));

                        if (!p->cfg.trigger.sw_event.use_rms && 
                            !p->cfg.trigger.sw_event.bands[0].enable && 
                            !p->cfg.trigger.sw_event.bands[1].enable && 
                            !p->cfg.trigger.sw_event.bands[2].enable) {
                            trig_ctx.is_triggered = true;
                            trig_ctx.active_source = EN_T20_TRIG_SRC_MANUAL;
                            trig_ctx.hold_end_tick = portMAX_DELAY;
                        }
                    }
                    break;
                case EN_T20_CMD_STOP:
                    if (p->current_state != EN_T20_STATE_READY) {
                        p->current_state = EN_T20_STATE_READY;
                        trig_ctx.is_triggered = false;
                        trig_ctx.active_source = EN_T20_TRIG_SRC_NONE;
                        if (p->storage.isOpen()) p->storage.closeSession("manual_stop");
                        p->sensor.pause();          
                    }
                    break;
                case EN_T20_CMD_LEARN_NOISE:
                    if (p->current_state >= EN_T20_STATE_MONITORING) {
                        p->current_state = EN_T20_STATE_NOISE_LEARNING;
                        p->dsp.setNoiseLearning(true);
                        p->dsp.resetNoiseStats();
                    } else if (p->current_state == EN_T20_STATE_NOISE_LEARNING) {
                        p->current_state = EN_T20_STATE_MONITORING;
                        p->dsp.setNoiseLearning(false);
                    }
                    break;
                case EN_T20_CMD_CALIBRATE: 
                    p->sensor.runCalibration(); 
                    break;
                default: break;
            }
        }

        // -------------------------------------------------------------
        // [FSM 2단계] 상태별 액션 수행
        // -------------------------------------------------------------
        if (p->current_state == EN_T20_STATE_READY && p->running) {
            uint8_t dummy; 
            while (xQueueReceive(p->frame_queue, &dummy, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(10)); 
            continue;
        }

        uint8_t read_idx;
        // 종료 중(Drain)일 때는 대기(Delay)하지 않고 큐에서 즉시 가져옴
        if (xQueueReceive(p->frame_queue, &read_idx, p->running ? pdMS_TO_TICKS(10) : 0) == pdTRUE) {
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            p_feature->timestamp_ms = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
            p_feature->frame_id = ++frame_id;
            p_feature->active_axes = axis_cnt;
            p_feature->status_flags = (tv.tv_sec > 1000000) ? T20::C10_Rec::FLAG_NTP_SYNCED : 0x00;
            
            if (p->cfg.storage.save_raw) {
                p->storage.pushRaw(p->raw_buffer[0][read_idx], p->raw_buffer[1][read_idx], p->raw_buffer[2][read_idx], N, axis_cnt);
            }

            bool all_ready = true;
            float max_band_energy[T20::C10_DSP::TRIGGER_BANDS_MAX] = {0.0f};

            for (uint8_t a = 0; a < axis_cnt; a++) {
                if (!p->dsp.processFrame(p->raw_buffer[a][read_idx], p_feature, a)) all_ready = false; 

                for (int b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) {
                    if (p->cfg.trigger.sw_event.bands[b].enable) {
                        float nrg = p->dsp.getBandEnergy(p->cfg.trigger.sw_event.bands[b].start_hz, p->cfg.trigger.sw_event.bands[b].end_hz);
                        if (nrg > max_band_energy[b]) max_band_energy[b] = nrg;
                    }
                }

                memcpy(ws_payload + (a * N), p->raw_buffer[a][read_idx], N * sizeof(float));
                memcpy(ws_payload + (axis_cnt * N) + (a * bins), p->dsp.getPowerSpectrum(), bins * sizeof(float));
            }

            if (p->current_state != EN_T20_STATE_NOISE_LEARNING) {
                if (trig_ctx.active_source != EN_T20_TRIG_SRC_MANUAL) {
                    _evaluateTriggers(&trig_ctx, p_feature, p->cfg, max_band_energy);
                }

                if (trig_ctx.is_triggered) {
                    if (p->current_state != EN_T20_STATE_RECORDING) {
                        p->current_state = EN_T20_STATE_RECORDING;
                        const char* prefix = (trig_ctx.active_source == EN_T20_TRIG_SRC_MANUAL) ? "man" : "trg";
                        
                        if (!p->storage.isOpen()) {
                            p->storage.openSession(p->cfg, prefix);
                            p->storage.writeEvent("trigger_started");
                            p->last_trigger_ms = millis();
                        }
                    }
                    p_feature->status_flags |= T20::C10_Rec::FLAG_TRIGGERED;
                } else {
                    if (p->current_state == EN_T20_STATE_RECORDING) {
                        p->current_state = EN_T20_STATE_MONITORING;
                        if (p->storage.isOpen()) {
                            p->storage.closeSession("trigger_hold_timeout");
                        }
                    }
                }
            }

            if (all_ready) {
                for (int b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) {
                    p_feature->band_energy[b] = max_band_energy[b];
                }

                // [정합성] SIMD 패딩(16 bytes)을 제외한 순수 데이터만 추출하여 직렬화
                float flat_features[mfcc_dim_raw];
                for(uint8_t a = 0; a < axis_cnt; a++) {
                    memcpy(flat_features + (a * mfcc_coeffs * 3), &p_feature->features[a][0], mfcc_coeffs * 3 * sizeof(float));
                }
                
                p->seq_builder.pushVector(flat_features);
                seq_push_cnt++;

                if (p->cfg.output.output_sequence) {
                    if (p->seq_builder.isReady() && (seq_push_cnt % p->cfg.output.sequence_frames == 0)) {
                        p->seq_builder.getSequenceFlat(seq_buffer);
                        p->comm.broadcastBinary(seq_buffer, p->cfg.output.sequence_frames * mfcc_dim_raw);
                    }
                } else {
                    // [정합성] 웹소켓으로 패딩 없는 순수 MFCC 전송
                    float* mfcc_ptr = ws_payload + (axis_cnt * N) + (axis_cnt * bins);
                    memcpy(mfcc_ptr, flat_features, mfcc_dim_raw * sizeof(float));
                    p->comm.broadcastBinary(ws_payload, ws_payload_len);
                }

                xQueueSend(p->recorder_queue, p_feature, 0);
            }
        }
        
        // [실시간성] 큐 처리 후 명시적 Context Switch 양보 (종료 Drain 중에는 무시하여 최고 속도 확보)
        if (p->running) taskYIELD();
    }

    // [메모리 무결성] 태스크 종료 시 완전한 동적 메모리 반환 보장
    heap_caps_free(p_feature);
    heap_caps_free(seq_buffer);
    heap_caps_free(ws_payload);
    vTaskDelete(nullptr);
}

/* ============================================================================
 * [Task 3] Core 1: 스토리지 기록 전담 태스크
 * ========================================================================== */
 void T20_recorderTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    ST_T20_FeatureVector_t msg;

    // 프로세스 태스크가 종료된 후에도 레코더 큐가 비워질 때까지 가동
    while (p->running || uxQueueMessagesWaiting(p->recorder_queue) > 0) {
        if (xQueueReceive(p->recorder_queue, &msg, p->running ? pdMS_TO_TICKS(200) : 0) == pdTRUE) {
            p->storage.pushVector(&msg);
        } else {
            if (p->running) p->storage.checkIdleFlush();
            else break;
        }
    }
    vTaskDelete(nullptr);
}


// ============================================================================
// [Main Class] Method Implementation
// ============================================================================
CL_T20_Mfcc::CL_T20_Mfcc() {
    _impl = (ST_Impl*)heap_caps_malloc(sizeof(ST_Impl), MALLOC_CAP_SPIRAM);
    if (_impl != nullptr) new (_impl) ST_Impl();
    g_t20 = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc() {
    stop();
    if (_impl) {
        _impl->~ST_Impl();
        heap_caps_free(_impl);
        _impl = nullptr;
    }
}


bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
    if (p_cfg) _impl->cfg = *p_cfg;
    else _impl->cfg = T20_makeDefaultConfig();

    _impl->frame_queue = xQueueCreate(T20::C10_Sys::QUEUE_LEN, sizeof(uint8_t));
    _impl->recorder_queue = xQueueCreate(32, sizeof(ST_T20_FeatureVector_t));
    _impl->cmd_queue = xQueueCreate(10, sizeof(EM_T20_Command_t));
    _impl->mutex = xSemaphoreCreateMutex();

    if (!_impl->sensor.begin(_impl->cfg.sensor)) return false;
    if (!_impl->dsp.begin(_impl->cfg)) return false;

    // [정합성 최적화] 부팅 시 운영 모드에 따른 초기 FSM 상태 세팅 및 센서 제어
    if (_impl->cfg.system.op_mode == EN_T20_OP_AUTO) {
        _impl->current_state = EN_T20_STATE_MONITORING;
    } else {
        _impl->current_state = EN_T20_STATE_READY;
        _impl->sensor.pause(); // 시작부터 대기 상태라면 센서를 멈춰 전력 보존
    }

    uint16_t vector_dim = (p_cfg->feature.mfcc_coeffs * 3) * (uint8_t)p_cfg->feature.axis_count;
    _impl->seq_builder.begin(_impl->cfg.output.sequence_frames, vector_dim);

    ST_T20_SdmmcProfile_t sd_prof = { "default", true,
        T20::C10_Pin::SDMMC_CLK, T20::C10_Pin::SDMMC_CMD, T20::C10_Pin::SDMMC_D0,
        T20::C10_Pin::SDMMC_D1, T20::C10_Pin::SDMMC_D2, T20::C10_Pin::SDMMC_D3 };
        
    _impl->storage.begin(sd_prof);
    _impl->storage.setConfig(_impl->cfg); 

    _impl->comm.begin(_impl->cfg);
    _impl->comm.initHandlers(_impl);

    pinMode(_impl->cfg.system.button_pin, INPUT_PULLUP);

    return true;
}



void CL_T20_Mfcc::postCommand(EM_T20_Command_t cmd) {
    if (_impl && _impl->cmd_queue) {
        xQueueSend(_impl->cmd_queue, &cmd, 0);
    }
}

bool CL_T20_Mfcc::start() {
    if (_impl->running) return false;
    _impl->running = true;

    xTaskCreatePinnedToCore(T20_sensorTask, "T20_Sens", T20::C10_Task::SENSOR_STACK, _impl, T20::C10_Task::SENSOR_PRIO, &_impl->sensor_task, 0);
    xTaskCreatePinnedToCore(T20_processTask, "T20_Proc", T20::C10_Task::PROCESS_STACK, _impl, T20::C10_Task::PROCESS_PRIO, &_impl->process_task, 1);
    xTaskCreatePinnedToCore(T20_recorderTask,"T20_Rec", T20::C10_Task::RECORDER_STACK, _impl, T20::C10_Task::RECORDER_PRIO, &_impl->recorder_task, 1);

    return true;
}

// [최적화] 강제 사살(vTaskDelete) 대신 태스크 자진 종료 유도
void CL_T20_Mfcc::stop() {
    if (!_impl->running) return;
    _impl->running = false; // 루프 탈출 플래그 설정
    
    // 센서 태스크는 portMAX_DELAY로 멈춰있을 수 있으므로 강제로 깨워줍니다.
    if (g_isr_sensor_task) {
        xTaskNotifyGive(g_isr_sensor_task);
    }
    
    // 태스크들이 스스로 루프를 빠져나와 메모리를 정리할 시간을 줍니다.
    delay(50);
    
    _impl->sensor_task = nullptr;
    _impl->process_task = nullptr;
    _impl->recorder_task = nullptr;
    
    _impl->storage.closeSession("system_stop");
}


void CL_T20_Mfcc::run() {
    if (!_impl || !_impl->running) return;

    _impl->comm.runMqtt();

    static bool last_btn_state = HIGH; 
    bool current_btn_state = digitalRead(_impl->cfg.system.button_pin);

    // 버튼 입력 시 FSM 커맨드 큐로 명령 발송 (비동기 처리)
    if (current_btn_state == LOW && last_btn_state == HIGH) {
        if (millis() - _impl->last_btn_ms > T20::C10_Web::BTN_DEBOUNCE_MS) {
            if (_impl->current_state == EN_T20_STATE_READY) postCommand(EN_T20_CMD_START);
            else postCommand(EN_T20_CMD_STOP);
            _impl->last_btn_ms = millis();
        }
    }
    last_btn_state = current_btn_state;

    // Watchdog
    static uint32_t wd_last_cnt = 0;
    static uint32_t wd_last_ms = millis();

    if (millis() - wd_last_ms > _impl->cfg.system.watchdog_ms) {
        if (_impl->current_state >= EN_T20_STATE_MONITORING && wd_last_cnt == _impl->sample_counter) {
            _impl->storage.writeEvent("watchdog_sensor_stall");
            _impl->sensor.resetHardware();
            _impl->sensor.begin(_impl->cfg.sensor);
        }
        wd_last_cnt = _impl->sample_counter;
        wd_last_ms = millis();
    }

    // 딥슬립 (유휴 모드인 READY 상태에서만 검사)
    if (_impl->cfg.trigger.hw_power.use_deep_sleep) {
        uint32_t idle_time = millis() - _impl->last_trigger_ms;

        if (_impl->current_state == EN_T20_STATE_READY && idle_time > (_impl->cfg.trigger.hw_power.sleep_timeout_sec * 1000)) {
            Serial.println(F("[Power] Entering Deep Sleep due to inactivity..."));
            _impl->sensor.enableWakeOnMotion(_impl->cfg.trigger.hw_power.wake_threshold_g, _impl->cfg.trigger.hw_power.duration_x20ms);
            _impl->storage.closeSession("system_sleep");
            delay(500);
            esp_sleep_enable_ext0_wakeup((gpio_num_t)T20::C10_Pin::BMI_INT1, 1); 
            esp_deep_sleep_start();
        }
    }
}

void CL_T20_Mfcc::printStatus(Stream& out) const {
    out.printf("--- %s Status ---\n", T20::C10_Sys::VERSION_STR);
    out.printf("State: %d | Sensor: %s\n", _impl->current_state, _impl->sensor.getStatusText());
    out.printf("Storage: %s (Records: %lu)\n", _impl->storage.isOpen() ? "Active" : "Idle", _impl->storage.getRecordCount());
    out.println("-------------------------");
}

