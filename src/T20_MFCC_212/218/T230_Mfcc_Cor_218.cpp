/* ============================================================================
 * File: T230_Mfcc_Cor_218.cpp
 * Summary: Main Task Logic & Engine Orchestration (v217 Full)
 * ========================================================================== */
#include "T220_Mfcc_218.h"
#include "T221_Mfcc_Inter_218.h"
#include "T218_Def_Main_218.h"

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

	// 현재 실행 중인 태스크의 핸들을 ISR 전용 변수에 저장
    // xTaskCreate의 동기화 타이밍 이슈를 완벽히 방지하기 위해 자기 자신의 핸들을 가져옵니다.
    g_isr_sensor_task = xTaskGetCurrentTaskHandle();

    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    // [수정] 람다 대신 정적 함수 매핑
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), T20_bmi_isr_handler, RISING);

    alignas(16) float raw_batch[32];

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->running || !p->measurement_active) continue;

        uint16_t read_cnt = p->sensor.readFifoBatch(raw_batch, 32, p->cfg.sensor.axis);

        for (uint16_t i = 0; i < read_cnt; i++) {
            p->sample_counter++;
            p->raw_buffer[p->active_fill_buffer][p->active_sample_index++] = raw_batch[i];

            if ((p->active_sample_index % p->cfg.feature.hop_size) == 0 && p->active_sample_index >= T20::C10_DSP::FFT_SIZE) {
                uint8_t ready_idx = p->active_fill_buffer;
                if (xQueueSend(p->frame_queue, &ready_idx, 0) == pdPASS) {
                    p->active_fill_buffer = (p->active_fill_buffer + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                    p->active_sample_index = 0;
                }
            }
        }
    }
}



// --- [2] Core 1: DSP 연산 및 브로드캐스트 ---
void T20_processTask(void* p_arg) {
    // 1. 포인터 캐스팅 및 예외 처리
    auto* p = static_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    if (!p) {
        vTaskDelete(nullptr);
        return;
    }

    uint8_t read_idx;
    ST_T20_FeatureVector_t feature;
    uint32_t frame_id = 0;

    // 2. 시퀀스 플랫 데이터를 담을 버퍼 할당 (Stack Overflow 방지를 위해 Heap 동적 할당)
    // alignas(16) 대신 ESP32에 최적화된 heap_caps_malloc 사용 (내부 RAM, 8비트 접근 가능 영역)
    const size_t seq_buffer_elements = T20::C10_Sys::SEQUENCE_FRAMES_MAX * (T20::C10_DSP::MFCC_COEFFS_MAX * 3);
    const size_t seq_buffer_size_bytes = seq_buffer_elements * sizeof(float);

    float* seq_buffer = static_cast<float*>(heap_caps_malloc(seq_buffer_size_bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

    // 메모리 할당 실패 시 태스크 종료
    if (!seq_buffer) {
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        // 큐 수신 대기 (portMAX_DELAY로 무한 대기)
        if (xQueueReceive(p->frame_queue, &read_idx, portMAX_DELAY) == pdTRUE) {

            // [1] Raw 파형 저장 옵션이 켜져 있으면, DSP 처리 전 샘플 통째로 넘김
            if (p->cfg.storage.save_raw) {
                p->storage.pushRaw(p->raw_buffer[read_idx], T20::C10_DSP::FFT_SIZE);
            }

            // [2] DSP: 39차원 단일 벡터 추출
            if (p->dsp.processFrame(p->raw_buffer[read_idx], &feature)) {
                feature.frame_id = ++frame_id;

                // [3] Sequence Builder에 1프레임 푸시 (Sliding Window 갱신)
                p->seq_builder.pushVector(feature.vector);

                // [4] 분기: 사용자가 웹에서 설정한 모드에 따라 처리
                if (p->cfg.output.output_sequence) {
                    if (p->seq_builder.isReady()) {
                        // 수정: 설정된 시퀀스 프레임(예: 16)이 완전히 새로 채워졌을 때만(Overlap 없이) 전송하여 UI 과부하 방지
                        if (feature.frame_id % p->cfg.output.sequence_frames == 0) {
                            p->seq_builder.getSequenceFlat(seq_buffer);

							// [ *** 삭제 금지 *** ] 향후 여기에 TinyML 모델 추론 로직 삽입
							// float result = my_tinyml_model.predict(seq_buffer);

							// 현재는 테스트용으로 시퀀스 전체를 웹소켓 브로드캐스트
                            uint16_t total_elements = p->seq_builder.getSequenceFrames() * p->seq_builder.getFeatureDim();
							// 내부에서 sizeof(float)를 곱하므로 원소 개수만 전달하면 됨
                            p->comm.broadcastBinary(seq_buffer, total_elements);
                        }
                    }
                } else {
                    // 단일 벡터 모드: 기존 로깅 및 웹 차트 모니터링용
                    // [수정됨] 내부에서 sizeof(float)를 곱하므로 원소 개수만 전달
    				p->comm.broadcastBinary(feature.vector, feature.vector_len);

                    if (p->cfg.output.enabled) {
                        xQueueSend(p->recorder_queue, &feature, 0);
                    }
                }
            }
        }
    }

    // 안전을 위한 메모리 해제 (실제로는 무한 루프이므로 도달하지 않음)
    heap_caps_free(seq_buffer);
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

    // Sequence Builder 설정 적용
    _impl->seq_builder.begin(_impl->cfg.output.sequence_frames, _impl->cfg.feature.mfcc_coeffs * 3);


    ST_T20_SdmmcProfile_t sd_prof = { "default", true,
        T20::C10_Pin::SDMMC_CLK, T20::C10_Pin::SDMMC_CMD, T20::C10_Pin::SDMMC_D0,
        T20::C10_Pin::SDMMC_D1, T20::C10_Pin::SDMMC_D2, T20::C10_Pin::SDMMC_D3 };
    _impl->storage.begin(sd_prof);

    _impl->comm.begin(_impl->cfg.wifi);
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


