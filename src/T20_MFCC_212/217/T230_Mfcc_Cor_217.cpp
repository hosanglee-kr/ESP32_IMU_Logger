/* ============================================================================
 * File: T230_Mfcc_Cor_217.cpp
 * Summary: Main Task Logic & Engine Orchestration (v217)
 * Compiler: gnu++17 / Dual-Core RTOS Optimized
 * ========================================================================== */

#include "T220_Mfcc_217.h"
#include "T221_Mfcc_Inter_217.h"

CL_T20_Mfcc* g_t20 = nullptr;

// --- [RTOS Tasks] ---

// Core 0: 고속 센서 데이터 수집 (1600Hz FIFO)
void T20_sensorTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    
    // BMI270 인터럽트(DRDY/FWM) 훅 설치
    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), [](){
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(g_t20->_impl->sensor_task, &woken);
        portYIELD_FROM_ISR(woken);
    }, RISING);

    alignas(16) float raw_batch[32];

    for (;;) {
        // 인터럽트 발생 시까지 대기 (FWM 16샘플 누적 시 트리거)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!p->running) continue;

        // 엔진을 통한 FIFO 배치 읽기
        uint16_t read_cnt = p->sensor.readFifoBatch(raw_batch, 32, p->cfg.sensor.axis);
        
        for (uint16_t i = 0; i < read_cnt; i++) {
            // 핑퐁 버퍼 투입 및 Hop Size 도달 체크 (v216 로직의 엔진화)
            p->raw_buffer[p->write_idx][p->sample_idx++] = raw_batch[i];

            if (p->sample_idx >= T20::C10_DSP::FFT_SIZE) {
                uint8_t ready_idx = p->write_idx;
                xQueueSend(p->frame_queue, &ready_idx, 0);
                
                // 버퍼 스위칭
                p->write_idx = (p->write_idx + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
                p->sample_idx = 0;
            }
        }
    }
}

// Core 1: DSP 연산, 로깅 및 통신
void T20_processTask(void* p_arg) {
    auto* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    uint8_t read_idx;
    ST_T20_FeatureVector_t feature;

    for (;;) {
        if (xQueueReceive(p->frame_queue, &read_idx, portMAX_DELAY) == pdTRUE) {
            // [1] DSP 엔진: MFCC 39차 벡터 추출
            if (p->dsp.processFrame(p->raw_buffer[read_idx], &feature)) {
                
                // [2] Storage 엔진: 세션 활성화 시 기록
                if (p->cfg.recorder.enabled) {
                    p->storage.writeRecord(&feature, sizeof(feature));
                }

                // [3] Comm 엔진: WebSocket 실시간 브로드캐스트
                p->comm.broadcastBinary(feature.vector, feature.vector_len);
            }
        }
    }
}

// --- [CL_T20_Mfcc Implementation] ---

CL_T20_Mfcc::CL_T20_Mfcc() : _impl(new ST_Impl()) {
    g_t20 = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc() {
    stop();
    delete _impl;
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
    if (p_cfg) _impl->cfg = *p_cfg;

    // 1. 센서 엔진 초기화
    if (!_impl->sensor.begin(_impl->cfg.sensor)) return false;

    // 2. DSP 엔진 초기화
    if (!_impl->dsp.begin(_impl->cfg)) return false;

    // 3. 스토리지 엔진 초기화 (SD_MMC 마운트)
    ST_T20_SdmmcProfile_t sd_prof = { "default", true, 
        T20::C10_Pin::SDMMC_CLK, T20::C10_Pin::SDMMC_CMD, T20::C10_Pin::SDMMC_D0,
        T20::C10_Pin::SDMMC_D1, T20::C10_Pin::SDMMC_D2, T20::C10_Pin::SDMMC_D3 };

    _impl->storage.begin(sd_prof);

    // 4. 통신 엔진 초기화 (WiFi & Web)
    _impl->comm.begin(_impl->cfg.wifi);
    _impl->comm.initHandlers(_impl);

    return true;
}

bool CL_T20_Mfcc::start() {
    if (_impl->running) return false;

    _impl->running = true;
    
    // 태스크 생성 (Core 분산)
    xTaskCreatePinnedToCore(T20_sensorTask,  "T20_Sens", 4096, _impl, 5, &_impl->sensor_task, 0);
    xTaskCreatePinnedToCore(T20_processTask, "T20_Proc", 8192, _impl, 4, &_impl->process_task, 1);

    return true;
}

void CL_T20_Mfcc::stop() {
    _impl->running = false;
    if (_impl->sensor_task) { vTaskDelete(_impl->sensor_task); _impl->sensor_task = nullptr; }
    if (_impl->process_task) { vTaskDelete(_impl->process_task); _impl->process_task = nullptr; }
    _impl->storage.closeSession();
}

void CL_T20_Mfcc::printStatus(Stream& out) const {
    out.printf("--- %s Status ---\n", T20::C10_Sys::VERSION);
    out.printf("Sensor: %s\n", _impl->sensor.getStatusText());
    out.printf("Storage: %s (Records: %lu)\n", 
               _impl->storage.isOpen() ? "Active" : "Idle", 
               _impl->storage.getRecordCount());
    out.printf("WiFi: %s\n", _impl->comm.isConnected() ? "Connected" : "Disconnected");
    out.println("-------------------------");
}

