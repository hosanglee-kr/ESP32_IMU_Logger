#include "T20_Mfcc_Inter_011.h"

/*
===============================================================================
소스명: T20_Mfcc_Core_011.cpp
버전: v011

[기능 스펙]
- 모듈 생명주기 관리(begin/start/stop)
- 설정 검증 및 재초기화 정책
- ISR / SensorTask / ProcessTask
- Sample Ring 기반 Sliding Window 프레임 디스패치
- runtime 상태 관리, 출력, sequence 관리

[향후 단계 구현 예정 사항]
- 측정 세션 상태기계 추가
- 버튼 기반 측정 start/stop/marker 추가
- raw / feature / event recorder 연동
- web status snapshot export 연동
===============================================================================
*/

CL_T20_Mfcc* g_t20_instance = nullptr;

void IRAM_ATTR T20_onBmiDrdyISR(void);
void T20_sensorTask(void* p_arg);
void T20_processTask(void* p_arg);

// ============================================================================
// Public API
// ============================================================================

CL_T20_Mfcc::CL_T20_Mfcc()
: _impl(new ST_Impl())
{
    g_t20_instance = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc()
{
    if (_impl != nullptr) {
        T20_resetRuntimeResources(_impl);
        delete _impl;
        _impl = nullptr;
    }

    if (g_t20_instance == this) {
        g_t20_instance = nullptr;
    }
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr) {
        return false;
    }

    T20_resetRuntimeResources(_impl);

    _impl->cfg = (p_cfg != nullptr) ? *p_cfg : T20_makeDefaultConfig();
    if (!T20_validateConfig(&_impl->cfg)) {
        return false;
    }

    _impl->mutex = xSemaphoreCreateMutex();
    if (_impl->mutex == nullptr) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    _impl->frame_queue = xQueueCreate(G_T20_FRAME_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    if (_impl->frame_queue == nullptr) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    _impl->spi.begin(G_T20_PIN_SPI_SCK,
                     G_T20_PIN_SPI_MISO,
                     G_T20_PIN_SPI_MOSI,
                     G_T20_PIN_BMI_CS);

    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);
    pinMode(G_T20_PIN_BMI_INT1, INPUT);

    if (!T20_initDSP(_impl)) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    if (!T20_initBMI270_SPI(_impl)) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    if (!T20_configBMI270_1600Hz_DRDY(_impl)) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    if (!T20_configurePipelineRuntime(_impl)) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    T20_seqInit(&_impl->seq_rb,
                _impl->cfg.output.sequence_frames,
                (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));

    _impl->initialized = true;
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (_impl == nullptr || !_impl->initialized || _impl->running) {
        return false;
    }

    BaseType_t v_r1 = xTaskCreatePinnedToCore(T20_sensorTask,
                                              "T20_Sensor",
                                              G_T20_SENSOR_TASK_STACK,
                                              _impl,
                                              G_T20_SENSOR_TASK_PRIO,
                                              &_impl->sensor_task_handle,
                                              0);

    BaseType_t v_r2 = xTaskCreatePinnedToCore(T20_processTask,
                                              "T20_Process",
                                              G_T20_PROCESS_TASK_STACK,
                                              _impl,
                                              G_T20_PROCESS_TASK_PRIO,
                                              &_impl->process_task_handle,
                                              1);

    if (v_r1 != pdPASS || v_r2 != pdPASS) {
        T20_stopTasks(_impl);
        return false;
    }

    attachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1), T20_onBmiDrdyISR, RISING);
    _impl->running = true;
    return true;
}

void CL_T20_Mfcc::stop(void)
{
    T20_stopTasks(_impl);
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr || _impl->mutex == nullptr || !T20_validateConfig(p_cfg)) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    _impl->cfg = *p_cfg;
    bool v_ok = T20_configurePipelineRuntime(_impl);
    T20_seqInit(&_impl->seq_rb,
                _impl->cfg.output.sequence_frames,
                (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const
{
    if (_impl == nullptr || _impl->mutex == nullptr || p_cfg_out == nullptr) {
        return;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    *p_cfg_out = _impl->cfg;
    xSemaphoreGive(_impl->mutex);
}

bool CL_T20_Mfcc::getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const
{
    if (_impl == nullptr || _impl->mutex == nullptr || p_out == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ok = _impl->latest_vector_valid;
    if (v_ok) {
        *p_out = _impl->latest_feature;
    }

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const
{
    if (_impl == nullptr || _impl->mutex == nullptr || p_out_vec == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ok = _impl->latest_vector_valid;
    uint16_t v_need = _impl->latest_feature.vector_len;
    if (!v_ok || v_need == 0U || p_len < v_need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * v_need);
    xSemaphoreGive(_impl->mutex);
    return true;
}

bool CL_T20_Mfcc::isSequenceReady(void) const
{
    if (_impl == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ready = T20_seqIsReady(&_impl->seq_rb);
    xSemaphoreGive(_impl->mutex);
    return v_ready;
}

bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    if (_impl == nullptr || _impl->mutex == nullptr || p_out_seq == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    uint16_t v_need = (uint16_t)(_impl->seq_rb.frames * _impl->seq_rb.feature_dim);
    if (v_need == 0U || p_len < v_need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    bool v_ok = _impl->latest_sequence_valid;
    if (v_ok) {
        memcpy(p_out_seq, _impl->latest_sequence_flat, sizeof(float) * v_need);
    }

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const
{
    return getLatestSequenceFlat(p_out_seq, p_len);
}

void CL_T20_Mfcc::printConfig(Stream& p_out) const
{
    if (_impl == nullptr || _impl->mutex == nullptr ||
        xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    p_out.println(F("----------- T20_Mfcc Config -----------"));
    p_out.printf("Version         : %s\n", G_T20_VERSION_STR);
    p_out.printf("SampleRate      : %.1f\n", _impl->cfg.feature.sample_rate_hz);
    p_out.printf("Frame Size      : %u\n", _impl->cfg.feature.frame_size);
    p_out.printf("Hop Size        : %u\n", _impl->cfg.feature.hop_size);
    p_out.printf("Mel Filters     : %u\n", _impl->cfg.feature.mel_filters);
    p_out.printf("MFCC Coeffs     : %u\n", _impl->cfg.feature.mfcc_coeffs);
    p_out.printf("Feature Dim     : %u\n", (unsigned)(_impl->cfg.feature.mfcc_coeffs * 3U));
    p_out.printf("Delta Window    : %u\n", _impl->cfg.feature.delta_window);
    p_out.printf("Output Mode     : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
    p_out.printf("Seq Frames      : %u\n", _impl->cfg.output.sequence_frames);
    p_out.printf("Stage Count     : %u\n", _impl->cfg.pipeline.stage_count);
    p_out.println(F("----------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

void CL_T20_Mfcc::printStatus(Stream& p_out) const
{
    if (_impl == nullptr || _impl->mutex == nullptr ||
        xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    p_out.println(F("----------- T20_Mfcc Status -----------"));
    p_out.printf("Initialized     : %s\n", _impl->initialized ? "YES" : "NO");
    p_out.printf("Running         : %s\n", _impl->running ? "YES" : "NO");
    p_out.printf("Dropped Frames  : %lu\n", (unsigned long)_impl->dropped_frames);
    p_out.printf("Total Samples   : %lu\n", (unsigned long)_impl->total_samples_written);
    p_out.printf("Next FrameStart : %lu\n", (unsigned long)_impl->next_frame_start_sample);
    p_out.printf("History Count   : %u\n", _impl->mfcc_history_count);
    p_out.printf("Noise Learned   : %u\n", _impl->noise_learned_frames);
    p_out.printf("Seq Frames      : %u\n", _impl->seq_rb.frames);
    p_out.printf("Seq Feature Dim : %u\n", _impl->seq_rb.feature_dim);
    p_out.printf("Seq Full        : %s\n", _impl->seq_rb.full ? "YES" : "NO");
    p_out.printf("Vector Valid    : %s\n", _impl->latest_vector_valid ? "YES" : "NO");
    p_out.printf("Sequence Valid  : %s\n", _impl->latest_sequence_valid ? "YES" : "NO");
    p_out.println(F("---------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

void CL_T20_Mfcc::printLatest(Stream& p_out) const
{
    if (_impl == nullptr || _impl->mutex == nullptr ||
        xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    if (!_impl->latest_vector_valid) {
        xSemaphoreGive(_impl->mutex);
        p_out.println(F("No latest feature available."));
        return;
    }

    ST_T20_FeatureVector_t v_feat = _impl->latest_feature;
    xSemaphoreGive(_impl->mutex);

    p_out.print(F("Log Mel      : "));
    for (uint16_t v_i = 0; v_i < v_feat.log_mel_len; ++v_i) p_out.printf("%.4f ", v_feat.log_mel[v_i]);
    p_out.println();

    p_out.print(F("MFCC         : "));
    for (uint16_t v_i = 0; v_i < v_feat.mfcc_len; ++v_i) p_out.printf("%.4f ", v_feat.mfcc[v_i]);
    p_out.println();

    p_out.print(F("Delta        : "));
    for (uint16_t v_i = 0; v_i < v_feat.delta_len; ++v_i) p_out.printf("%.4f ", v_feat.delta[v_i]);
    p_out.println();

    p_out.print(F("DeltaDelta   : "));
    for (uint16_t v_i = 0; v_i < v_feat.delta2_len; ++v_i) p_out.printf("%.4f ", v_feat.delta2[v_i]);
    p_out.println();

    p_out.printf("Vector Len    : %u\n", v_feat.vector_len);
}

// ============================================================================
// ISR / Tasks
// ============================================================================

void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    if (g_t20_instance == nullptr || g_t20_instance->_impl == nullptr) {
        return;
    }

    CL_T20_Mfcc::ST_Impl* p = g_t20_instance->_impl;
    BaseType_t v_hp_task_woken = pdFALSE;

    if (p->sensor_task_handle != nullptr) {
        vTaskNotifyGiveFromISR(p->sensor_task_handle, &v_hp_task_woken);
    }

    if (v_hp_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void T20_sensorTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);

    for (;;) {
        uint32_t v_notify_count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!p->running) {
            continue;
        }

        while (v_notify_count-- > 0U) {
            uint16_t v_interrupt_status = 0U;
            if (p->imu.getInterruptStatus(&v_interrupt_status) != BMI2_OK) {
                continue;
            }

            if ((v_interrupt_status & BMI2_ACC_DRDY_INT_MASK) == 0U) {
                continue;
            }

            if (p->imu.getSensorData() != BMI2_OK) {
                continue;
            }

            float v_sample = T20_selectAxisSample(p);
            p->sample_ring[p->sample_write_index] = v_sample;
            p->sample_write_index = (uint32_t)((p->sample_write_index + 1U) % G_T20_SAMPLE_RING_SIZE);
            p->total_samples_written++;

            T20_tryEnqueueReadyFrames(p);
        }
    }
}

void T20_processTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_FrameMessage_t v_msg;

    for (;;) {
        if (xQueueReceive(p->frame_queue, &v_msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!p->running) {
            continue;
        }

        ST_T20_ConfigSnapshot_t v_cfg_snap;
        ST_T20_PipelineSnapshot_t v_pipe_snap;

        if (!T20_buildConfigSnapshot(p, &v_cfg_snap)) {
            continue;
        }

        if (!T20_buildPipelineSnapshot(&v_cfg_snap.cfg, &v_pipe_snap)) {
            continue;
        }

        if (!T20_copyFrameFromRing(p,
                                   v_msg.frame_start_sample,
                                   p->frame_time,
                                   v_cfg_snap.cfg.feature.frame_size)) {
            continue;
        }

        float v_mfcc[G_T20_MFCC_COEFFS_MAX] = {0};
        float v_delta[G_T20_MFCC_COEFFS_MAX] = {0};
        float v_delta2[G_T20_MFCC_COEFFS_MAX] = {0};

        T20_computeMFCC(p,
                        &v_cfg_snap,
                        &v_pipe_snap,
                        p->frame_time,
                        v_mfcc);

        T20_pushMfccHistory(p, v_mfcc, v_cfg_snap.cfg.feature.mfcc_coeffs);
        T20_computeDeltaFromHistory(p,
                                    v_cfg_snap.cfg.feature.mfcc_coeffs,
                                    v_cfg_snap.cfg.feature.delta_window,
                                    v_delta);
        T20_computeDeltaDeltaFromHistory(p,
                                         v_cfg_snap.cfg.feature.mfcc_coeffs,
                                         v_delta2);

        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            p->latest_feature.log_mel_len = v_cfg_snap.cfg.feature.mel_filters;
            p->latest_feature.mfcc_len = v_cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.delta_len = v_cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.delta2_len = v_cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.vector_len = v_cfg_snap.vector_len;

            memcpy(p->latest_feature.log_mel, p->log_mel, sizeof(float) * v_cfg_snap.cfg.feature.mel_filters);
            memcpy(p->latest_feature.mfcc, v_mfcc, sizeof(float) * v_cfg_snap.cfg.feature.mfcc_coeffs);
            memcpy(p->latest_feature.delta, v_delta, sizeof(float) * v_cfg_snap.cfg.feature.mfcc_coeffs);
            memcpy(p->latest_feature.delta2, v_delta2, sizeof(float) * v_cfg_snap.cfg.feature.mfcc_coeffs);

            T20_buildVector(v_mfcc,
                            v_delta,
                            v_delta2,
                            v_cfg_snap.cfg.feature.mfcc_coeffs,
                            p->latest_feature.vector);
            p->latest_vector_valid = true;

            T20_updateOutput(p);
            xSemaphoreGive(p->mutex);
        }
    }
}

// ============================================================================
// Core Helpers
// ============================================================================

bool T20_validateConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) {
        return false;
    }

    if (p_cfg->feature.frame_size != G_T20_FRAME_SIZE_FIXED) {
        return false;
    }

    if (p_cfg->feature.hop_size == 0U || p_cfg->feature.hop_size > p_cfg->feature.frame_size) {
        return false;
    }

    if (p_cfg->feature.sample_rate_hz <= 0.0f) {
        return false;
    }

    if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS_FIXED) {
        return false;
    }

    if (p_cfg->feature.mfcc_coeffs == 0U || p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) {
        return false;
    }

    if (p_cfg->feature.mfcc_coeffs > p_cfg->feature.mel_filters) {
        return false;
    }

    if (p_cfg->feature.delta_window == 0U || p_cfg->feature.delta_window > (G_T20_MFCC_HISTORY / 2U)) {
        return false;
    }

    if (p_cfg->output.sequence_frames == 0U || p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) {
        return false;
    }

    if (p_cfg->pipeline.stage_count > G_T20_PREPROCESS_STAGE_MAX) {
        return false;
    }

    const float v_nyquist = p_cfg->feature.sample_rate_hz * 0.5f;
    for (uint16_t v_i = 0; v_i < p_cfg->pipeline.stage_count; ++v_i) {
        const ST_T20_PreprocessStageConfig_t& v_s = p_cfg->pipeline.stages[v_i];
        if (!v_s.enable) {
            continue;
        }

        switch (v_s.stage_type) {
            case EN_T20_STAGE_PREEMPHASIS:
                if (v_s.param_1 < 0.0f || v_s.param_1 > 1.0f) return false;
                break;
            case EN_T20_STAGE_NOISE_GATE:
                if (v_s.param_1 < 0.0f) return false;
                break;
            case EN_T20_STAGE_BIQUAD_LPF:
            case EN_T20_STAGE_BIQUAD_HPF:
                if (v_s.param_1 <= 0.0f || v_s.param_1 >= v_nyquist || v_s.q_factor <= 0.0f) return false;
                break;
            case EN_T20_STAGE_BIQUAD_BPF:
                if (v_s.param_1 <= 0.0f || v_s.param_2 <= v_s.param_1 || v_s.param_2 >= v_nyquist || v_s.q_factor <= 0.0f) return false;
                break;
            default:
                break;
        }
    }

    if (p_cfg->noise.enable_spectral_subtract) {
        if (p_cfg->noise.noise_learn_frames == 0U || p_cfg->noise.spectral_subtract_strength < 0.0f) {
            return false;
        }
    }

    return true;
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    detachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1));

    if (p->sensor_task_handle != nullptr) {
        vTaskDelete(p->sensor_task_handle);
        p->sensor_task_handle = nullptr;
    }

    if (p->process_task_handle != nullptr) {
        vTaskDelete(p->process_task_handle);
        p->process_task_handle = nullptr;
    }

    p->running = false;
}

void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    if (p->frame_queue != nullptr) {
        vQueueDelete(p->frame_queue);
        p->frame_queue = nullptr;
    }

    if (p->mutex != nullptr) {
        vSemaphoreDelete(p->mutex);
        p->mutex = nullptr;
    }
}

void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    p->initialized = false;
    p->running = false;
    p->sample_write_index = 0U;
    p->total_samples_written = 0U;
    p->next_frame_start_sample = 0U;
    p->dropped_frames = 0U;
    p->mfcc_history_count = 0U;
    p->latest_vector_valid = false;
    p->latest_sequence_valid = false;
    p->prev_raw_sample = 0.0f;
    p->noise_learned_frames = 0U;

    memset(p->sample_ring, 0, sizeof(p->sample_ring));
    memset(p->frame_time, 0, sizeof(p->frame_time));
    memset(p->frame_stage_a, 0, sizeof(p->frame_stage_a));
    memset(p->frame_stage_b, 0, sizeof(p->frame_stage_b));
    memset(p->window, 0, sizeof(p->window));
    memset(p->fft_buffer, 0, sizeof(p->fft_buffer));
    memset(p->power, 0, sizeof(p->power));
    memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mel_bank, 0, sizeof(p->mel_bank));
    memset(p->mfcc_history, 0, sizeof(p->mfcc_history));
    memset(&p->latest_feature, 0, sizeof(p->latest_feature));
    memset(&p->seq_rb, 0, sizeof(p->seq_rb));
    memset(p->latest_sequence_flat, 0, sizeof(p->latest_sequence_flat));
    memset(&p->pipeline_runtime, 0, sizeof(p->pipeline_runtime));
}

void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    T20_stopTasks(p);
    T20_releaseSyncObjects(p);
    T20_clearRuntimeState(p);
}

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p)
{
    switch (p->cfg.input.axis) {
        case EN_T20_AXIS_X: return p->imu.data.accelX;
        case EN_T20_AXIS_Y: return p->imu.data.accelY;
        case EN_T20_AXIS_Z:
        default:
            return p->imu.data.accelZ;
    }
}

bool T20_copyFrameFromRing(CL_T20_Mfcc::ST_Impl* p,
                           uint32_t p_frame_start_sample,
                           float* p_out_frame,
                           uint16_t p_frame_size)
{
    if (p == nullptr || p_out_frame == nullptr || p_frame_size == 0U) {
        return false;
    }

    // NOTE:
    // This is a simple logical-index to ring-index copy path for Bundle-A.
    // Future zero-copy optimization may replace this with DMA/cache-aware chunk handling.
    uint32_t v_total_written = p->total_samples_written;
    if (v_total_written < (p_frame_start_sample + p_frame_size)) {
        return false;
    }

    uint32_t v_oldest_available = (v_total_written > G_T20_SAMPLE_RING_SIZE)
                                ? (v_total_written - G_T20_SAMPLE_RING_SIZE)
                                : 0U;
    if (p_frame_start_sample < v_oldest_available) {
        return false;
    }

    for (uint16_t v_i = 0; v_i < p_frame_size; ++v_i) {
        uint32_t v_abs = p_frame_start_sample + v_i;
        uint32_t v_idx = v_abs % G_T20_SAMPLE_RING_SIZE;
        p_out_frame[v_i] = p->sample_ring[v_idx];
    }

    return true;
}

void T20_tryEnqueueReadyFrames(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    const uint16_t v_frame_size = p->cfg.feature.frame_size;
    const uint16_t v_hop = p->cfg.feature.hop_size;

    while (p->total_samples_written >= (p->next_frame_start_sample + v_frame_size)) {
        ST_T20_FrameMessage_t v_msg;
        v_msg.frame_start_sample = p->next_frame_start_sample;

        if (xQueueSend(p->frame_queue, &v_msg, 0) != pdTRUE) {
            ST_T20_FrameMessage_t v_old;
            if (xQueueReceive(p->frame_queue, &v_old, 0) == pdTRUE) {
                p->dropped_frames++;
                if (xQueueSend(p->frame_queue, &v_msg, 0) != pdTRUE) {
                    p->dropped_frames++;
                }
            } else {
                p->dropped_frames++;
            }
        }

        p->next_frame_start_sample += v_hop;
    }
}

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p,
                         const float* p_mfcc,
                         uint16_t p_dim)
{
    if (p == nullptr || p_mfcc == nullptr || p_dim == 0U) {
        return;
    }

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * p_dim);
        p->mfcc_history_count++;
    } else {
        for (uint16_t v_i = 0; v_i < (G_T20_MFCC_HISTORY - 1U); ++v_i) {
            memcpy(p->mfcc_history[v_i], p->mfcc_history[v_i + 1U], sizeof(float) * p_dim);
        }
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1U], p_mfcc, sizeof(float) * p_dim);
    }
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                 uint16_t p_dim,
                                 uint16_t p_delta_window,
                                 float* p_delta_out)
{
    memset(p_delta_out, 0, sizeof(float) * p_dim);
    if (p == nullptr || p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const uint16_t v_center = (G_T20_MFCC_HISTORY / 2U);
    float v_den = 0.0f;
    for (uint16_t v_n = 1U; v_n <= p_delta_window; ++v_n) {
        v_den += (float)(v_n * v_n);
    }
    v_den *= 2.0f;

    for (uint16_t v_c = 0; v_c < p_dim; ++v_c) {
        float v_num = 0.0f;
        for (uint16_t v_n = 1U; v_n <= p_delta_window; ++v_n) {
            float v_plus = p->mfcc_history[v_center + v_n][v_c];
            float v_minus = p->mfcc_history[v_center - v_n][v_c];
            v_num += (float)v_n * (v_plus - v_minus);
        }
        p_delta_out[v_c] = v_num / (v_den + G_T20_EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                      uint16_t p_dim,
                                      float* p_delta2_out)
{
    memset(p_delta2_out, 0, sizeof(float) * p_dim);
    if (p == nullptr || p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const uint16_t v_center = (G_T20_MFCC_HISTORY / 2U);
    for (uint16_t v_c = 0; v_c < p_dim; ++v_c) {
        float v_prev = p->mfcc_history[v_center - 1U][v_c];
        float v_curr = p->mfcc_history[v_center][v_c];
        float v_next = p->mfcc_history[v_center + 1U][v_c];
        p_delta2_out[v_c] = v_next - (2.0f * v_curr) + v_prev;
    }
}

void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec)
{
    if (p_out_vec == nullptr) {
        return;
    }

    uint16_t v_idx = 0U;
    for (uint16_t v_i = 0; v_i < p_dim; ++v_i) p_out_vec[v_idx++] = p_mfcc[v_i];
    for (uint16_t v_i = 0; v_i < p_dim; ++v_i) p_out_vec[v_idx++] = p_delta[v_i];
    for (uint16_t v_i = 0; v_i < p_dim; ++v_i) p_out_vec[v_idx++] = p_delta2[v_i];
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb,
                 uint16_t p_frames,
                 uint16_t p_feature_dim)
{
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
    p_rb->head = 0U;
    p_rb->full = false;
}

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb,
                 const float* p_feature_vec)
{
    memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * p_rb->feature_dim);
    p_rb->head++;
    if (p_rb->head >= p_rb->frames) {
        p_rb->head = 0U;
        p_rb->full = true;
    }
}

bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb)
{
    return (p_rb != nullptr) ? p_rb->full : false;
}

void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb,
                          float* p_out_flat)
{
    uint16_t v_start = p_rb->full ? p_rb->head : 0U;
    for (uint16_t v_i = 0; v_i < p_rb->frames; ++v_i) {
        uint16_t v_idx = (uint16_t)((v_start + v_i) % p_rb->frames);
        memcpy(&p_out_flat[v_i * p_rb->feature_dim],
               p_rb->data[v_idx],
               sizeof(float) * p_rb->feature_dim);
    }
}

void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->latest_vector_valid) {
        return;
    }

    if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) {
        p->latest_sequence_valid = false;
        return;
    }

    if (p->latest_feature.vector_len == 0U || p->latest_feature.vector_len != p->seq_rb.feature_dim) {
        p->latest_sequence_valid = false;
        return;
    }

    T20_seqPush(&p->seq_rb, p->latest_feature.vector);
    if (p->cfg.output.sequence_flatten) {
        T20_seqExportFlatten(&p->seq_rb, p->latest_sequence_flat);
    }
    p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}
