/* ============================================================================
 * File: T230_Mfcc_Cor_213.cpp
 * Summary: 메인 컨트롤러 및 RTOS 태스크 스케줄링 (v210 로직 통합)
 * * [v212 구현 및 점검 사항]
 * 1. Pimpl 패턴의 생성자/소멸자 및 자원 할당 관리
 * 2. Sensor(Core 0) / Process(Core 1) / Recorder(Core 1) 태스크 분산 배치
 * 3. 핑퐁 버퍼(Double Buffering) 인덱스 스위칭 로직 복구
 * 4. Alias Accessor를 통한 v210 상태 로직의 v212 구조체 매핑
 ============================================================================ */

#include "T221_Mfcc_Inter_212.h"

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg) {
    if (_impl == nullptr) return false;

    // 1. 자원 초기화 및 설정 복사
    T20_resetRuntimeResources(_impl);
    _impl->cfg = (p_cfg != nullptr) ? *p_cfg : T20_makeDefaultConfig();

    // 2. 동기화 객체 생성 (Mutex & Queues)
    _impl->mutex = xSemaphoreCreateMutex();
    _impl->frame_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    _impl->recorder_queue = xQueueCreate(16, sizeof(ST_T20_RecorderVectorMessage_t)); // 큐 크기 확장

    // 3. 하드웨어 초기화 (SPI/SDMMC)
    _impl->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);
    T20_initProfiles(_impl);
    T20_initDSP(_impl);
    
    _impl->initialized = true;
    _impl->bmi_state.init = EN_T20_STATE_READY;
    return true;
}

bool CL_T20_Mfcc::start(void) {
    if (_impl == nullptr || !_impl->initialized || _impl->running) return false;

    // Core 분산 배치: 센서는 0번 코어, 연산 및 저장은 1번 코어
    xTaskCreatePinnedToCore(T20_sensorTask, "T20_Sens", G_T20_SENSOR_TASK_STACK, _impl, G_T20_SENSOR_TASK_PRIO, &_impl->sensor_task_handle, 0);
    xTaskCreatePinnedToCore(T20_processTask, "T20_Proc", G_T20_PROCESS_TASK_STACK, _impl, G_T20_PROCESS_TASK_PRIO, &_impl->process_task_handle, 1);
    xTaskCreatePinnedToCore(T20_recorderTask, "T20_Rec", G_T20_RECORDER_TASK_STACK, _impl, G_T20_RECORDER_TASK_PRIO, &_impl->recorder_task_handle, 1);

    _impl->running = true;
    _impl->bmi_state.runtime = EN_T20_STATE_RUNNING;
    return true;
}

void T20_sensorTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    for (;;) {
        if (p == nullptr || !p->running) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        // 1. 실센서(BMI270) 또는 시뮬레이션 데이터 획득
        float sample = 0.0f;
        if (p->live_source_mode == G_T20_LIVE_SOURCE_MODE_BMI270) {
            if (p->bmi270_drdy_isr_flag) {
                T20_bmi270ReadVectorSample(p, &sample);
                p->bmi270_drdy_isr_flag = 0;
            } else {
                vTaskDelay(1); continue; 
            }
        } else {
            T20_fillSyntheticFrame(p, &sample, 1); // 시뮬레이션은 샘플 단위로
            vTaskDelay(pdMS_TO_TICKS(1)); // 약 1000Hz 시뮬레이션
        }

        // 2. 핑퐁 버퍼링 로직
        p->frame_buffer[p->active_fill_buffer][p->active_sample_index++] = sample;

        // 버퍼가 가득 차면 DSP 태스크로 전달
        if (p->active_sample_index >= G_T20_FFT_SIZE) {
            ST_T20_FrameMessage_t msg = { .frame_index = p->active_fill_buffer };
            if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
                p->dropped_frames++;
            }
            // 버퍼 스위칭
            p->active_fill_buffer = (p->active_fill_buffer + 1) % G_T20_RAW_FRAME_BUFFERS;
            p->active_sample_index = 0;
        }
    }
}

