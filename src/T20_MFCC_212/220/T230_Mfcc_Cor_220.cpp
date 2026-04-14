/* ============================================================================
 * File: T230_Mfcc_Cor_220.cpp
 * Summary: Main Task Logic & Engine Orchestration
 * ========================================================================== */
#include "T220_Mfcc_220.h"
#include "T221_Mfcc_Inter_220.h"
#include "T210_Def_221.h"

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

// Core 1: DSP 연산 및 특징량 조립, 스마트 트리거, MQTT 제어 태스크
void T20_processTask(void* p_arg) {
    auto* p = static_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    if (!p) { vTaskDelete(nullptr); return; }

    uint8_t read_idx;
    ST_T20_FeatureVector_t feature;
    uint32_t frame_id = 0;

    const uint16_t axis_cnt = (uint8_t)p->cfg.feature.axis_count;
    const uint16_t mfcc_dim = 39 * axis_cnt;

    // [1] 시퀀스 전송용 메모리 할당 (16바이트 정렬, 내부 RAM 사용으로 캐시 미스 방지)
    float* seq_buffer = (float*)heap_caps_aligned_alloc(16, T20::C10_Sys::SEQUENCE_FRAMES_MAX * mfcc_dim * sizeof(float), MALLOC_CAP_INTERNAL);

    // [2] 단일 프레임 WS 전송용 통합 버퍼 할당 (프론트엔드 파형 + 스펙트럼 + MFCC 차트용)
    const uint16_t N = p->cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;
    const size_t ws_payload_len = N + bins + mfcc_dim;
    float* ws_payload = (float*)heap_caps_aligned_alloc(16, ws_payload_len * sizeof(float), MALLOC_CAP_INTERNAL);

    for (;;) {
        if (xQueueReceive(p->frame_queue, &read_idx, portMAX_DELAY) == pdTRUE) {

            // [3] 타임스탬프 획득 및 NTP 동기화 상태 플래그 설정
            struct timeval tv;
            gettimeofday(&tv, NULL);
            feature.timestamp_ms = (uint64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
            feature.frame_id = ++frame_id;
            feature.active_axes = axis_cnt;
            feature.status_flags = (tv.tv_sec > 1000000) ? 0x01 : 0x00; // Bit 0: NTP 동기화 여부

            // [4] Raw 파형 데이터 3축 인터리빙 병합 저장 (설정 켜짐 시)
            if (p->cfg.storage.save_raw) {
                p->storage.pushRaw(p->raw_buffer[0][read_idx], p->raw_buffer[1][read_idx], p->raw_buffer[2][read_idx], N, axis_cnt);
            }

            bool all_ready = true;

            // [5] 3축(또는 1축) 독립 DSP 연산 수행 및 특징량 추출
            for (uint8_t a = 0; a < axis_cnt; a++) {
                if (!p->dsp.processFrame(p->raw_buffer[a][read_idx], &feature, a)) {
                    all_ready = false;
                    break;
                }
            }

            // 모든 축 연산이 완료된 시점에만 트리거 판별 및 저장 진행
            if (all_ready) {
                bool event_detected = false;

                // [6] 스마트 트리거 판별: RMS 진동 세기 검사 (전체 활성 축 대상)
                if (p->cfg.trigger.use_threshold) {
                    for (uint8_t a = 0; a < axis_cnt; a++) {
                        if (feature.rms[a] > p->cfg.trigger.threshold_rms) {
                            event_detected = true;
                        }
                    }
                }

                // [7] 스마트 트리거 판별: 다중 주파수 대역(Multi-band) 에너지 검사 (기준 축 0번 대상)
                for (int b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) {
                    if (p->cfg.trigger.bands[b].enable) {
                        float band_nrg = p->dsp.getBandEnergy(p->cfg.trigger.bands[b].start_hz, p->cfg.trigger.bands[b].end_hz);
                        feature.band_energy[b] = band_nrg;

                        if (band_nrg > p->cfg.trigger.bands[b].threshold) {
                            event_detected = true;
                        }
                    } else {
                        feature.band_energy[b] = 0.0f; // 비활성 밴드는 0으로 초기화
                    }
                }

                // [8] 트리거 이벤트 발생 시 SD 레코딩 세션 및 MQTT 경보 제어
                if (event_detected) {
                    feature.status_flags |= 0x02; // Bit 1: Triggered 플래그 활성화

                    // MQTT 알림 전송 (연속 알림 폭주를 방지하기 위한 5초 쿨타임)
                    if (millis() - p->last_trigger_ms > 5000) {
                        JsonDocument alert_doc;
                        alert_doc["event"] = "smart_trigger_alert";
                        alert_doc["timestamp_ms"] = feature.timestamp_ms;

                        JsonArray rms_arr = alert_doc["rms"].to<JsonArray>();
                        for(uint8_t a = 0; a < axis_cnt; a++) rms_arr.add(feature.rms[a]);

                        JsonArray bnd_arr = alert_doc["bands"].to<JsonArray>();
                        for(uint8_t b = 0; b < T20::C10_DSP::TRIGGER_BANDS_MAX; b++) bnd_arr.add(feature.band_energy[b]);

                        p->comm.publishMqtt("alert", alert_doc);
                    }

                    p->last_trigger_ms = millis();

                    // 레코딩이 닫혀있다면 이벤트 시점에 즉시 자동 시작
                    if (!p->storage.isOpen()) {
                        p->storage.openSession(p->cfg);
                        p->storage.writeEvent("smart_trigger_started");
                    }
                } else {
                    // 이벤트 소멸 후 5초(Hold Time) 유지 후 세션 자동 종료
                    if (p->storage.isOpen() && (millis() - p->last_trigger_ms > 5000)) {
                        p->storage.closeSession("trigger_hold_timeout");
                    }
                }

                // [9] 추출된 MFCC/특징량 벡터를 시퀀스 빌더(링 버퍼)에 푸시
                p->seq_builder.pushVector(&feature.features[0][0]);

                // [10] 프론트엔드(Web UI) 및 SD 스토리지로 데이터 전송/저장 분기
                if (p->cfg.output.output_sequence) {
                    // (A) 시퀀스 텐서 모드: 설정된 프레임 단위로 묶어서 WS 전송
                    if (p->seq_builder.isReady() && (frame_id % p->cfg.output.sequence_frames == 0)) {
                        p->seq_builder.getSequenceFlat(seq_buffer);
                        p->comm.broadcastBinary(seq_buffer, p->seq_builder.getSequenceFrames() * mfcc_dim);
                    }
                } else {
                    // (B) 단일 벡터 모드: 실시간 차트 렌더링을 위한 통합 페이로드 조립
                    // 데이터 구조: [Wave 파형 (N)] + [Power Spectrum (bins)] + [MFCC (39*축)]
                    memcpy(ws_payload, p->raw_buffer[0][read_idx], N * sizeof(float));
                    memcpy(ws_payload + N, p->dsp.getPowerSpectrum(), bins * sizeof(float));
                    memcpy(ws_payload + N + bins, &feature.features[0][0], mfcc_dim * sizeof(float));

                    p->comm.broadcastBinary(ws_payload, ws_payload_len);

                    // 수동 측정 상태이거나 스마트 트리거로 파일이 열려있을 때 SD 저장 큐로 전송
                    if (p->cfg.output.enabled && p->storage.isOpen()) {
                        xQueueSend(p->recorder_queue, &feature, 0);
                    }
                }
            }
        }
    }

    // Task 종료 시 안전하게 힙 메모리 해제 (무한 루프 구조상 도달하지는 않음)
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
CL_T20_Mfcc::CL_T20_Mfcc() {
    // 192KB 거대 구조체를 PSRAM에 안전하게 할당
    _impl = (ST_Impl*)heap_caps_malloc(sizeof(ST_Impl), MALLOC_CAP_SPIRAM);
    if (_impl != nullptr) {
        // 확보된 PSRAM 메모리 공간에 객체 생성자(초기화 리스트 등) 호출
        new (_impl) ST_Impl(); 
    } else {
        Serial.println(F("[Critical] PSRAM Allocation Failed for ST_Impl!"));
    }
    g_t20 = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc() { 
    stop(); 
    if (_impl) {
        _impl->~ST_Impl(); // 소멸자 명시적 호출
        heap_caps_free(_impl);
        _impl = nullptr;
    }
}



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

    // [딥슬립 로직] 설정된 시간 동안 아무런 트리거 이벤트가 없으면 진입
    if (_impl->cfg.trigger.use_deep_sleep) {
        // last_trigger_ms가 0이면 부팅 직후이므로, 부팅 후 또는 마지막 트리거 이후 시간 검사
        uint32_t idle_time = millis() - _impl->last_trigger_ms;

        // 주의: 수동으로 시작한 경우(measurement_active == true)는 딥슬립 무시
        if (!_impl->measurement_active && idle_time > (_impl->cfg.trigger.sleep_timeout_sec * 1000)) {
            Serial.println(F("[Power] Entering Deep Sleep due to inactivity..."));

            // 센서 Any-Motion Wakeup 인터럽트 설정
            _impl->sensor.enableWakeOnMotion(_impl->cfg.trigger.threshold_rms, _impl->cfg.trigger.any_motion_duration);

            // SD 카드 및 시스템 안전 종료
            _impl->storage.closeSession("system_sleep");
            delay(500);

            // INT1 핀을 통해 깨어나도록 설정 (BMI270_INT1 = GPIO14 가정)
            esp_sleep_enable_ext0_wakeup((gpio_num_t)T20::C10_Pin::BMI_INT1, 1); // 1 = High

            esp_deep_sleep_start();
        }
    }
}

void CL_T20_Mfcc::printStatus(Stream& out) const {
    out.printf("--- %s Status ---\n", T20::C10_Sys::VERSION_STR);
    out.printf("Sensor: %s | Measuring: %s\n", _impl->sensor.getStatusText(), _impl->measurement_active ? "ON" : "OFF");
    out.printf("Storage: %s (Records: %lu)\n", _impl->storage.isOpen() ? "Active" : "Idle", _impl->storage.getRecordCount());
    out.printf("WiFi: %s\n", _impl->comm.isConnected() ? "Connected" : "Disconnected");
    out.println("-------------------------");
}

