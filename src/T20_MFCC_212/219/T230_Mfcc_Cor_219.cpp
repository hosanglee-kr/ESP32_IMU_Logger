/* ============================================================================
 * File: T230_Mfcc_Cor_219.cpp
 * Summary: Main Task Logic & Engine Orchestration (v217 Full)
 * ========================================================================== */
#include "T220_Mfcc_219.h"
#include "T221_Mfcc_Inter_219.h"
#include "T218_Def_Main_219.h"

CL_T20_Mfcc* g_t20 = nullptr;

// --- ISR 전용 Task Handle 변수 ---
static TaskHandle_t g_isr_sensor_task = nullptr;

// --- IRAM에 적재되는 안전한 인터럽트 핸들러 ---
static void IRAM_ATTR T20_bmi_isr_handler() {
    BaseType_t woken = pdFALSE;
    // 클래스의 _impl 포인터를 거치지 않고 직접 Task Handle에 알림을 보냅니다.
    if(g_isr_sensor_task) {
        vTaskNotifyGiveFromISR(g_isr_sensor_task, &woken);
        if (woken) {
            portYIELD_FROM_ISR();
        }
    }
}

// --- [1] Core 0: 고속 센서 데이터 수집 ---
void T20_sensorTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    g_isr_sensor_task = xTaskGetCurrentTaskHandle();

    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), T20_bmi_isr_handler, RISING);

    alignas(16) float batch_x[32], batch_y[32], batch_z[32];

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->running || !p->measurement_active) continue;

        uint16_t read_cnt = p->sensor.readFifoBatch(batch_x, batch_y, batch_z, 32, 
                                                   p->cfg.feature.axis_count, p->cfg.sensor.axis);

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

            // [보완] Sliding Window 판단 로직
            const uint16_t fft_sz = (uint16_t)p->cfg.feature.fft_size;
            const uint16_t hop_sz = p->cfg.feature.hop_size;

            if (p->active_sample_index >= fft_sz) {
                uint8_t ready_idx = p->active_fill_buffer;
                if (xQueueSend(p->frame_queue, &ready_idx, 0) == pdPASS) {
                    // 다음 핑퐁 슬롯으로 전환
                    uint8_t next_slot = (p->active_fill_buffer + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                    
                    // [핵심] Overlap 처리: 현재 데이터의 끝부분(fft - hop)을 다음 슬롯의 앞부분으로 복사
                    uint16_t overlap_samples = fft_sz - hop_sz;
                    if (overlap_samples > 0) {
                        for (uint8_t a = 0; a < (uint8_t)p->cfg.feature.axis_count; a++) {
                            memcpy(p->raw_buffer[a][next_slot], 
                                   &p->raw_buffer[a][ready_idx][hop_sz], 
                                   overlap_samples * sizeof(float));
                        }
                    }
                    p->active_fill_buffer = next_slot;
                    p->active_sample_index = overlap_samples; // 복사된 위치 다음부터 채우기 시작
                }
            }
        }
    }
}

// Core 1: DSP 연산 및 스마트 트리거, MQTT 제어 태스크 (v219 완성체)
void T20_processTask(void* p_arg) {
    auto* p = static_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    if (!p) { vTaskDelete(nullptr); return; }

    uint8_t read_idx;
    ST_T20_FeatureVector_t feature;
    uint32_t frame_id = 0;

    const uint16_t axis_cnt = (uint8_t)p->cfg.feature.axis_count;
    const uint16_t mfcc_dim = 39 * axis_cnt;
    
    // [메모리 정렬 보완] 16바이트 정렬 할당
    float* seq_buffer = (float*)heap_caps_aligned_alloc(16, T20::C10_Sys::SEQUENCE_FRAMES_MAX * mfcc_dim * sizeof(float), MALLOC_CAP_INTERNAL);
    
    // [WS 누락 보완] 파형 + 스펙트럼 + MFCC 통합 전송용 버퍼 동적 할당
    // 최대치(FFT 4096 + Bins 2049 + MFCC 117 = 6262 float) 대응
    const uint16_t N = p->cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;
    const size_t ws_payload_len = N + bins + mfcc_dim;
    float* ws_payload = (float*)heap_caps_aligned_alloc(16, ws_payload_len * sizeof(float), MALLOC_CAP_INTERNAL);

    for (;;) {
        if (xQueueReceive(p->frame_queue, &read_idx, portMAX_DELAY) == pdTRUE) {
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            feature.timestamp_ms = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
            feature.frame_id = ++frame_id;
            feature.active_axes = axis_cnt;
            feature.status_flags = (tv.tv_sec > 1000000) ? 0x01 : 0x00;

            // [Raw 누락 보완] 3축 인터리빙 저장
            if (p->cfg.storage.save_raw) {
                p->storage.pushRaw(p->raw_buffer[0][read_idx], p->raw_buffer[1][read_idx], p->raw_buffer[2][read_idx], N, axis_cnt);
            }

            bool all_ready = true;
            bool event_detected = false;

            for (uint8_t a = 0; a < axis_cnt; a++) {
                if (p->dsp.processFrame(p->raw_buffer[a][read_idx], &feature, a)) {
                    
                    // [트리거 보완] RMS 및 밴드 에너지(특정 주파수 대역) 동시 감시
                    float cur_rms = feature.rms[a];
                    float band_nrg = feature.band_energy[a]; // 500~800Hz 에너지

                    if (p->cfg.trigger.use_threshold) {
                        // 진동이 비정상적으로 커지거나, 특정 주파수(예: 마찰음) 대역 에너지가 급증하면 트리거
                        // (참고: band_energy 임계값은 trigger_rms와 비율로 쓰거나 별도 설정 추가 가능)
                        if (cur_rms > p->cfg.trigger.threshold_rms || band_nrg > (p->cfg.trigger.threshold_rms * 5.0f)) {
                            event_detected = true;
                        }
                    }
                } else {
                    all_ready = false; break;
                }
            }

            if (all_ready) {
                // 트리거 및 SD 기록 세션 제어
                if (event_detected) {
                    feature.status_flags |= 0x02; 
                    if (millis() - p->last_trigger_ms > 5000) { 
                        JsonDocument alert_doc;
                        alert_doc["event"] = "anomaly_detected";
                        alert_doc["timestamp_ms"] = feature.timestamp_ms;
                        JsonArray rms_arr = alert_doc["rms"].to<JsonArray>();
                        for(uint8_t a = 0; a < axis_cnt; a++) rms_arr.add(feature.rms[a]);
                        p->comm.publishMqtt("alert", alert_doc);
                    }
                    p->last_trigger_ms = millis();
                    if (!p->storage.isOpen()) p->storage.openSession(p->cfg);
                } else {
                    if (p->storage.isOpen() && (millis() - p->last_trigger_ms > 5000)) {
                        p->storage.closeSession("trigger_hold_timeout");
                    }
                }

                p->seq_builder.pushVector(&feature.features[0][0]);

                if (p->cfg.output.output_sequence) {
                    if (p->seq_builder.isReady() && (frame_id % p->cfg.output.sequence_frames == 0)) {
                        p->seq_builder.getSequenceFlat(seq_buffer);
                        p->comm.broadcastBinary(seq_buffer, p->seq_builder.getSequenceFrames() * mfcc_dim);
                    }
                } else {
                    // [WS 페이로드 보완] 프론트엔드 차트를 위한 통합 데이터 조립
                    // 구조: [Wave 파형 (N)] + [Power Spectrum (bins)] + [MFCC (39 or 117)]
                    
                    // 1번 축(0번 인덱스)의 데이터를 대표 차트 데이터로 사용
                    memcpy(ws_payload, p->raw_buffer[0][read_idx], N * sizeof(float));
                    memcpy(ws_payload + N, p->dsp.getPowerSpectrum(), bins * sizeof(float));
                    memcpy(ws_payload + N + bins, &feature.features[0][0], mfcc_dim * sizeof(float));

                    p->comm.broadcastBinary(ws_payload, ws_payload_len);
                    
                    if (p->cfg.output.enabled && p->storage.isOpen()) {
                        xQueueSend(p->recorder_queue, &feature, 0);
                    }
                }
            }
        }
    }
    heap_caps_free(seq_buffer);
    heap_caps_free(ws_payload);
    vTaskDelete(nullptr);
}



// --- [3] Core 1: 레코더 전담 태스크  ---
void T20_recorderTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    ST_T20_FeatureVector_t msg;

    for (;;) {
        if (xQueueReceive(p->recorder_queue, &msg, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (p->storage.isOpen()) {
                p->storage.pushVector(&msg);
            }
        } else {
            // Queue 타임아웃(200ms) 시 Idle Flush 트리거
            p->storage.checkIdleFlush();
        }
    }
}

// --- [CL_T20_Mfcc Implementation] ---

CL_T20_Mfcc::CL_T20_Mfcc() : _impl(new ST_Impl()) { g_t20 = this; }
CL_T20_Mfcc::~CL_T20_Mfcc() { stop(); delete _impl; }


bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
    if (p_cfg) _impl->cfg = *p_cfg;
    else _impl->cfg = T20_makeDefaultConfig();

    _impl->frame_queue = xQueueCreate(T20::C10_Sys::QUEUE_LEN, sizeof(uint8_t));
    _impl->recorder_queue = xQueueCreate(32, sizeof(ST_T20_FeatureVector_t));
    _impl->mutex = xSemaphoreCreateMutex();

    if (!_impl->sensor.begin(_impl->cfg.sensor)) return false;
    if (!_impl->dsp.begin(_impl->cfg)) return false;

    
    // 축 개수와 계수 크기를 곱하여 Sequence Builder의 정확한 차원 설정
    uint16_t vector_dim = (p_cfg->feature.mfcc_coeffs * 3) * (uint8_t)p_cfg->feature.axis_count;
    // Sequence Builder 설정 적용
    _impl->seq_builder.begin(_impl->cfg.output.sequence_frames, vector_dim);



    ST_T20_SdmmcProfile_t sd_prof = { "default", true,
        T20::C10_Pin::SDMMC_CLK, T20::C10_Pin::SDMMC_CMD, T20::C10_Pin::SDMMC_D0,
        T20::C10_Pin::SDMMC_D1, T20::C10_Pin::SDMMC_D2, T20::C10_Pin::SDMMC_D3 };
    _impl->storage.begin(sd_prof);

    _impl->comm.begin(_impl->cfg); 
    _impl->comm.initHandlers(_impl);

    _impl->measurement_active = _impl->cfg.system.auto_start;
    pinMode(_impl->cfg.system.button_pin, INPUT_PULLUP);

    return true;
}

bool CL_T20_Mfcc::start() {
    if (_impl->running) return false;
    _impl->running = true;

    xTaskCreatePinnedToCore(
		T20_sensorTask,
		"T20_Sens",
		T20::C10_Task::SENSOR_STACK,
		_impl, T20::C10_Task::SENSOR_PRIO,
		&_impl->sensor_task,
		0
	);

	xTaskCreatePinnedToCore(
		T20_processTask,
		"T20_Proc",
		T20::C10_Task::PROCESS_STACK,
		_impl, T20::C10_Task::PROCESS_PRIO,
		&_impl->process_task,
		1
	);

    xTaskCreatePinnedToCore(
		T20_recorderTask,"T20_Rec",
		T20::C10_Task::RECORDER_STACK,
		_impl,
		T20::C10_Task::RECORDER_PRIO,
		&_impl->recorder_task,
		1
	);

    return true;
}

void CL_T20_Mfcc::stop() {
    _impl->running = false;
    if (_impl->sensor_task) { vTaskDelete(_impl->sensor_task); _impl->sensor_task = nullptr; }
    if (_impl->process_task) { vTaskDelete(_impl->process_task); _impl->process_task = nullptr; }
    if (_impl->recorder_task) { vTaskDelete(_impl->recorder_task); _impl->recorder_task = nullptr; }
    _impl->storage.closeSession();
}

// Watchdog 및 Button 로직
void CL_T20_Mfcc::run() {
    // 내부 구현체 인스턴스 검사
    if (!_impl || !_impl->running) return;
    
    // MQTT 상태 유지보수 루틴 실행
    _impl->comm.runMqtt();

    // 1. 버튼 디바운스 및 제어 (Edge Detection 추가)
    static bool last_btn_state = HIGH; // 풀업 저항 사용 가정 (평상시 HIGH)
    bool current_btn_state = digitalRead(_impl->cfg.system.button_pin);

    // 버튼이 눌리는 순간(Falling Edge)에만 동작하도록 개선
    if (current_btn_state == LOW && last_btn_state == HIGH) {
        if (millis() - _impl->last_btn_ms > T20::C10_Web::BTN_DEBOUNCE_MS) {
            _impl->measurement_active = !_impl->measurement_active;
            _impl->storage.writeEvent(_impl->measurement_active ? "btn_start" : "btn_stop");
            _impl->last_btn_ms = millis();
        }
    }
    // 다음 루프를 위해 상태 업데이트
    last_btn_state = current_btn_state;

    // 2. Watchdog: 샘플 카운터 정지 감지
    static uint32_t wd_last_cnt = 0;
    // 시스템 부팅 직후 바로 Watchdog이 터지는 것을 막기 위해 현재 시간으로 초기화
    static uint32_t wd_last_ms = millis();

    if (millis() - wd_last_ms > _impl->cfg.system.watchdog_ms) {
        // 측정이 활성화되어 있는데도 샘플 카운터가 증가하지 않았다면 센서 먹통으로 간주
        if (_impl->measurement_active && wd_last_cnt == _impl->sample_counter) {
            _impl->storage.writeEvent("watchdog_sensor_stall");
            _impl->sensor.resetHardware();
            _impl->sensor.begin(_impl->cfg.sensor);
        }
        wd_last_cnt = _impl->sample_counter;
        wd_last_ms = millis();
    }
}

void CL_T20_Mfcc::printStatus(Stream& out) const {
    out.printf("--- %s Status ---\n", T20::C10_Sys::VERSION_STR);
    out.printf("Sensor: %s | Measuring: %s\n", _impl->sensor.getStatusText(), _impl->measurement_active ? "ON" : "OFF");
    out.printf("Storage: %s (Records: %lu)\n", _impl->storage.isOpen() ? "Active" : "Idle", _impl->storage.getRecordCount());
    out.printf("WiFi: %s\n", _impl->comm.isConnected() ? "Connected" : "Disconnected");
    out.println("-------------------------");
}

