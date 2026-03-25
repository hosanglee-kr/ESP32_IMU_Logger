#include "T20_Mfcc_Inter_009.h"

/*
===============================================================================
소스명: T20_Mfcc_Core_009.cpp
버전: v009

[기능 스펙]
- 모듈 생명주기 관리(begin/start/stop)
- 설정 검증 및 재초기화 정책
- ISR / SensorTask / ProcessTask
- runtime 상태 관리, 출력, 시퀀스 관리
===============================================================================
*/

CL_T20_Mfcc* g_t20_instance = nullptr;

void IRAM_ATTR T20_onBmiDrdyISR(void);
void T20_sensorTask(void* p_arg);
void T20_processTask(void* p_arg);

// ============================================================================
// [공개 API]
// ============================================================================

CL_T20_Mfcc::CL_T20_Mfcc()
: _impl(new ST_Impl())
{
    g_t20_instance = this;
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr) {
        return false;
    }

    T20_resetRuntimeResources(_impl);

    if (p_cfg != nullptr) {
        _impl->cfg = *p_cfg;
    } else {
        _impl->cfg = T20_makeDefaultConfig();
    }

    bool ok = false;

    do {
        if (!T20_validateConfig(&_impl->cfg)) {
            break;
        }

        _impl->mutex = xSemaphoreCreateMutex();
        if (_impl->mutex == nullptr) {
            break;
        }

        _impl->frame_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
        if (_impl->frame_queue == nullptr) {
            break;
        }

        _impl->spi.begin(
            G_T20_PIN_SPI_SCK,
            G_T20_PIN_SPI_MISO,
            G_T20_PIN_SPI_MOSI,
            G_T20_PIN_BMI_CS
        );

        pinMode(G_T20_PIN_BMI_CS, OUTPUT);
        digitalWrite(G_T20_PIN_BMI_CS, HIGH);
        pinMode(G_T20_PIN_BMI_INT1, INPUT);

        if (!T20_initDSP(_impl)) {
            break;
        }

        if (!T20_initBMI270_SPI(_impl)) {
            break;
        }

        if (!T20_configBMI270_1600Hz_DRDY(_impl)) {
            break;
        }

        if (!T20_configureFilter(_impl)) {
            break;
        }

        T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames);

        ok = true;
    } while (0);

    if (!ok) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    _impl->initialized = true;
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (!_impl->initialized || _impl->running) {
        return false;
    }

    BaseType_t r1 = xTaskCreatePinnedToCore(
        T20_sensorTask,
        "T20_Sensor",
        G_T20_SENSOR_TASK_STACK,
        _impl,
        G_T20_SENSOR_TASK_PRIO,
        &_impl->sensor_task_handle,
        0
    );

    BaseType_t r2 = xTaskCreatePinnedToCore(
        T20_processTask,
        "T20_Process",
        G_T20_PROCESS_TASK_STACK,
        _impl,
        G_T20_PROCESS_TASK_PRIO,
        &_impl->process_task_handle,
        1
    );

    if (r1 != pdPASS || r2 != pdPASS) {
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
    if (!T20_validateConfig(p_cfg)) {
        return false;
    }

    if (_impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    _impl->cfg = *p_cfg;
    bool ok = T20_configureFilter(_impl);
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames);

    xSemaphoreGive(_impl->mutex);
    return ok;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const
{
    if (p_cfg_out == nullptr || _impl->mutex == nullptr) {
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
    if (p_out == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool ok = _impl->latest_vector_valid;
    if (ok) {
        *p_out = _impl->latest_feature;
    }

    xSemaphoreGive(_impl->mutex);
    return ok;
}

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const
{
    if (p_out_vec == nullptr || p_len < G_T20_FEATURE_DIM_DEFAULT || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool ok = _impl->latest_vector_valid;
    if (ok) {
        memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * G_T20_FEATURE_DIM_DEFAULT);
    }

    xSemaphoreGive(_impl->mutex);
    return ok;
}

bool CL_T20_Mfcc::isSequenceReady(void) const
{
    if (_impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool ready = T20_seqIsReady(&_impl->seq_rb);
    xSemaphoreGive(_impl->mutex);
    return ready;
}

bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    if (p_out_seq == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    uint16_t need = _impl->cfg.output.sequence_frames * G_T20_FEATURE_DIM_DEFAULT;
    if (p_len < need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    bool ok = _impl->latest_sequence_valid;
    if (ok) {
        memcpy(p_out_seq, _impl->latest_sequence_flat, sizeof(float) * need);
    }

    xSemaphoreGive(_impl->mutex);
    return ok;
}

bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const
{
    return getLatestSequenceFlat(p_out_seq, p_len);
}

void CL_T20_Mfcc::printConfig(Stream& p_out) const
{
    if (_impl->mutex == nullptr || xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    p_out.println(F("----------- T20_Mfcc Config -----------"));
    p_out.printf("SampleRate      : %.1f\n", _impl->cfg.feature.sample_rate_hz);
    p_out.printf("FFT Size        : %u\n",   _impl->cfg.feature.fft_size);
    p_out.printf("Mel Filters     : %u\n",   _impl->cfg.feature.mel_filters);
    p_out.printf("MFCC Coeffs     : %u\n",   _impl->cfg.feature.mfcc_coeffs);
    p_out.printf("Delta Window    : %u\n",   _impl->cfg.feature.delta_window);
    p_out.printf("Output Mode     : %s\n",   _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
    p_out.printf("Seq Frames      : %u\n",   _impl->cfg.output.sequence_frames);
    p_out.printf("PreEmphasis     : %s\n",   _impl->cfg.preprocess.preemphasis.enable ? "ON" : "OFF");
    p_out.printf("Noise Gate      : %s\n",   _impl->cfg.preprocess.noise.enable_gate ? "ON" : "OFF");
    p_out.printf("Spectral Sub    : %s\n",   _impl->cfg.preprocess.noise.enable_spectral_subtract ? "ON" : "OFF");
    p_out.printf("Filter Enable   : %s\n",   _impl->cfg.preprocess.filter.enable ? "ON" : "OFF");
    p_out.println(F("----------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

void CL_T20_Mfcc::printStatus(Stream& p_out) const
{
    if (_impl->mutex == nullptr || xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    p_out.println(F("----------- T20_Mfcc Status -----------"));
    p_out.printf("Initialized     : %s\n", _impl->initialized ? "YES" : "NO");
    p_out.printf("Running         : %s\n", _impl->running ? "YES" : "NO");
    p_out.printf("Dropped Frames  : %lu\n", (unsigned long)_impl->dropped_frames);
    p_out.printf("History Count   : %u\n", _impl->mfcc_history_count);
    p_out.printf("Noise Learned   : %u\n", _impl->noise_learned_frames);
    p_out.printf("Active Buffer   : %u\n", _impl->active_fill_buffer);
    p_out.printf("Sample Index    : %u\n", _impl->active_sample_index);
    p_out.printf("Seq Frames      : %u\n", _impl->seq_rb.frames);
    p_out.printf("Seq Full        : %s\n", _impl->seq_rb.full ? "YES" : "NO");
    p_out.printf("Vector Valid    : %s\n", _impl->latest_vector_valid ? "YES" : "NO");
    p_out.printf("Sequence Valid  : %s\n", _impl->latest_sequence_valid ? "YES" : "NO");
    p_out.println(F("---------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

void CL_T20_Mfcc::printLatest(Stream& p_out) const
{
    if (_impl->mutex == nullptr || xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    if (!_impl->latest_vector_valid) {
        xSemaphoreGive(_impl->mutex);
        p_out.println(F("No latest feature available."));
        return;
    }

    ST_T20_FeatureVector_t feat = _impl->latest_feature;
    xSemaphoreGive(_impl->mutex);


    p_out.print(F("Log Mel      : "));
    for (int i = 0; i < feat.log_mel_len; ++i) p_out.printf("%.4f ", feat.log_mel[i]);
    p_out.println();
    
    p_out.print(F("MFCC      : "));
    for (int i = 0; i < feat.mfcc_len; ++i) p_out.printf("%.4f ", feat.mfcc[i]);
    p_out.println();

    p_out.print(F("Delta     : "));
    for (int i = 0; i < feat.mfcc_len; ++i) p_out.printf("%.4f ", feat.delta[i]);
    p_out.println();

    p_out.print(F("DeltaDelta: "));
    for (int i = 0; i < feat.mfcc_len; ++i) p_out.printf("%.4f ", feat.delta2[i]);
    p_out.println();
}

// ============================================================================
// [ISR / Task]
// ============================================================================

void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    if (g_t20_instance == nullptr) {
        return;
    }

    CL_T20_Mfcc::ST_Impl* p = g_t20_instance->_impl;
    BaseType_t hp_task_woken = pdFALSE;

    if (p->sensor_task_handle != nullptr) {
        vTaskNotifyGiveFromISR(p->sensor_task_handle, &hp_task_woken);
    }

    if (hp_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void T20_sensorTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);

    for (;;) {
        uint32_t notify_count = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!p->running) {
            continue;
        }

        while (notify_count-- > 0) {
            uint16_t interrupt_status = 0;
            if (p->imu.getInterruptStatus(&interrupt_status) != BMI2_OK) {
                continue;
            }

            if ((interrupt_status & BMI2_ACC_DRDY_INT_MASK) == 0) {
                continue;
            }

            if (p->imu.getSensorData() != BMI2_OK) {
                continue;
            }

            float sample = T20_selectAxisSample(p);

            uint8_t  buf = p->active_fill_buffer;
            uint16_t idx = p->active_sample_index;

            if (idx < G_T20_FFT_SIZE) {
                p->frame_buffer[buf][idx] = sample;
                idx++;
                p->active_sample_index = idx;
            }

            if (idx >= G_T20_FFT_SIZE) {
                ST_T20_FrameMessage_t msg;
                msg.frame_index = buf;

                if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
                    ST_T20_FrameMessage_t old_msg;
                    if (xQueueReceive(p->frame_queue, &old_msg, 0) == pdTRUE) {
                        p->dropped_frames++;

                        if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
                            p->dropped_frames++;
                        }
                    } else {
                        p->dropped_frames++;
                    }
                }

                p->active_fill_buffer = (uint8_t)((buf + 1U) % G_T20_RAW_FRAME_BUFFERS);
                p->active_sample_index = 0;
            }
        }
    }
}

void T20_processTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_FrameMessage_t msg;

    for (;;) {
        if (xQueueReceive(p->frame_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!p->running) {
            continue;
        }

        // 완료된 버퍼 index만 queue로 전달되므로 processing task는 완료 프레임만 읽음
        memcpy(p->work_frame, p->frame_buffer[msg.frame_index], sizeof(float) * G_T20_FFT_SIZE);

        float mfcc[G_T20_MFCC_COEFFS_MAX] = {0};
        float delta[G_T20_MFCC_COEFFS_MAX] = {0};
        float delta2[G_T20_MFCC_COEFFS_MAX] = {0};

        T20_computeMFCC(p, p->work_frame, mfcc);
        T20_pushMfccHistory(p, mfcc);
        T20_computeDeltaFromHistory(p, delta);
        T20_computeDeltaDeltaFromHistory(p, delta2);

        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(p->latest_feature.mfcc,   mfcc,   sizeof(mfcc));
            memcpy(p->latest_feature.delta,  delta,  sizeof(delta));
            memcpy(p->latest_feature.delta2, delta2, sizeof(delta2));
            T20_buildVector(mfcc, delta, delta2, p->latest_feature.vector);
            p->latest_vector_valid = true;

            T20_updateOutput(p);

            xSemaphoreGive(p->mutex);
        }
    }
}

// ============================================================================
// [Core Helpers]
// ============================================================================

bool T20_validateConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) {
        return false;
    }

    if (p_cfg->feature.fft_size != G_T20_FFT_SIZE) {
        return false;
    }

    if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS) {
        return false;
    }

    if (p_cfg->feature.mfcc_coeffs != G_T20_MFCC_COEFFS) {
        return false;
    }

    if (p_cfg->feature.sample_rate_hz <= 0.0f) {
        return false;
    }

    if (p_cfg->feature.delta_window > (G_T20_MFCC_HISTORY / 2)) {
        return false;
    }

    if (p_cfg->output.sequence_frames == 0 ||
        p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) {
        return false;
    }

    if (p_cfg->preprocess.preemphasis.enable) {
        if (p_cfg->preprocess.preemphasis.alpha < 0.0f ||
            p_cfg->preprocess.preemphasis.alpha > 1.0f) {
            return false;
        }
    }

    if (p_cfg->preprocess.noise.enable_spectral_subtract) {
        if (p_cfg->preprocess.noise.noise_learn_frames == 0) {
            return false;
        }

        if (p_cfg->preprocess.noise.spectral_subtract_strength < 0.0f) {
            return false;
        }
    }

    if (p_cfg->preprocess.filter.enable) {
        const float nyquist = p_cfg->feature.sample_rate_hz * 0.5f;

        if (p_cfg->preprocess.filter.q_factor <= 0.0f) {
            return false;
        }

        if (p_cfg->preprocess.filter.type == EN_T20_FILTER_LPF ||
            p_cfg->preprocess.filter.type == EN_T20_FILTER_HPF) {
            if (p_cfg->preprocess.filter.cutoff_hz_1 <= 0.0f ||
                p_cfg->preprocess.filter.cutoff_hz_1 >= nyquist) {
                return false;
            }
        }

        if (p_cfg->preprocess.filter.type == EN_T20_FILTER_BPF) {
            if (p_cfg->preprocess.filter.cutoff_hz_1 <= 0.0f ||
                p_cfg->preprocess.filter.cutoff_hz_2 <= 0.0f ||
                p_cfg->preprocess.filter.cutoff_hz_2 <= p_cfg->preprocess.filter.cutoff_hz_1 ||
                p_cfg->preprocess.filter.cutoff_hz_2 >= nyquist) {
                return false;
            }
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
    p->active_fill_buffer = 0;
    p->active_sample_index = 0;
    p->dropped_frames = 0;
    p->mfcc_history_count = 0;
    p->noise_learned_frames = 0;
    p->latest_vector_valid = false;
    p->latest_sequence_valid = false;
    p->prev_raw_sample = 0.0f;

    memset(p->frame_buffer, 0, sizeof(p->frame_buffer));
    memset(p->work_frame, 0, sizeof(p->work_frame));
    memset(p->temp_frame, 0, sizeof(p->temp_frame));
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
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));
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
    switch (p->cfg.preprocess.axis) {
        case EN_T20_AXIS_X: return p->imu.data.accelX;
        case EN_T20_AXIS_Y: return p->imu.data.accelY;
        case EN_T20_AXIS_Z:
        default:
            return p->imu.data.accelZ;
    }
}

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc)
{
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * G_T20_MFCC_COEFFS);
        p->mfcc_history_count++;
    } else {
        for (int i = 0; i < G_T20_MFCC_HISTORY - 1; ++i) {
            memcpy(p->mfcc_history[i], p->mfcc_history[i + 1], sizeof(float) * G_T20_MFCC_COEFFS);
        }
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1], p_mfcc, sizeof(float) * G_T20_MFCC_COEFFS);
    }
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta_out)
{
    memset(p_delta_out, 0, sizeof(float) * G_T20_MFCC_COEFFS);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int center = G_T20_MFCC_HISTORY / 2;
    const int N = p->cfg.feature.delta_window;

    float den = 0.0f;
    for (int n = 1; n <= N; ++n) {
        den += (float)(n * n);
    }
    den *= 2.0f;

    for (int c = 0; c < G_T20_MFCC_COEFFS; ++c) {
        float num = 0.0f;
        for (int n = 1; n <= N; ++n) {
            float plus  = p->mfcc_history[center + n][c];
            float minus = p->mfcc_history[center - n][c];
            num += (float)n * (plus - minus);
        }
        p_delta_out[c] = num / (den + G_T20_EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta2_out)
{
    memset(p_delta2_out, 0, sizeof(float) * G_T20_MFCC_COEFFS);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int center = G_T20_MFCC_HISTORY / 2;

    for (int c = 0; c < G_T20_MFCC_COEFFS; ++c) {
        float prev = p->mfcc_history[center - 1][c];
        float curr = p->mfcc_history[center][c];
        float next = p->mfcc_history[center + 1][c];
        p_delta2_out[c] = next - (2.0f * curr) + prev;
    }
}

void T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, float* p_out_vec)
{
    int idx = 0;

    for (int i = 0; i < G_T20_MFCC_COEFFS; ++i) p_out_vec[idx++] = p_mfcc[i];
    for (int i = 0; i < G_T20_MFCC_COEFFS; ++i) p_out_vec[idx++] = p_delta[i];
    for (int i = 0; i < G_T20_MFCC_COEFFS; ++i) p_out_vec[idx++] = p_delta2[i];
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames)
{
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->head = 0;
    p_rb->full = false;
}

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec)
{
    memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * G_T20_FEATURE_DIM);

    p_rb->head++;
    if (p_rb->head >= p_rb->frames) {
        p_rb->head = 0;
        p_rb->full = true;
    }
}

bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb)
{
    return p_rb->full;
}

void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat)
{
    uint16_t frames = p_rb->frames;
    uint16_t start = p_rb->full ? p_rb->head : 0;
    uint16_t written = 0;

    for (uint16_t i = 0; i < frames; ++i) {
        uint16_t idx = (uint16_t)((start + i) % frames);
        memcpy(&p_out_flat[written * G_T20_FEATURE_DIM],
               p_rb->data[idx],
               sizeof(float) * G_T20_FEATURE_DIM);
        written++;
    }
}

void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p)
{
    if (!p->latest_vector_valid) {
        return;
    }

    if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) {
        p->latest_sequence_valid = false;
        return;
    }

    T20_seqPush(&p->seq_rb, p->latest_feature.vector);

    if (p->cfg.output.sequence_flatten) {
        T20_seqExportFlatten(&p->seq_rb, p->latest_sequence_flat);
    }

    p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}