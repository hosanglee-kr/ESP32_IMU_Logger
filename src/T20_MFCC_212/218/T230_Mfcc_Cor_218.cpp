/* ============================================================================
 * File: T230_Mfcc_Cor_218.cpp
 * Summary: Main Task Logic & Engine Orchestration (v217 Full)
 * ========================================================================== */
#include "T220_Mfcc_218.h"
#include "T221_Mfcc_Inter_218.h"
#include "T218_Def_Main_218.h"

CL_T20_Mfcc* g_t20 = nullptr;

// --- [1] Core 0: 고속 센서 데이터 수집 ---
void T20_sensorTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    
    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), [](){
        BaseType_t woken = pdFALSE;
        if(g_t20 && g_t20->_impl && g_t20->_impl->sensor_task) {
            vTaskNotifyGiveFromISR(g_t20->_impl->sensor_task, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }, RISING);

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
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    uint8_t read_idx;
    ST_T20_FeatureVector_t feature;
    uint32_t frame_id = 0;

    // 시퀀스 플랫 데이터를 담을 임시 버퍼 할당
    alignas(16) float seq_buffer[T20::C10_Sys::SEQUENCE_FRAMES_MAX * (T20::C10_DSP::MFCC_COEFFS_MAX * 3)];

    for (;;) {
        if (xQueueReceive(p->frame_queue, &read_idx, portMAX_DELAY) == pdTRUE) {
            // [1] DSP: 39차원 단일 벡터 추출
            if (p->dsp.processFrame(p->raw_buffer[read_idx], &feature)) {
                feature.frame_id = ++frame_id;
                
                // [2] Sequence Builder에 1프레임 푸시 (Sliding Window 갱신)
                p->seq_builder.pushVector(feature.vector);

                // [3] 분기: 사용자가 웹에서 설정한 모드에 따라 처리
                if (p->cfg.output.output_sequence) {
                    // 시퀀스 모드: TinyML 추론을 위해 버퍼가 16프레임 꽉 찼을 때만 동작
                    if (p->seq_builder.isReady()) {
                        p->seq_builder.getSequenceFlat(seq_buffer);
                        
                        // 향후 여기에 TinyML 모델 추론 로직 삽입
                        // float result = my_tinyml_model.predict(seq_buffer);
                        
                        // 현재는 테스트용으로 시퀀스 전체를 웹소켓 브로드캐스트
                        uint16_t total_len = p->seq_builder.getSequenceFrames() * p->seq_builder.getFeatureDim();
                        p->comm.broadcastBinary(seq_buffer, total_len);
                    }
                } else {
                    // 단일 벡터 모드: 기존 로깅 및 웹 차트 모니터링용 (v216 100% 동일)
                    p->comm.broadcastBinary(feature.vector, feature.vector_len);
                    if (p->cfg.output.enabled) {
                        xQueueSend(p->recorder_queue, &feature, 0);
                    }
                }
            }
        }
    }
}

// --- [3] Core 1: 레코더 전담 태스크 (v216 복원) ---
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
    
    xTaskCreatePinnedToCore(T20_sensorTask,  "T20_Sens", T20::C10_Task::SENSOR_STACK, _impl, T20::C10_Task::SENSOR_PRIO, &_impl->sensor_task, 0);
    xTaskCreatePinnedToCore(T20_processTask, "T20_Proc", T20::C10_Task::PROCESS_STACK, _impl, T20::C10_Task::PROCESS_PRIO, &_impl->process_task, 1);
    xTaskCreatePinnedToCore(T20_recorderTask,"T20_Rec",  T20::C10_Task::RECORDER_STACK, _impl, T20::C10_Task::RECORDER_PRIO, &_impl->recorder_task, 1);

    return true;
}

void CL_T20_Mfcc::stop() {
    _impl->running = false;
    if (_impl->sensor_task) { vTaskDelete(_impl->sensor_task); _impl->sensor_task = nullptr; }
    if (_impl->process_task) { vTaskDelete(_impl->process_task); _impl->process_task = nullptr; }
    if (_impl->recorder_task) { vTaskDelete(_impl->recorder_task); _impl->recorder_task = nullptr; }
    _impl->storage.closeSession();
}

// Watchdog 및 Button 로직 (v216 복원)
void CL_T20_Mfcc::run() {
    if (!_impl->running) return;

    // 1. 버튼 디바운스 및 제어
    if (digitalRead(_impl->cfg.system.button_pin) == LOW) {
        if (millis() - _impl->last_btn_ms > T20::C10_Web::BTN_DEBOUNCE_MS) {
            _impl->measurement_active = !_impl->measurement_active;
            _impl->storage.writeEvent(_impl->measurement_active ? "btn_start" : "btn_stop");
            _impl->last_btn_ms = millis();
        }
    }

    // 2. Watchdog: 샘플 카운터 정지 감지
    static uint32_t wd_last_cnt = 0;
    static uint32_t wd_last_ms = 0;
    if (millis() - wd_last_ms > 2000) {
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
