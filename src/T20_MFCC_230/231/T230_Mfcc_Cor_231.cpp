/* ============================================================================
 * File: T230_Mfcc_Cor_231.cpp
 * Summary: Main Task Logic, FSM Orchestration & Trigger Evaluator
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
// [트리거 판별기 캡슐화]
// ============================================================================
static void _evaluateTriggers(ST_T20_TriggerCtx_t* ctx, const ST_T20_FeatureVector_t* p_feature, 
                              const ST_T20_Config_t& cfg, const float* max_band_energy) {
    bool condition_met = false;
    const uint8_t axis_cnt = (uint8_t)cfg.feature.axis_count;

    // 1. RMS 트리거 판별 (어느 한 축이라도 넘으면 발동)
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

    // 3. 런타임 컨텍스트 갱신 (Hold Time 연장 처리)
    uint32_t current_tick = xTaskGetTickCount();
    if (condition_met) {
        ctx->is_triggered = true;
        ctx->hold_end_tick = current_tick + pdMS_TO_TICKS(cfg.trigger.sw_event.hold_time_ms);
    } else {
        // 조건이 맞지 않더라도 Hold Time이 남아있으면 레코딩 유지 (Damping 기록 보장)
        if (ctx->is_triggered && current_tick >= ctx->hold_end_tick) {
            ctx->is_triggered = false;
            ctx->active_source = EN_T20_TRIG_SRC_NONE;
        }
    }
}

// ============================================================================
// [Task 1] Core 0: 센서 데이터 수집
// ============================================================================
void T20_sensorTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    g_isr_sensor_task = xTaskGetCurrentTaskHandle();

    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), T20_bmi_isr_handler, RISING);

    alignas(16) float batch_x[32], batch_y[32], batch_z[32];

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->running) continue;

        // [FSM 개선] READY 상태일 때는 FIFO 버퍼만 비워주고(Overflow 방지), 핑퐁 연산은 무시합니다.
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
                if (xQueueSend(p->frame_queue, &ready_idx, 0) == pdPASS) {
                    uint8_t next_slot = (p->active_fill_buffer + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                    uint16_t overlap_samples = fft_sz - hop_sz;
                    if (overlap_samples > 0) {
                        for (uint8_t a = 0; a < (uint8_t)p->cfg.feature.axis_count; a++) {
                            memcpy(p->raw_buffer[a][next_slot], &p->raw_buffer[a][ready_idx][hop_sz], overlap_samples * sizeof(float));
                        }
                    }
                    p->active_fill_buffer = next_slot;
                    p->active_sample_index = overlap_samples;
                }
            }
        }
    }
}

// ============================================================================
// [Task 2] Core 1: DSP 연산 및 유한 상태 머신 (FSM)
// ============================================================================
void T20_processTask(void* p_arg) {
    auto* p = static_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    if (!p) { vTaskDelete(nullptr); return; }

    uint32_t frame_id = 0;
    const uint16_t axis_cnt = (uint8_t)p->cfg.feature.axis_count;
    const uint16_t mfcc_dim = (uint16_t)p->cfg.feature.mfcc_coeffs * 3 * axis_cnt;
    const uint16_t N = p->cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;

    // SIMD 힙 할당
    ST_T20_FeatureVector_t* p_feature = (ST_T20_FeatureVector_t*)heap_caps_aligned_alloc(16, sizeof(ST_T20_FeatureVector_t), MALLOC_CAP_INTERNAL);
    float* seq_buffer = (float*)heap_caps_aligned_alloc(16, T20::C10_Sys::SEQUENCE_FRAMES_MAX * mfcc_dim * sizeof(float), MALLOC_CAP_INTERNAL);
    const size_t ws_payload_len = (N * axis_cnt) + (bins * axis_cnt) + (39 * axis_cnt);
    float* ws_payload = (float*)heap_caps_aligned_alloc(16, ws_payload_len * sizeof(float), MALLOC_CAP_INTERNAL);

    if (!p_feature || !seq_buffer || !ws_payload) {
        Serial.println(F("[Critical] DSP Task OOM!"));
        vTaskDelete(nullptr); return;
    }

    // 트리거 런타임 제어 컨텍스트 초기화
    ST_T20_TriggerCtx_t trig_ctx = {false, EN_T20_TRIG_SRC_NONE, 0};

    for (;;) {
        // -------------------------------------------------------------
        // [FSM 1단계] 외부 명령(Command) 수신 및 상태 전이
        // -------------------------------------------------------------
        EM_T20_Command_t cmd;
        if (xQueueReceive(p->cmd_queue, &cmd, 0) == pdTRUE) {
            switch(cmd) {
                case EN_T20_CMD_START:
                    if (p->current_state == EN_T20_STATE_READY) p->current_state = EN_T20_STATE_MONITORING;
                    break;
                case EN_T20_CMD_STOP:
                    p->current_state = EN_T20_STATE_READY;
                    trig_ctx.is_triggered = false;
                    if (p->storage.isOpen()) p->storage.closeSession("manual_stop");
                    break;
                case EN_T20_CMD_LEARN_NOISE:
                    if (p->current_state == EN_T20_STATE_MONITORING || p->current_state == EN_T20_STATE_RECORDING) {
                        p->current_state = EN_T20_STATE_NOISE_LEARNING;
                        p->dsp.setNoiseLearning(true);
                        p->dsp.resetNoiseStats();
                    } else if (p->current_state == EN_T20_STATE_NOISE_LEARNING) {
                        p->current_state = EN_T20_STATE_MONITORING;
                        p->dsp.setNoiseLearning(false);
                    }
                    break;
                default: break;
            }
        }

        // -------------------------------------------------------------
        // [FSM 2단계] 상태별 액션 수행 (State Action)
        // -------------------------------------------------------------
        if (p->current_state == EN_T20_STATE_READY) {
            uint8_t dummy; // 큐를 비워 메모리 병목 방지
            while (xQueueReceive(p->frame_queue, &dummy, 0) == pdTRUE) {}
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // MONITORING, RECORDING, NOISE_LEARNING 공통 로직
        uint8_t read_idx;
        if (xQueueReceive(p->frame_queue, &read_idx, pdMS_TO_TICKS(10)) == pdTRUE) {
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            p_feature->timestamp_ms = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
            p_feature->frame_id = ++frame_id;
            p_feature->active_axes = axis_cnt;
            p_feature->status_flags = (tv.tv_sec > 1000000) ? C10_Rec::FLAG_NTP_SYNCED : 0x00;

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

                // 페이로드 조립
                memcpy(ws_payload + (a * N), p->raw_buffer[a][read_idx], N * sizeof(float));
                memcpy(ws_payload + (axis_cnt * N) + (a * bins), p->dsp.getPowerSpectrum(), bins * sizeof(float));
            }

            // 노이즈 학습 중이 아닐 때만 트리거 판별 실행
            if (p->current_state != EN_T20_STATE_NOISE_LEARNING) {
                _evaluateTriggers(&trig_ctx, p_feature, p->cfg, max_band_energy);

                if (trig_ctx.is_triggered) {
                    p->current_state = EN_T20_STATE_RECORDING;
                    p_feature->status_flags |= C10_Rec::FLAG_TRIGGERED;
                    
                    if (!p->storage.isOpen()) {
                        p->storage.openSession(p->cfg);
                        p->storage.writeEvent("smart_trigger_started");
                        p->last_trigger_ms = millis();
                    }
                } else {
                    p->current_state = EN_T20_STATE_MONITORING;
                    if (p->storage.isOpen()) {
                        p->storage.closeSession("trigger_hold_timeout");
                    }
                }
            }

            if (all_ready) {
                for (int b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) {
                    p_feature->band_energy[b] = max_band_energy[b];
                }

                p->seq_builder.pushVector(&p_feature->features[0][0]);

                if (p->cfg.output.output_sequence) {
                    if (p->seq_builder.isReady() && (frame_id % p->cfg.output.sequence_frames == 0)) {
                        p->seq_builder.getSequenceFlat(seq_buffer);
                        p->comm.broadcastBinary(seq_buffer, p->seq_builder.getSequenceFrames() * mfcc_dim);
                    }
                } else {
                    float* mfcc_ptr = ws_payload + (axis_cnt * N) + (axis_cnt * bins);
                    for(uint8_t a = 0; a < axis_cnt; a++) {
                        memcpy(mfcc_ptr + (a * 39), &p_feature->features[a][0], 39 * sizeof(float));
                    }
                    p->comm.broadcastBinary(ws_payload, ws_payload_len);
                }

                xQueueSend(p->recorder_queue, p_feature, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    heap_caps_free(p_feature);
    heap_caps_free(seq_buffer);
    heap_caps_free(ws_payload);
    vTaskDelete(nullptr);
}

// ============================================================================
// [Task 3] Core 1: 스토리지 기록 전담 태스크
// ============================================================================
void T20_recorderTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    ST_T20_FeatureVector_t msg;

    for (;;) {
        if (xQueueReceive(p->recorder_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            p->storage.pushVector(&msg);
        } else {
            p->storage.checkIdleFlush();
        }
    }
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

    // 부팅 시 운영 모드에 따른 초기 FSM 상태 세팅
    if (_impl->cfg.system.op_mode == EN_T20_OP_AUTO) {
        _impl->current_state = EN_T20_STATE_MONITORING;
    } else {
        _impl->current_state = EN_T20_STATE_READY;
    }

    if (!_impl->sensor.begin(_impl->cfg.sensor)) return false;
    if (!_impl->dsp.begin(_impl->cfg)) return false;

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

void CL_T20_Mfcc::stop() {
    _impl->running = false;
    if (_impl->sensor_task) { vTaskDelete(_impl->sensor_task); _impl->sensor_task = nullptr; }
    if (_impl->process_task) { vTaskDelete(_impl->process_task); _impl->process_task = nullptr; }
    if (_impl->recorder_task) { vTaskDelete(_impl->recorder_task); _impl->recorder_task = nullptr; }
    _impl->storage.closeSession();
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

