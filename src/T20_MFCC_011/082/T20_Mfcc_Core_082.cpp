#include "T20_Mfcc_Inter_082.h"
#include <algorithm>
#include <vector>

CL_T20_Mfcc* g_t20_instance = nullptr;

static bool T20_takeMutex(const SemaphoreHandle_t p_mutex)
{
    if (p_mutex == nullptr) return false;
    return (xSemaphoreTake(p_mutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

static void T20_giveMutex(const SemaphoreHandle_t p_mutex)
{
    if (p_mutex != nullptr) xSemaphoreGive(p_mutex);
}

void IRAM_ATTR T20_onBmiDrdyISR(void) {}

void T20_fillSyntheticFrame(CL_T20_Mfcc::ST_Impl* p, float* p_out_frame, uint16_t p_len)
{
    if (p == nullptr || p_out_frame == nullptr || p_len == 0) return;

    const float amp = G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT;
    const float f1 = 8.0f;
    const float f2 = 23.0f;
    const float sr = p->cfg.feature.sample_rate_hz;
    float phase = p->runtime_sim_phase;

    for (uint16_t i = 0; i < p_len; ++i) {
        float t = phase + ((float)i / sr);
        float s1 = sinf(2.0f * G_T20_PI * f1 * t);
        float s2 = 0.5f * sinf(2.0f * G_T20_PI * f2 * t);
        float env = 0.7f + 0.3f * sinf(2.0f * G_T20_PI * 0.25f * t);
        p_out_frame[i] = amp * env * (s1 + s2);
    }

    p->runtime_sim_phase += ((float)p_len / sr);
}

bool T20_processOneFrame(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, uint16_t p_len)
{
    if (p == nullptr || p_frame == nullptr || p_len != G_T20_FFT_SIZE) return false;

    float mfcc[G_T20_MFCC_COEFFS_MAX] = {0};
    float delta[G_T20_MFCC_COEFFS_MAX] = {0};
    float delta2[G_T20_MFCC_COEFFS_MAX] = {0};

    T20_computeMFCC(p, p_frame, mfcc);
    T20_pushMfccHistory(p, mfcc, p->cfg.feature.mfcc_coeffs);
    T20_computeDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, p->cfg.feature.delta_window, delta);
    T20_computeDeltaDeltaFromHistory(p, p->cfg.feature.mfcc_coeffs, delta2);

    if (!T20_takeMutex(p->mutex)) return false;

    memcpy(p->latest_wave_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);
    memcpy(p->viewer_last_waveform, p_frame, sizeof(float) * G_T20_FFT_SIZE);
    p->viewer_last_waveform_len = G_T20_FFT_SIZE;

    memcpy(p->viewer_last_spectrum, p->power, sizeof(float) * ((G_T20_FFT_SIZE / 2U) + 1U));
    p->viewer_last_spectrum_len = (G_T20_FFT_SIZE / 2U) + 1U;

    memcpy(p->viewer_last_log_mel, p->log_mel, sizeof(float) * G_T20_MEL_FILTERS);
    p->viewer_last_log_mel_len = G_T20_MEL_FILTERS;

    memcpy(p->viewer_last_mfcc, mfcc, sizeof(float) * p->cfg.feature.mfcc_coeffs);
    p->viewer_last_mfcc_len = p->cfg.feature.mfcc_coeffs;

    p->latest_feature.log_mel_len = G_T20_MEL_FILTERS;
    p->latest_feature.mfcc_len = p->cfg.feature.mfcc_coeffs;
    p->latest_feature.delta_len = p->cfg.feature.mfcc_coeffs;
    p->latest_feature.delta2_len = p->cfg.feature.mfcc_coeffs;
    p->latest_feature.vector_len = (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U);

    memcpy(p->latest_feature.log_mel, p->log_mel, sizeof(float) * G_T20_MEL_FILTERS);
    memcpy(p->latest_feature.mfcc, mfcc, sizeof(float) * p->cfg.feature.mfcc_coeffs);
    memcpy(p->latest_feature.delta, delta, sizeof(float) * p->cfg.feature.mfcc_coeffs);
    memcpy(p->latest_feature.delta2, delta2, sizeof(float) * p->cfg.feature.mfcc_coeffs);
    T20_buildVector(mfcc, delta, delta2, p->cfg.feature.mfcc_coeffs, p->latest_feature.vector);

    memcpy(p->viewer_last_vector, p->latest_feature.vector, sizeof(float) * p->latest_feature.vector_len);
    p->viewer_last_vector_len = p->latest_feature.vector_len;
    p->latest_vector_valid = true;

    p->viewer_last_frame_id++;
    p->last_frame_process_ms = millis();

    uint16_t wave_slot = (uint16_t)(p->viewer_recent_waveform_head % G_T20_VIEWER_RECENT_WAVE_COUNT);
    memcpy(p->viewer_recent_waveforms[wave_slot], p_frame, sizeof(float) * G_T20_FFT_SIZE);
    p->viewer_recent_frame_ids[wave_slot] = p->viewer_last_frame_id;
    if (p->viewer_recent_waveform_count < G_T20_VIEWER_RECENT_WAVE_COUNT) p->viewer_recent_waveform_count++;
    p->viewer_recent_waveform_head = (uint16_t)((p->viewer_recent_waveform_head + 1U) % G_T20_VIEWER_RECENT_WAVE_COUNT);

    if (p->viewer_event_count < G_T20_VIEWER_EVENT_MAX) {
        ST_T20_ViewerEvent_t* ev = &p->viewer_events[p->viewer_event_count++];
        ev->frame_id = p->viewer_last_frame_id;
        strlcpy(ev->kind, "frame", sizeof(ev->kind));
        snprintf(ev->text, sizeof(ev->text), "frame_%lu processed", (unsigned long)p->viewer_last_frame_id);
    }

    T20_updateOutput(p);

    if (p->recorder_enabled && p->recorder_queue != nullptr) {
        ST_T20_RecorderVectorMessage_t rec_msg;
        memset(&rec_msg, 0, sizeof(rec_msg));
        rec_msg.frame_id = p->viewer_last_frame_id;
        rec_msg.vector_len = p->latest_feature.vector_len;
        memcpy(rec_msg.vector, p->latest_feature.vector, sizeof(float) * rec_msg.vector_len);
        xQueueSend(p->recorder_queue, &rec_msg, 0);
    }

    T20_giveMutex(p->mutex);
    return true;
}

void T20_sensorTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    for (;;) {
        if (p == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!p->running) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        T20_fillSyntheticFrame(p, p->frame_buffer[p->active_fill_buffer], G_T20_FFT_SIZE);

        ST_T20_FrameMessage_t msg;
        msg.frame_index = p->active_fill_buffer;

        if (p->frame_queue != nullptr) {
            if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
                p->dropped_frames++;
            }
        }

        p->active_fill_buffer = (uint8_t)((p->active_fill_buffer + 1U) % G_T20_RAW_FRAME_BUFFERS);
        vTaskDelay(pdMS_TO_TICKS(G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS));
    }
}

void T20_processTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_FrameMessage_t msg;
    for (;;) {
        if (p == nullptr || p->frame_queue == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (xQueueReceive(p->frame_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (!p->running) continue;
        T20_processOneFrame(p, p->frame_buffer[msg.frame_index], G_T20_FFT_SIZE);
    }
}

extern void T20_recorderTask(void* p_arg);

CL_T20_Mfcc::CL_T20_Mfcc() : _impl(new ST_Impl()) { g_t20_instance = this; }
CL_T20_Mfcc::~CL_T20_Mfcc() { stop(); delete _impl; if (g_t20_instance == this) g_t20_instance = nullptr; }

bool T20_validateConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) return false;
    if (p_cfg->feature.fft_size != G_T20_FFT_SIZE) return false;
    if (p_cfg->feature.frame_size != G_T20_FFT_SIZE) return false;
    if (p_cfg->feature.hop_size == 0 || p_cfg->feature.hop_size > G_T20_FFT_SIZE) return false;
    if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS) return false;
    if (p_cfg->feature.mfcc_coeffs == 0 || p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) return false;
    if (p_cfg->output.sequence_frames == 0 || p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) return false;
    return true;
}

void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    for (uint16_t i = 0; i < G_T20_CFG_PROFILE_COUNT; ++i) {
        snprintf(p->profiles[i].name, sizeof(p->profiles[i].name), "profile_%u", (unsigned)i);
        p->profiles[i].used = false;
    }
    T20_initSdmmcProfiles(p);
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    if (p->sensor_task_handle != nullptr) { vTaskDelete(p->sensor_task_handle); p->sensor_task_handle = nullptr; }
    if (p->process_task_handle != nullptr) { vTaskDelete(p->process_task_handle); p->process_task_handle = nullptr; }
    if (p->recorder_task_handle != nullptr) { vTaskDelete(p->recorder_task_handle); p->recorder_task_handle = nullptr; }
    p->running = false;
}

void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    if (p->frame_queue != nullptr) { vQueueDelete(p->frame_queue); p->frame_queue = nullptr; }
    if (p->recorder_queue != nullptr) { vQueueDelete(p->recorder_queue); p->recorder_queue = nullptr; }
    if (p->mutex != nullptr) { vSemaphoreDelete(p->mutex); p->mutex = nullptr; }
}

void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    p->initialized = false;
    p->running = false;
    p->measurement_active = false;
    p->active_fill_buffer = 0;
    p->active_sample_index = 0;
    p->dropped_frames = 0;
    p->mfcc_history_count = 0;
    p->prev_raw_sample = 0.0f;
    p->noise_learned_frames = 0;
    p->latest_vector_valid = false;
    p->latest_sequence_valid = false;
    p->viewer_last_frame_id = 0;
    p->viewer_event_count = 0;
    p->recorder_record_count = 0;
    p->web_last_push_ms = 0;
    p->last_frame_process_ms = 0;
    p->runtime_sim_phase = 0.0f;
    memset(p->frame_buffer, 0, sizeof(p->frame_buffer));
    memset(p->work_frame, 0, sizeof(p->work_frame));
    memset(p->temp_frame, 0, sizeof(p->temp_frame));
    memset(p->window, 0, sizeof(p->window));
    memset(p->power, 0, sizeof(p->power));
    memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mel_bank, 0, sizeof(p->mel_bank));
    memset(p->mfcc_history, 0, sizeof(p->mfcc_history));
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));
    memset(&p->latest_feature, 0, sizeof(p->latest_feature));
    memset(&p->seq_rb, 0, sizeof(p->seq_rb));
    memset(p->latest_wave_frame, 0, sizeof(p->latest_wave_frame));
    memset(p->viewer_last_vector, 0, sizeof(p->viewer_last_vector));
    memset(p->viewer_last_log_mel, 0, sizeof(p->viewer_last_log_mel));
    memset(p->viewer_last_mfcc, 0, sizeof(p->viewer_last_mfcc));
    memset(p->viewer_last_waveform, 0, sizeof(p->viewer_last_waveform));
    memset(p->viewer_last_spectrum, 0, sizeof(p->viewer_last_spectrum));
    memset(p->viewer_recent_waveforms, 0, sizeof(p->viewer_recent_waveforms));
    memset(p->viewer_events, 0, sizeof(p->viewer_events));
    memset(p->recorder_index_items, 0, sizeof(p->recorder_index_items));
    memset(p->recorder_last_error, 0, sizeof(p->recorder_last_error));
}

void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    T20_stopTasks(p);
    T20_releaseSyncObjects(p);
    T20_clearRuntimeState(p);
}

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p) { (void)p; return 0.0f; }

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim)
{
    if (p == nullptr || p_mfcc == nullptr || p_dim == 0) return;
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * p_dim);
        p->mfcc_history_count++;
    } else {
        for (uint16_t i = 0; i < G_T20_MFCC_HISTORY - 1U; ++i) {
            memcpy(p->mfcc_history[i], p->mfcc_history[i + 1U], sizeof(float) * p_dim);
        }
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1U], p_mfcc, sizeof(float) * p_dim);
    }
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out)
{
    if (p == nullptr || p_delta_out == nullptr) return;
    memset(p_delta_out, 0, sizeof(float) * p_dim);
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) return;
    const uint16_t c = G_T20_MFCC_HISTORY / 2U;
    float den = 0.0f;
    for (uint16_t n = 1; n <= p_delta_window; ++n) den += (float)(n * n);
    den *= 2.0f;
    for (uint16_t i = 0; i < p_dim; ++i) {
        float num = 0.0f;
        for (uint16_t n = 1; n <= p_delta_window; ++n) {
            num += (float)n * (p->mfcc_history[c + n][i] - p->mfcc_history[c - n][i]);
        }
        p_delta_out[i] = num / (den + G_T20_EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out)
{
    if (p == nullptr || p_delta2_out == nullptr) return;
    memset(p_delta2_out, 0, sizeof(float) * p_dim);
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) return;
    const uint16_t c = G_T20_MFCC_HISTORY / 2U;
    for (uint16_t i = 0; i < p_dim; ++i) {
        p_delta2_out[i] = p->mfcc_history[c + 1U][i] - (2.0f * p->mfcc_history[c][i]) + p->mfcc_history[c - 1U][i];
    }
}

void T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec)
{
    if (p_mfcc == nullptr || p_delta == nullptr || p_delta2 == nullptr || p_out_vec == nullptr) return;
    uint16_t idx = 0;
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_mfcc[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta2[i];
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames, uint16_t p_feature_dim)
{
    if (p_rb == nullptr) return;
    memset(p_rb, 0, sizeof(*p_rb));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
}

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec)
{
    if (p_rb == nullptr || p_feature_vec == nullptr || p_rb->frames == 0 || p_rb->feature_dim == 0) return;
    memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * p_rb->feature_dim);
    p_rb->head++;
    if (p_rb->head >= p_rb->frames) { p_rb->head = 0; p_rb->full = true; }
}

bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb) { return (p_rb != nullptr) ? p_rb->full : false; }

void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat)
{
    if (p_rb == nullptr || p_out_flat == nullptr) return;
    uint16_t start = p_rb->full ? p_rb->head : 0U;
    for (uint16_t i = 0; i < p_rb->frames; ++i) {
        uint16_t idx = (uint16_t)((start + i) % p_rb->frames);
        memcpy(&p_out_flat[i * p_rb->feature_dim], p_rb->data[idx], sizeof(float) * p_rb->feature_dim);
    }
}

void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->latest_vector_valid) return;
    if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) { p->latest_sequence_valid = false; return; }
    T20_seqPush(&p->seq_rb, p->latest_feature.vector);
    p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr) return false;
    T20_resetRuntimeResources(_impl);
    _impl->cfg = (p_cfg != nullptr) ? *p_cfg : T20_makeDefaultConfig();
    if (!T20_validateConfig(&_impl->cfg)) return false;
    _impl->mutex = xSemaphoreCreateMutex();
    if (_impl->mutex == nullptr) return false;
    _impl->frame_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    _impl->recorder_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_RecorderVectorMessage_t));
    if (_impl->frame_queue == nullptr || _impl->recorder_queue == nullptr) { T20_resetRuntimeResources(_impl); return false; }
    _impl->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);
    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);
    pinMode(G_T20_PIN_BMI_INT1, INPUT);
    T20_initProfiles(_impl);
    T20_loadRuntimeConfigFile(_impl);
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
    T20_initDSP(_impl);
    _impl->initialized = true;
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (_impl == nullptr || !_impl->initialized || _impl->running) return false;
    BaseType_t r1 = xTaskCreatePinnedToCore(T20_sensorTask, "T20_Sensor", G_T20_SENSOR_TASK_STACK, _impl, G_T20_SENSOR_TASK_PRIO, &_impl->sensor_task_handle, 0);
    BaseType_t r2 = xTaskCreatePinnedToCore(T20_processTask, "T20_Process", G_T20_PROCESS_TASK_STACK, _impl, G_T20_PROCESS_TASK_PRIO, &_impl->process_task_handle, 1);
    BaseType_t r3 = xTaskCreatePinnedToCore(T20_recorderTask, "T20_Recorder", G_T20_RECORDER_TASK_STACK, _impl, G_T20_RECORDER_TASK_PRIO, &_impl->recorder_task_handle, 1);
    if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
        T20_stopTasks(_impl);
        return false;
    }
    _impl->running = true;
    return true;
}

void CL_T20_Mfcc::stop(void)
{
    if (_impl != nullptr) T20_stopTasks(_impl);
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr || p_cfg == nullptr || !T20_validateConfig(p_cfg)) return false;
    if (!T20_takeMutex(_impl->mutex)) return false;
    _impl->cfg = *p_cfg;
    _impl->viewer_effective_hop_size = p_cfg->feature.hop_size;
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
    T20_configureRuntimeFilter(_impl);
    T20_saveRuntimeConfigFile(_impl);
    T20_giveMutex(_impl->mutex);
    return true;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const { if (_impl != nullptr && p_cfg_out != nullptr) *p_cfg_out = _impl->cfg; }

bool CL_T20_Mfcc::getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const
{
    if (_impl == nullptr || p_out == nullptr) return false;
    if (!T20_takeMutex(_impl->mutex)) return false;
    bool ok = _impl->latest_vector_valid;
    if (ok) *p_out = _impl->latest_feature;
    T20_giveMutex(_impl->mutex);
    return ok;
}

bool CL_T20_Mfcc::getLatestVector(float* p_out_vec, uint16_t p_len) const
{
    if (_impl == nullptr || p_out_vec == nullptr) return false;
    if (!T20_takeMutex(_impl->mutex)) return false;
    bool ok = (_impl->latest_vector_valid && _impl->latest_feature.vector_len <= p_len);
    if (ok) memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * _impl->latest_feature.vector_len);
    T20_giveMutex(_impl->mutex);
    return ok;
}

bool CL_T20_Mfcc::isSequenceReady(void) const { return (_impl != nullptr) ? T20_seqIsReady(&_impl->seq_rb) : false; }
bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    if (_impl == nullptr || p_out_seq == nullptr) return false;
    uint16_t need = (uint16_t)(_impl->seq_rb.frames * _impl->seq_rb.feature_dim);
    if (need == 0 || p_len < need || !_impl->latest_sequence_valid) return false;
    T20_seqExportFlatten(&_impl->seq_rb, p_out_seq);
    return true;
}
bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const { return getLatestSequenceFlat(p_out_seq, p_len); }

// keep the rest from 057 by including exported/JSON wrappers and helpers from existing file


bool T20_buildRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["version"] = G_T20_VERSION_STR;
    doc["profile_name"] = p->runtime_cfg_profile_name;
    doc["frame_size"] = p->cfg.feature.frame_size;
    doc["hop_size"] = p->cfg.feature.hop_size;
    doc["sample_rate_hz"] = p->cfg.feature.sample_rate_hz;
    doc["mfcc_coeffs"] = p->cfg.feature.mfcc_coeffs;
    doc["sequence_frames"] = p->cfg.output.sequence_frames;
    doc["output_mode"] = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";
    doc["recorder_backend"] = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
    doc["recorder_enabled"] = p->recorder_enabled;
    doc["sdmmc_profile"] = p->sdmmc_profile.profile_name;
    doc["sdmmc_profile_applied"] = p->sdmmc_profile_applied;
    doc["sdmmc_last_apply_reason"] = p->sdmmc_last_apply_reason;
    doc["selection_sync_enabled"] = p->selection_sync_enabled;
    doc["selection_sync_frame_from"] = p->selection_sync_frame_from;
    doc["selection_sync_frame_to"] = p->selection_sync_frame_to;
    doc["selection_sync_name"] = p->selection_sync_name;
    doc["selection_sync_range_valid"] = p->selection_sync_range_valid;
    doc["selection_sync_effective_from"] = p->selection_sync_effective_from;
    doc["selection_sync_effective_to"] = p->selection_sync_effective_to;
    doc["type_meta_enabled"] = p->type_meta_enabled;
    doc["type_meta_name"] = p->type_meta_name;
    doc["type_meta_kind"] = p->type_meta_kind;
    doc["type_meta_auto_text"] = p->type_meta_auto_text;
    doc["batch_flush_records"] = G_T20_RECORDER_BATCH_FLUSH_RECORDS;
    doc["batch_flush_timeout_ms"] = G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS;
    doc["batch_watermark_low"] = p->recorder_batch_watermark_low;
    doc["batch_watermark_high"] = p->recorder_batch_watermark_high;
    doc["batch_idle_flush_ms"] = p->recorder_batch_idle_flush_ms;
    doc["dma_slot_count"] = G_T20_ZERO_COPY_DMA_SLOT_COUNT;
    doc["dma_slot_bytes"] = G_T20_ZERO_COPY_DMA_SLOT_BYTES;

    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    serializeJson(doc, p_out_buf, p_len);
    return true;
}

bool T20_applyRuntimeConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text)
{
    if (p == nullptr || p_json_text == nullptr) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, p_json_text);
    if (err) return false;

    const char* profile_name = doc["profile_name"] | p->runtime_cfg_profile_name;
    const char* backend = doc["recorder_backend"] | ((p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc");
    const char* sdmmc_profile = doc["sdmmc_profile"] | p->sdmmc_profile.profile_name;
    const char* output_mode = doc["output_mode"] | ((p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence");
    const char* selection_sync_name = doc["selection_sync_name"] | p->selection_sync_name;
    const char* type_meta_name = doc["type_meta_name"] | p->type_meta_name;
    const char* type_meta_kind = doc["type_meta_kind"] | p->type_meta_kind;

    strlcpy(p->runtime_cfg_profile_name, profile_name, sizeof(p->runtime_cfg_profile_name));
    strlcpy(p->selection_sync_name, selection_sync_name, sizeof(p->selection_sync_name));
    strlcpy(p->type_meta_name, type_meta_name, sizeof(p->type_meta_name));
    strlcpy(p->type_meta_kind, type_meta_kind, sizeof(p->type_meta_kind));
    p->type_meta_enabled = (bool)(doc["type_meta_enabled"] | p->type_meta_enabled);
    p->cfg.feature.hop_size = (uint16_t)(doc["hop_size"] | p->cfg.feature.hop_size);
    p->cfg.feature.mfcc_coeffs = (uint16_t)(doc["mfcc_coeffs"] | p->cfg.feature.mfcc_coeffs);
    p->cfg.output.sequence_frames = (uint16_t)(doc["sequence_frames"] | p->cfg.output.sequence_frames);
    p->recorder_enabled = (bool)(doc["recorder_enabled"] | p->recorder_enabled);
    p->selection_sync_enabled = (bool)(doc["selection_sync_enabled"] | p->selection_sync_enabled);
    p->selection_sync_frame_from = (uint32_t)(doc["selection_sync_frame_from"] | p->selection_sync_frame_from);
    p->selection_sync_frame_to = (uint32_t)(doc["selection_sync_frame_to"] | p->selection_sync_frame_to);
    p->recorder_batch_watermark_low = (uint16_t)(doc["batch_watermark_low"] | p->recorder_batch_watermark_low);
    p->recorder_batch_watermark_high = (uint16_t)(doc["batch_watermark_high"] | p->recorder_batch_watermark_high);
    p->recorder_batch_idle_flush_ms = (uint32_t)(doc["batch_idle_flush_ms"] | p->recorder_batch_idle_flush_ms);
    if (p->recorder_batch_watermark_low == 0) p->recorder_batch_watermark_low = 1;
    if (p->recorder_batch_watermark_high < p->recorder_batch_watermark_low) {
        p->recorder_batch_watermark_high = p->recorder_batch_watermark_low;
    }

    if (strcmp(backend, "sdmmc") == 0) p->recorder_storage_backend = EN_T20_STORAGE_SDMMC;
    else p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;

    if (strcmp(output_mode, "sequence") == 0) p->cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;
    else p->cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;

    T20_applySdmmcProfileByName(p, sdmmc_profile);
    T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U));
    T20_configureRuntimeFilter(p);
    T20_syncDerivedViewState(p);
    return true;
}


bool T20_loadRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (!LittleFS.exists(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH)) return false;

    File file = LittleFS.open(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH, "r");
    if (!file) return false;

    String json_text = file.readString();
    file.close();

    return T20_applyRuntimeConfigJsonText(p, json_text.c_str());
}

bool T20_saveRuntimeConfigFile(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;

    char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
    if (!T20_buildRuntimeConfigJsonText(p, json, sizeof(json))) return false;

    File file = LittleFS.open(G_T20_RECORDER_RUNTIME_CFG_FILE_PATH, "w");
    if (!file) return false;
    file.print(json);
    file.close();
    return true;
}



bool T20_buildSdmmcProfilesJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["active"] = p->sdmmc_profile.profile_name;
    JsonArray arr = doc["profiles"].to<JsonArray>();

    for (uint16_t i = 0; i < G_T20_SDMMC_PROFILE_PRESET_COUNT; ++i) {
        if (!p->sdmmc_profiles[i].enabled) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"] = p->sdmmc_profiles[i].profile_name;
        o["use_1bit_mode"] = p->sdmmc_profiles[i].use_1bit_mode;
        o["clk_pin"] = p->sdmmc_profiles[i].clk_pin;
        o["cmd_pin"] = p->sdmmc_profiles[i].cmd_pin;
        o["d0_pin"] = p->sdmmc_profiles[i].d0_pin;
        o["d1_pin"] = p->sdmmc_profiles[i].d1_pin;
        o["d2_pin"] = p->sdmmc_profiles[i].d2_pin;
        o["d3_pin"] = p->sdmmc_profiles[i].d3_pin;
    }

    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    serializeJson(doc, p_out_buf, p_len);
    return true;
}


bool T20_buildSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
    JsonDocument doc;
    doc["enabled"] = p->selection_sync_enabled;
    doc["name"] = p->selection_sync_name;
    doc["frame_from"] = p->selection_sync_frame_from;
    doc["frame_to"] = p->selection_sync_frame_to;
    doc["effective_from"] = p->selection_sync_effective_from;
    doc["effective_to"] = p->selection_sync_effective_to;
    doc["range_valid"] = p->selection_sync_range_valid;
    doc["selected_points_len"] = p->viewer_selection_points_len;
    if (p->selection_sync_enabled && !p->selection_sync_range_valid) {
        doc["status"] = "range_invalid";
    } else if (p->selection_sync_enabled) {
        doc["status"] = "range_ready";
    } else {
        doc["status"] = "disabled";
    }
    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    serializeJson(doc, p_out_buf, p_len);
    return true;
}

bool T20_buildTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;
    JsonDocument doc;
    doc["enabled"] = p->type_meta_enabled;
    doc["name"] = p->type_meta_name;
    doc["kind"] = p->type_meta_kind;
    doc["vector_len"] = p->viewer_last_vector_len;
    doc["latest_frame_id"] = p->viewer_last_frame_id;
    doc["auto_text"] = p->type_meta_auto_text;
    doc["preview_link_path"] = p->type_preview_link_path;
    doc["preview_parser_name"] = p->type_preview_parser_name;
    doc["preview_sample_row_count"] = p->type_preview_sample_row_count;
    doc["preview_text_loaded"] = (p->type_preview_text_buf[0] != 0);
    doc["schema_kind"] = p->type_preview_schema_kind;
    doc["detected_delim"] = p->type_preview_detected_delim;
    doc["header_guess"] = p->type_preview_header_guess;
    doc["build_sync_state"] = "synced";
    JsonArray rows = doc["preview_sample_rows"].to<JsonArray>();
    for (uint16_t i = 0; i < p->type_preview_sample_row_count; ++i) { rows.add(p->type_preview_sample_rows[i]); }
    size_t need = measureJson(doc) + 1U;
    if (need > p_len) return false;
    serializeJson(doc, p_out_buf, p_len);
    return true;
}


void T20_updateSelectionSyncState(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    if (!p->selection_sync_enabled) {
        p->selection_sync_range_valid = false;
        p->selection_sync_effective_from = 0;
        p->selection_sync_effective_to = 0;
        return;
    }

    if (p->selection_sync_frame_to < p->selection_sync_frame_from) {
        p->selection_sync_range_valid = false;
        p->selection_sync_effective_from = p->selection_sync_frame_from;
        p->selection_sync_effective_to = p->selection_sync_frame_to;
        return;
    }

    p->selection_sync_range_valid = true;
    p->selection_sync_effective_from = p->selection_sync_frame_from;
    p->selection_sync_effective_to = p->selection_sync_frame_to;
}

void T20_updateTypeMetaAutoClassify(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    if (!p->type_meta_enabled) {
        strlcpy(p->type_meta_auto_text, "disabled", sizeof(p->type_meta_auto_text));
        return;
    }

    if (strcmp(p->type_meta_kind, "feature_vector") == 0) {
        if (p->viewer_last_vector_len >= 39U) {
            strlcpy(p->type_meta_auto_text, "mfcc_39d_vector", sizeof(p->type_meta_auto_text));
        } else if (p->viewer_last_vector_len > 0U) {
            strlcpy(p->type_meta_auto_text, "compact_feature_vector", sizeof(p->type_meta_auto_text));
        } else {
            strlcpy(p->type_meta_auto_text, "empty_feature_vector", sizeof(p->type_meta_auto_text));
        }
    } else if (strcmp(p->type_meta_kind, "sequence") == 0) {
        if (p->latest_sequence_valid) {
            strlcpy(p->type_meta_auto_text, "sequence_ready", sizeof(p->type_meta_auto_text));
        } else {
            strlcpy(p->type_meta_auto_text, "sequence_warming_up", sizeof(p->type_meta_auto_text));
        }
    } else if (strcmp(p->type_meta_kind, "waveform") == 0) {
        strlcpy(p->type_meta_auto_text, "waveform_frame", sizeof(p->type_meta_auto_text));
    } else {
        strlcpy(p->type_meta_auto_text, "generic_meta", sizeof(p->type_meta_auto_text));
    }
}


void T20_updateViewerSelectionProjection(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    memset(p->viewer_selection_points, 0, sizeof(p->viewer_selection_points));
    p->viewer_selection_points_len = 0;

    if (!p->selection_sync_enabled || !p->selection_sync_range_valid) return;
    if (p->viewer_last_waveform_len == 0) return;

    uint32_t frame_from = p->selection_sync_effective_from;
    uint32_t frame_to = p->selection_sync_effective_to;

    if (p->viewer_last_frame_id < frame_from || p->viewer_last_frame_id > frame_to) {
        return;
    }

    uint16_t copy_len = p->viewer_last_waveform_len;
    if (copy_len > G_T20_VIEWER_SELECTION_POINTS_MAX) {
        copy_len = G_T20_VIEWER_SELECTION_POINTS_MAX;
    }

    memcpy(p->viewer_selection_points, p->viewer_last_waveform, sizeof(float) * copy_len);
    p->viewer_selection_points_len = copy_len;
}




bool T20_loadTypePreviewText(CL_T20_Mfcc::ST_Impl* p, const char* p_path)
{
    if (p == nullptr || p_path == nullptr || p_path[0] == 0) return false;

    File file = LittleFS.open(p_path, "r");
    if (!file) return false;

    size_t n = file.readBytes(p->type_preview_text_buf, G_T20_TYPE_PREVIEW_TEXT_BUF_MAX - 1U);
    p->type_preview_text_buf[n] = 0;
    file.close();
    return true;
}

void T20_updateTypePreviewSamples(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    memset(p->type_preview_sample_rows, 0, sizeof(p->type_preview_sample_rows));
    p->type_preview_sample_row_count = 0;

    if (!p->type_meta_enabled) return;

    if (p->viewer_last_vector_len > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "vector_len=%u", p->viewer_last_vector_len);
    }

    if (p->viewer_last_waveform_len > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "waveform_len=%u", p->viewer_last_waveform_len);
    }

    if (p->selection_sync_enabled && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "selection=%s:%lu-%lu", p->selection_sync_name,
                 (unsigned long)p->selection_sync_effective_from,
                 (unsigned long)p->selection_sync_effective_to);
    }

    if (p->viewer_overlay_accum_count > 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "overlay_accum=%u", p->viewer_overlay_accum_count);
    }

    if (p->type_preview_text_buf[0] != 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "preview_text=loaded");
    }

    if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "schema=%s delim=%s", p->type_preview_schema_kind, p->type_preview_detected_delim);
    }

    if (p->type_preview_header_guess[0] != 0 && p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "header=%s", p->type_preview_header_guess);
    }

    if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX) {
        snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                 sizeof(p->type_preview_sample_rows[0]),
                 "auto=%s", p->type_meta_auto_text);
    }
}


void T20_updateTypePreviewSchemaGuess(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    strlcpy(p->type_preview_schema_kind, "unknown", sizeof(p->type_preview_schema_kind));
    strlcpy(p->type_preview_detected_delim, ",", sizeof(p->type_preview_detected_delim));

    if (p->type_preview_text_buf[0] == 0) {
        return;
    }

    const char* txt = p->type_preview_text_buf;
    uint32_t comma = 0, tab = 0, pipe = 0;
    for (uint16_t i = 0; txt[i] != 0 && i < G_T20_TYPE_PREVIEW_TEXT_BUF_MAX; ++i) {
        if (txt[i] == ',') comma++;
        else if (txt[i] == '\t') tab++;
        else if (txt[i] == '|') pipe++;
    }

    if (tab >= comma && tab >= pipe && tab > 0) {
        strlcpy(p->type_preview_detected_delim, "\\t", sizeof(p->type_preview_detected_delim));
        strlcpy(p->type_preview_schema_kind, "tsv_like", sizeof(p->type_preview_schema_kind));
    } else if (pipe >= comma && pipe >= tab && pipe > 0) {
        strlcpy(p->type_preview_detected_delim, "|", sizeof(p->type_preview_detected_delim));
        strlcpy(p->type_preview_schema_kind, "pipe_like", sizeof(p->type_preview_schema_kind));
    } else if (comma > 0) {
        strlcpy(p->type_preview_detected_delim, ",", sizeof(p->type_preview_detected_delim));
        strlcpy(p->type_preview_schema_kind, "csv_like", sizeof(p->type_preview_schema_kind));
    } else {
        strlcpy(p->type_preview_schema_kind, "plain_text", sizeof(p->type_preview_schema_kind));
    }

    T20_updateTypePreviewHeaderGuess(p);

    if (p->type_preview_header_guess[0] != 0) {
        if (strstr(p->type_preview_header_guess, "time") != nullptr ||
            strstr(p->type_preview_header_guess, "frame") != nullptr ||
            strstr(p->type_preview_header_guess, "value") != nullptr) {
            strlcpy(p->type_preview_schema_kind, "headered_table", sizeof(p->type_preview_schema_kind));
        }
    }
}


void T20_updateTypePreviewHeaderGuess(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    memset(p->type_preview_header_guess, 0, sizeof(p->type_preview_header_guess));

    if (p->type_preview_text_buf[0] == 0) {
        return;
    }

    uint16_t i = 0;
    while (p->type_preview_text_buf[i] != 0 &&
           p->type_preview_text_buf[i] != '\n' &&
           p->type_preview_text_buf[i] != '\r' &&
           i < (sizeof(p->type_preview_header_guess) - 1U)) {
        p->type_preview_header_guess[i] = p->type_preview_text_buf[i];
        ++i;
    }
    p->type_preview_header_guess[i] = 0;
}


void T20_updateViewerOverlayProjection(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    memset(p->viewer_overlay_points, 0, sizeof(p->viewer_overlay_points));
    p->viewer_overlay_points_len = 0;
    p->viewer_overlay_accum_count = 0;
    p->viewer_overlay_subset_count = 0;

    if (p->viewer_recent_waveform_count == 0) return;

    uint16_t copy_len = G_T20_VIEWER_SELECTION_POINTS_MAX;
    if (copy_len > G_T20_FFT_SIZE) copy_len = G_T20_FFT_SIZE;

    for (uint16_t n = 0; n < p->viewer_recent_waveform_count; ++n) {
        uint16_t idx = (uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT);
        uint32_t fid = p->viewer_recent_frame_ids[idx];

        bool include = true;
        if (p->selection_sync_enabled) {
            include = p->selection_sync_range_valid &&
                      (fid >= p->selection_sync_effective_from) &&
                      (fid <= p->selection_sync_effective_to);
        }
        if (!include) continue;

        for (uint16_t i = 0; i < copy_len; ++i) {
            p->viewer_overlay_points[i] += p->viewer_recent_waveforms[idx][i];
        }
        p->viewer_overlay_accum_count++;
        p->viewer_overlay_subset_count++;
    }

    if (p->viewer_overlay_accum_count == 0) return;

    if (p->viewer_overlay_accum_count > 1) {
        for (uint16_t i = 0; i < copy_len; ++i) {
            p->viewer_overlay_points[i] /= (float)p->viewer_overlay_accum_count;
        }
    }
    p->viewer_overlay_points_len = copy_len;
}


void T20_updateTypePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    /* TODO:
       - 실제 column 별 타입 추론 구조체 도입
       - 현재는 header_guess / schema_kind 조합을 sample row에 반영하는 수준 */

    if (p->type_preview_sample_row_count < G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX &&
        p->type_preview_header_guess[0] != 0) {
        if (strstr(p->type_preview_header_guess, "time") != nullptr) {
            snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                     sizeof(p->type_preview_sample_rows[0]),
                     "col_hint=time-like");
        } else if (strstr(p->type_preview_header_guess, "frame") != nullptr) {
            snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                     sizeof(p->type_preview_sample_rows[0]),
                     "col_hint=frame-like");
        } else if (strstr(p->type_preview_header_guess, "value") != nullptr) {
            snprintf(p->type_preview_sample_rows[p->type_preview_sample_row_count++],
                     sizeof(p->type_preview_sample_rows[0]),
                     "col_hint=value-like");
        }
    }
}


void T20_syncDerivedViewState(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    T20_syncDerivedViewState(p);
}

bool T20_buildBuildSanityJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["version"] = G_T20_VERSION_STR;
    doc["has_recent_frame_ids"] = true;
    doc["has_selection_points"] = true;
    doc["has_overlay_points"] = true;
    doc["has_type_preview_header_guess"] = true;
    doc["has_type_preview_schema_kind"] = true;
    doc["has_json_write_doc"] = true;
    doc["has_live_source_path"] = true;
    doc["selection_sync_enabled"] = p->selection_sync_enabled;
    doc["selection_sync_range_valid"] = p->selection_sync_range_valid;
    doc["viewer_overlay_accum_count"] = p->viewer_overlay_accum_count;
    doc["viewer_overlay_subset_count"] = p->viewer_overlay_subset_count;
    doc["type_preview_text_loaded"] = (p->type_preview_text_buf[0] != 0);
    doc["type_preview_schema_kind"] = p->type_preview_schema_kind;
    doc["type_preview_header_guess"] = p->type_preview_header_guess;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}



void T20_updatePreviewColumnHints(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    p->preview_column_hint_count = 0;
    memset(p->preview_column_hints, 0, sizeof(p->preview_column_hints));

    if (p->type_preview_header_guess[0] == 0) return;

    if (strstr(p->type_preview_header_guess, "time") != nullptr && p->preview_column_hint_count < 8) {
        strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "time-like", sizeof(p->preview_column_hints[0]));
    }
    if (strstr(p->type_preview_header_guess, "frame") != nullptr && p->preview_column_hint_count < 8) {
        strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "frame-like", sizeof(p->preview_column_hints[0]));
    }
    if ((strstr(p->type_preview_header_guess, "value") != nullptr ||
         strstr(p->type_preview_header_guess, "val") != nullptr) && p->preview_column_hint_count < 8) {
        strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "value-like", sizeof(p->preview_column_hints[0]));
    }
    if ((strstr(p->type_preview_header_guess, "x") != nullptr ||
         strstr(p->type_preview_header_guess, "y") != nullptr) && p->preview_column_hint_count < 8) {
        strlcpy(p->preview_column_hints[p->preview_column_hint_count++], "axis-like", sizeof(p->preview_column_hints[0]));
    }
}

bool T20_buildUnifiedViewerBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["mode"] = p->viewer_bundle_mode_name;
    doc["frame_id"] = p->viewer_last_frame_id;

    JsonObject selection = doc["selection"].to<JsonObject>();
    selection["enabled"] = p->selection_sync_enabled;
    selection["range_valid"] = p->selection_sync_range_valid;
    selection["name"] = p->selection_sync_name;
    selection["from"] = p->selection_sync_effective_from;
    selection["to"] = p->selection_sync_effective_to;
    selection["points_len"] = p->viewer_selection_points_len;

    JsonArray wave = doc["waveform"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_waveform_len; ++i) wave.add(p->viewer_last_waveform[i]);

    JsonArray sel = doc["selection_points"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_selection_points_len; ++i) sel.add(p->viewer_selection_points[i]);

    JsonArray ov = doc["overlay_points"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_overlay_points_len; ++i) ov.add(p->viewer_overlay_points[i]);
    doc["overlay_accum_count"] = p->viewer_overlay_accum_count;
    doc["overlay_subset_count"] = p->viewer_overlay_subset_count;

    JsonArray recent = doc["recent_frame_ids"].to<JsonArray>();
    for (uint16_t n = 0; n < p->viewer_recent_waveform_count; ++n) {
        uint16_t idx = (uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT);
        recent.add(p->viewer_recent_frame_ids[idx]);
    }

    JsonObject type = doc["type_meta"].to<JsonObject>();
    type["enabled"] = p->type_meta_enabled;
    type["name"] = p->type_meta_name;
    type["kind"] = p->type_meta_kind;
    type["auto_text"] = p->type_meta_auto_text;
    type["preview_link_path"] = p->type_preview_link_path;
    type["preview_parser_name"] = p->type_preview_parser_name;
    type["preview_text_loaded"] = (p->type_preview_text_buf[0] != 0);
    type["schema_kind"] = p->type_preview_schema_kind;
    type["detected_delim"] = p->type_preview_detected_delim;
    type["header_guess"] = p->type_preview_header_guess;

    JsonArray hints = type["column_hints"].to<JsonArray>();
    for (uint16_t i = 0; i < p->preview_column_hint_count; ++i) hints.add(p->preview_column_hints[i]);

    JsonArray rows = type["preview_sample_rows"].to<JsonArray>();
    for (uint16_t i = 0; i < p->type_preview_sample_row_count; ++i) rows.add(p->type_preview_sample_rows[i]);

    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}


bool T20_buildRecorderStorageJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["backend"] = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
    doc["fallback_active"] = p->recorder_fallback_active;
    doc["active_path"] = p->recorder_active_path;
    doc["record_count"] = p->recorder_record_count;
    doc["batch_count"] = p->recorder_batch_count;
    doc["index_count"] = p->recorder_index_count;
    doc["last_flush_ms"] = p->recorder_last_flush_ms;
    doc["rotate_keep_max"] = p->recorder_rotate_keep_max;
    doc["last_error"] = p->recorder_last_error;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}



bool T20_jsonWriteDoc(JsonDocument& p_doc, char* p_out_buf, uint16_t p_len)
{
    if (p_out_buf == nullptr || p_len == 0) return false;
    size_t need = measureJson(p_doc) + 1U;
    if (need > p_len) {
        if (p_len > 0) p_out_buf[0] = 0;
        return false;
    }
    serializeJson(p_doc, p_out_buf, p_len);
    return true;
}

bool T20_beginLiveSource(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;

    /* TODO:
       - BMI270 실제 begin / DRDY 인터럽트 / 샘플레이트 설정
       - queue / ring buffer / frame assemble 연동
       - 현재 단계는 live path 구조 연결용 skeleton */
    p->bmi270_live_enabled = true;
    p->bmi270_live_ready = true;
    p->live_source_mode = 1;
    p->live_last_sample_ms = millis();
    return true;
}

void T20_stopLiveSource(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    p->bmi270_live_enabled = false;
    p->bmi270_live_ready = false;
    p->live_source_mode = 0;
}

bool T20_processLiveSourceTick(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (!p->bmi270_live_enabled || !p->bmi270_live_ready) return false;

    uint32_t now_ms = millis();
    if ((now_ms - p->live_last_sample_ms) < G_T20_BMI270_SIM_SAMPLE_INTERVAL_MS) return false;

    /* TODO:
       - 실제 BMI270 begin/init
       - 실제 gyro/acc raw sample read
       - DRDY interrupt 기반 큐 수집
       - axis 선택/전처리/정규화 반영
       - 현재 단계는 실센서 연결 전 라이브 경로 skeleton 검증용 */
    p->live_last_sample_ms = now_ms;

    float sample = (float)((p->live_frame_counter % 100U) - 50U) / 50.0f;
    if (!T20_feedLiveSample(p, sample)) return false;

    if (T20_tryBuildLiveFrame(p)) {
        p->live_frame_counter++;
        return true;
    }
    return false;
}

bool T20_buildLiveSourceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["live_source_mode"] = p->live_source_mode;
    doc["bmi270_live_enabled"] = p->bmi270_live_enabled;
    doc["bmi270_live_ready"] = p->bmi270_live_ready;
    doc["live_frame_counter"] = p->live_frame_counter;
    doc["live_last_sample_ms"] = p->live_last_sample_ms;
    doc["hop_size"] = p->cfg.feature.hop_size;
    doc["frame_size"] = p->cfg.feature.frame_size;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}



bool T20_feedLiveSample(CL_T20_Mfcc::ST_Impl* p, float p_value)
{
    if (p == nullptr) return false;
    if (p->live_frame_fill >= G_T20_LIVE_FRAME_TEMP_MAX) return false;

    p->live_frame_temp[p->live_frame_fill++] = p_value;
    p->bmi270_sample_counter++;
    return true;
}

bool T20_tryBuildLiveFrame(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (!p->bmi270_live_enabled || !p->bmi270_live_ready) return false;

    uint16_t frame_size = p->cfg.feature.frame_size;
    if (frame_size == 0 || frame_size > G_T20_LIVE_FRAME_TEMP_MAX) return false;
    if (p->live_frame_fill < frame_size) return false;

    memcpy(p->latest_wave_frame, p->live_frame_temp, sizeof(float) * frame_size);

    uint16_t hop = p->cfg.feature.hop_size;
    if (hop == 0 || hop > frame_size) hop = frame_size;

    uint16_t remain = (p->live_frame_fill > hop) ? (p->live_frame_fill - hop) : 0;
    if (remain > 0) {
        memmove(p->live_frame_temp, &p->live_frame_temp[hop], sizeof(float) * remain);
    }
    p->live_frame_fill = remain;
    p->live_frame_ready = true;
    return true;
}

bool T20_buildLiveDebugJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["live_source_mode"] = p->live_source_mode;
    doc["bmi270_live_enabled"] = p->bmi270_live_enabled;
    doc["bmi270_live_ready"] = p->bmi270_live_ready;
    doc["live_frame_fill"] = p->live_frame_fill;
    doc["live_frame_ready"] = p->live_frame_ready;
    doc["bmi270_sample_counter"] = p->bmi270_sample_counter;
    doc["frame_size"] = p->cfg.feature.frame_size;
    doc["hop_size"] = p->cfg.feature.hop_size;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}



bool T20_buildRecorderSessionJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) return false;

    JsonDocument doc;
    doc["session_open"] = p->recorder_session_open;
    doc["session_id"] = p->recorder_session_id;
    doc["session_name"] = p->recorder_session_name;
    doc["session_open_ms"] = p->recorder_session_open_ms;
    doc["session_close_ms"] = p->recorder_session_close_ms;
    doc["record_count"] = p->recorder_record_count;
    doc["active_path"] = p->recorder_active_path;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}


bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out) { if (p_cfg_out == nullptr) return false; *p_cfg_out = T20_makeDefaultConfig(); return true; }
bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out) { (void)p_profile_index; return T20_loadRuntimeConfigFromLittleFs(p_cfg_out); }
bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p) { if (p == nullptr) return false; p->recorder_index_count = 0; return true; }
bool T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg) { (void)p_profile_index; (void)p_cfg; return true; }
bool T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg) { (void)p_cfg; return true; }
bool T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out) { (void)p_json_text; if (p_cfg_out == nullptr) return false; *p_cfg_out = T20_makeDefaultConfig(); return true; }
bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json) { return T20_applyConfigJsonText(p, p_json); }
bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text) { if (p == nullptr || p_json_text == nullptr) return false; ST_T20_Config_t cfg = p->cfg; if (!T20_parseConfigJsonText(p_json_text, &cfg)) return false; p->cfg = cfg; return true; }

static bool T20_jsonWriteDoc(const JsonDocument& p_doc, char* p_out_buf, uint16_t p_len)
{
    if (p_out_buf == nullptr || p_len == 0) return false;
    size_t need = measureJson(p_doc) + 1U;
    if (need > p_len) return false;
    serializeJson(p_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p == nullptr) return false; JsonDocument doc; doc["frame_size"]=p->cfg.feature.frame_size; doc["hop_size"]=p->cfg.feature.hop_size; doc["output_mode"]=(p->cfg.output.output_mode==EN_T20_OUTPUT_VECTOR)?"vector":"sequence"; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len) { JsonDocument doc; doc["mode"]="config_schema"; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_waveform"; doc["len"]=p->viewer_last_waveform_len; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_spectrum"; doc["len"]=p->viewer_last_spectrum_len; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_data"; doc["frame_id"]=p->viewer_last_frame_id; doc["vector_len"]=p->viewer_last_vector_len; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }

bool T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc; doc["mode"]="viewer_events";
    JsonArray arr = doc["events"].to<JsonArray>();
    for (uint16_t i=0;i<p->viewer_event_count && i<G_T20_VIEWER_EVENT_MAX;++i) {
        JsonObject o = arr.add<JsonObject>();
        o["frame_id"] = p->viewer_events[i].frame_id;
        o["kind"] = p->viewer_events[i].kind;
        o["text"] = p->viewer_events[i].text;
    }
    return T20_jsonWriteDoc(doc,p_out_buf,p_len);
}
bool T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_sequence"; doc["ready"]=p->latest_sequence_valid; doc["frames"]=p->seq_rb.frames; doc["feature_dim"]=p->seq_rb.feature_dim; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { return T20_buildViewerDataJsonText(p,p_out_buf,p_len); }
bool T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_multi_frame"; doc["count"]=p->viewer_recent_waveform_count; doc["selection_sync_enabled"]=p->selection_sync_enabled; doc["selection_sync_range_valid"]=p->selection_sync_range_valid; doc["selection_points_len"]=p->viewer_selection_points_len; JsonArray arr=doc["selection_points"].to<JsonArray>(); for(uint16_t i=0;i<p->viewer_selection_points_len;++i){ arr.add(p->viewer_selection_points[i]); } JsonArray ov=doc["overlay_points"].to<JsonArray>(); for(uint16_t i=0;i<p->viewer_overlay_points_len;++i){ ov.add(p->viewer_overlay_points[i]); } doc["overlay_points_len"]=p->viewer_overlay_points_len; doc["overlay_accum_count"]=p->viewer_overlay_accum_count; doc["overlay_subset_count"]=p->viewer_overlay_subset_count; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildViewerChartBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, uint16_t p_points) { if(p==nullptr) return false; JsonDocument doc; doc["mode"]="viewer_chart_bundle"; doc["points"]=p_points; doc["frame_id"]=p->viewer_last_frame_id; doc["selection_sync_enabled"]=p->selection_sync_enabled; doc["selection_sync_range_valid"]=p->selection_sync_range_valid; doc["selection_name"]=p->selection_sync_name; doc["selection_effective_from"]=p->selection_sync_effective_from; doc["selection_effective_to"]=p->selection_sync_effective_to; JsonArray wave=doc["waveform"].to<JsonArray>(); uint16_t wave_len=p->viewer_last_waveform_len; if(wave_len>p_points) wave_len=p_points; for(uint16_t i=0;i<wave_len;++i){ wave.add(p->viewer_last_waveform[i]); } JsonArray sel=doc["selection_points"].to<JsonArray>(); for(uint16_t i=0;i<p->viewer_selection_points_len;++i){ sel.add(p->viewer_selection_points[i]); } JsonArray ov=doc["overlay_points"].to<JsonArray>(); for(uint16_t i=0;i<p->viewer_overlay_points_len;++i){ ov.add(p->viewer_overlay_points[i]); } doc["overlay_points_len"]=p->viewer_overlay_points_len; doc["overlay_accum_count"]=p->viewer_overlay_accum_count; doc["overlay_subset_count"]=p->viewer_overlay_subset_count; JsonArray recent_ids=doc["recent_frame_ids"].to<JsonArray>(); for(uint16_t n=0;n<p->viewer_recent_waveform_count;++n){ uint16_t idx=(uint16_t)((p->viewer_recent_waveform_head + G_T20_VIEWER_RECENT_WAVE_COUNT - 1U - n) % G_T20_VIEWER_RECENT_WAVE_COUNT); recent_ids.add(p->viewer_recent_frame_ids[idx]); } JsonObject type=doc["type_meta"].to<JsonObject>(); type["enabled"]=p->type_meta_enabled; type["name"]=p->type_meta_name; type["kind"]=p->type_meta_kind; type["auto_text"]=p->type_meta_auto_text; type["preview_link_path"]=p->type_preview_link_path; type["preview_parser_name"]=p->type_preview_parser_name; type["preview_text_loaded"]=(p->type_preview_text_buf[0]!=0); type["build_sync_state"]="synced"; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }

bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="recorder_manifest"; doc["file_path"]=p->recorder_file_path; doc["record_count"]=p->recorder_record_count; doc["backend"]=(p->recorder_storage_backend==EN_T20_STORAGE_LITTLEFS)?"littlefs":"sdmmc"; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="recorder_index"; doc["count"]=p->recorder_index_count; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) { (void)p; (void)p_bytes; JsonDocument doc; doc["mode"]="recorder_preview"; doc["path"]=(p_path!=nullptr)?p_path:""; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) { (void)p; (void)p_bytes; JsonDocument doc; doc["mode"]="recorder_parsed_preview"; doc["path"]=(p_path!=nullptr)?p_path:""; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderRangeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) { (void)p; JsonDocument doc; doc["mode"]="recorder_range"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["offset"]=p_offset; doc["length"]=p_length; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderBinaryHeaderJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path) { (void)p; JsonDocument doc; doc["mode"]="binary_header"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["magic"]=G_T20_BINARY_MAGIC; doc["version"]=G_T20_BINARY_VERSION; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderCsvTableJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) { return T20_buildRecorderCsvTableAdvancedJsonText(p,p_out_buf,p_len,p_path,p_bytes,"","",0,G_T20_CSV_SORT_ASC,0,G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT); }
bool T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) { return T20_buildRecorderCsvTypeMetaJsonText(p,p_out_buf,p_len,p_path,p_bytes); }
bool T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) { (void)p; (void)p_bytes; JsonDocument doc; doc["mode"]="csv_type_meta"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["todo"]="TODO: 타입 메타 캐시 고도화"; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) { (void)p; (void)p_bytes; JsonDocument doc; doc["mode"]="csv_table_advanced"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["global_filter"]=(p_global_filter!=nullptr)?p_global_filter:""; doc["col_filters"]=(p_col_filters_csv!=nullptr)?p_col_filters_csv:""; doc["sort_col"]=p_sort_col; doc["sort_dir"]=p_sort_dir; doc["page"]=p_page; doc["page_size"]=p_page_size; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderBinaryRecordsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) { if (p==nullptr) return false; JsonDocument doc; doc["mode"]="binary_records"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["offset"]=p_offset; doc["limit"]=p_limit; doc["record_count"]=p->recorder_record_count; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path) { (void)p; JsonDocument doc; doc["mode"]="binary_payload_schema"; doc["path"]=(p_path!=nullptr)?p_path:""; doc["vector_max"]=G_T20_FEATURE_DIM_MAX; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { (void)p; JsonDocument doc; doc["mode"]="render_selection_sync"; doc["series_max"]=G_T20_RECORDER_RENDER_SYNC_SERIES_MAX; doc["selection_sync_max"]=G_T20_RENDER_SELECTION_SYNC_MAX; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }
bool T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len) { (void)p; JsonDocument doc; doc["mode"]="type_meta_preview_link"; doc["preview_link_max"]=G_T20_TYPE_META_PREVIEW_LINK_MAX; return T20_jsonWriteDoc(doc,p_out_buf,p_len); }

bool T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value) { (void)p_json; (void)p_section; (void)p_key; if (p_out_value==nullptr) return false; return false; }
bool T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out_value) { (void)p_json; (void)p_section; (void)p_key; if (p_out_value==nullptr) return false; return false; }
bool T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out_value) { (void)p_json; (void)p_section; (void)p_key; if (p_out_value==nullptr) return false; return false; }
bool T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out_buf, uint16_t p_len) { (void)p_json; (void)p_section; (void)p_key; if (p_out_buf==nullptr||p_len==0) return false; p_out_buf[0]=0; return false; }

bool T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode) { (void)p_json; if (p_out_mode==nullptr) return false; *p_out_mode=EN_T20_OUTPUT_VECTOR; return true; }
bool T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type) { (void)p_json; if (p_out_type==nullptr) return false; *p_out_type=EN_T20_FILTER_HPF; return true; }
bool T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out)
{
    if (p_offset_out == nullptr || p_length_out == nullptr) return false;
    *p_offset_out = 0; *p_length_out = p_file_size;
    if (!p_range.startsWith("bytes=")) return false;
    int dash = p_range.indexOf('-');
    if (dash < 0) return false;
    uint32_t start = (uint32_t)p_range.substring(6, dash).toInt();
    uint32_t end = (uint32_t)p_range.substring(dash + 1).toInt();
    if (start >= p_file_size) return false;
    if (end == 0 || end >= p_file_size) end = p_file_size - 1U;
    if (end < start) return false;
    *p_offset_out = start; *p_length_out = (end - start) + 1U;
    return true;
}
bool T20_isLikelyDateText(const String& p_text) { return (p_text.length()==10 && p_text.indexOf('-')==4); }
bool T20_isLikelyDateTimeText(const String& p_text) { return (p_text.length()>=19 && p_text.indexOf('-')==4 && (p_text.indexOf('T')==10 || p_text.indexOf(' ')==10)); }
String T20_upgradeCsvTypeGuess(const String& p_current, const String& p_cell) { if (p_cell.length()==0) return (p_current.length()>0)?p_current:String("text"); if (p_cell.toFloat()!=0.0f || p_cell=="0") return String("number"); if (T20_isLikelyDateText(p_cell)||T20_isLikelyDateTimeText(p_cell)) return String("date"); return (p_current.length()>0)?p_current:String("text"); }
bool T20_csvRowMatchesGlobalFilter(const std::vector<String>& p_row, const String& p_filter) { if (p_filter.length()==0) return true; String f=p_filter; f.toLowerCase(); for (const auto& cell : p_row) { String v=cell; v.toLowerCase(); if (v.indexOf(f)>=0) return true; } return false; }

bool CL_T20_Mfcc::exportConfigJson(char* p_out_buf, uint16_t p_len) const { return T20_buildConfigJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportConfigSchemaJson(char* p_out_buf, uint16_t p_len) const { return T20_buildConfigSchemaJsonText(p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerDataJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerDataJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerWaveformJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerWaveformJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerSpectrumJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerSpectrumJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerEventsJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerEventsJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerSequenceJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerSequenceJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerOverviewJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerOverviewJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerMultiFrameJson(char* p_out_buf, uint16_t p_len) const { return T20_buildViewerMultiFrameJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportViewerChartBundleJson(char* p_out_buf, uint16_t p_len, uint16_t p_points) const { return T20_buildViewerChartBundleJsonText(_impl, p_out_buf, p_len, p_points); }
bool CL_T20_Mfcc::exportRecorderManifestJson(char* p_out_buf, uint16_t p_len) const { return T20_buildRecorderManifestJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportRecorderIndexJson(char* p_out_buf, uint16_t p_len) const { return T20_buildRecorderIndexJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportRecorderPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const { return T20_buildRecorderPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes); }
bool CL_T20_Mfcc::exportRecorderParsedPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const { return T20_buildRecorderParsedPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes); }
bool CL_T20_Mfcc::exportRecorderRangeJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) const { return T20_buildRecorderRangeJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_length); }
bool CL_T20_Mfcc::exportRecorderBinaryHeaderJson(char* p_out_buf, uint16_t p_len, const char* p_path) const { return T20_buildRecorderBinaryHeaderJsonText(_impl, p_out_buf, p_len, p_path); }
bool CL_T20_Mfcc::exportRecorderCsvTableJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const { return T20_buildRecorderCsvTableJsonText(_impl, p_out_buf, p_len, p_path, p_bytes); }
bool CL_T20_Mfcc::exportRecorderCsvSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const { return T20_buildRecorderCsvSchemaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes); }
bool CL_T20_Mfcc::exportRecorderCsvTypeMetaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const { return T20_buildRecorderCsvTypeMetaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes); }
bool CL_T20_Mfcc::exportRecorderCsvTableAdvancedJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) const { return T20_buildRecorderCsvTableAdvancedJsonText(_impl, p_out_buf, p_len, p_path, p_bytes, p_global_filter, p_col_filters_csv, p_sort_col, p_sort_dir, p_page, p_page_size); }
bool CL_T20_Mfcc::exportRecorderBinaryRecordsJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) const { return T20_buildRecorderBinaryRecordsJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_limit); }
bool CL_T20_Mfcc::exportRecorderBinaryPayloadSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path) const { return T20_buildRecorderBinaryPayloadSchemaJsonText(_impl, p_out_buf, p_len, p_path); }
bool CL_T20_Mfcc::exportRenderSelectionSyncJson(char* p_out_buf, uint16_t p_len) const { return T20_buildRenderSelectionSyncJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportTypeMetaPreviewLinkJson(char* p_out_buf, uint16_t p_len) const { return T20_buildTypeMetaPreviewLinkJsonText(_impl, p_out_buf, p_len); }

void CL_T20_Mfcc::printConfig(Stream& p_out) const { p_out.println(F("----------- T20_Mfcc Config -----------")); p_out.printf("FrameSize   : %u\n", _impl->cfg.feature.frame_size); p_out.printf("HopSize     : %u\n", _impl->cfg.feature.hop_size); p_out.printf("MFCC Coeffs : %u\n", _impl->cfg.feature.mfcc_coeffs); p_out.printf("Output Mode : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE"); p_out.println(F("---------------------------------------")); }
void CL_T20_Mfcc::printStatus(Stream& p_out) const { p_out.println(F("----------- T20_Mfcc Status -----------")); p_out.printf("Initialized  : %s\n", _impl->initialized ? "YES" : "NO"); p_out.printf("Running      : %s\n", _impl->running ? "YES" : "NO"); p_out.printf("Record Count : %lu\n", (unsigned long)_impl->recorder_record_count); p_out.printf("Dropped      : %lu\n", (unsigned long)_impl->dropped_frames); p_out.println(F("---------------------------------------")); }
void CL_T20_Mfcc::printLatest(Stream& p_out) const { p_out.println(F("----------- T20_Mfcc Latest -----------")); p_out.printf("Frame ID     : %lu\n", (unsigned long)_impl->viewer_last_frame_id); p_out.printf("Vector Len   : %u\n", _impl->viewer_last_vector_len); p_out.println(F("---------------------------------------")); }
void CL_T20_Mfcc::printChartSyncStatus(Stream& p_out) const { p_out.println(F("Chart sync: skeleton ready")); }
void CL_T20_Mfcc::printRecorderBackendStatus(Stream& p_out) const { p_out.printf("Recorder Backend : %s\n", _impl->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS ? "LITTLEFS" : "SDMMC"); p_out.printf("Recorder File    : %s\n", _impl->recorder_file_path); p_out.printf("Active Path      : %s\n", _impl->recorder_active_path); p_out.printf("Recorder Opened  : %s\n", _impl->recorder_file_opened ? "YES" : "NO"); p_out.printf("Fallback Active  : %s\n", _impl->recorder_fallback_active ? "YES" : "NO"); p_out.printf("Session Open     : %s\n", _impl->recorder_session_open ? "YES" : "NO"); p_out.printf("SDMMC Profile    : %s\n", _impl->sdmmc_profile.profile_name); p_out.printf("SDMMC Apply Why  : %s\n", _impl->sdmmc_last_apply_reason); p_out.printf("Batch Count      : %u\n", _impl->recorder_batch_count); p_out.printf("Last Flush ms    : %lu\n", (unsigned long)_impl->recorder_last_flush_ms); p_out.printf("Session ID       : %lu\n", (unsigned long)_impl->recorder_session_id); p_out.printf("WM Low/High      : %u / %u\n", _impl->recorder_batch_watermark_low, _impl->recorder_batch_watermark_high); p_out.printf("Selection Sync   : %s [%lu-%lu] eff[%lu-%lu] valid=%s pts=%u\n", _impl->selection_sync_name, (unsigned long)_impl->selection_sync_frame_from, (unsigned long)_impl->selection_sync_frame_to, (unsigned long)_impl->selection_sync_effective_from, (unsigned long)_impl->selection_sync_effective_to, _impl->selection_sync_range_valid ? "YES" : "NO", _impl->viewer_selection_points_len); p_out.printf("Type Meta        : %s / %s / %s / %s / parser=%s rows=%u hints=%u txt=%s schema=%s delim=%s header=%s\n", _impl->type_meta_name, _impl->type_meta_kind, _impl->type_meta_auto_text, _impl->type_preview_link_path, _impl->type_preview_parser_name, _impl->type_preview_sample_row_count, _impl->preview_column_hint_count, _impl->type_preview_text_buf[0] != 0 ? "YES" : "NO", _impl->type_preview_schema_kind, _impl->type_preview_detected_delim, _impl->type_preview_header_guess); p_out.printf("Last Error       : %s\n", _impl->recorder_last_error); }
void CL_T20_Mfcc::printTypeMetaStatus(Stream& p_out) const { p_out.println(F("TypeMeta/Preview link: skeleton ready")); }
void CL_T20_Mfcc::printRoadmapTodo(Stream& p_out) const { p_out.println(F("TODO:")); p_out.println(F("- SD_MMC 보드별 pin/mode/profile 실제 적용")); p_out.println(F("- zero-copy / DMA / cache aligned write 경로 실제화")); p_out.println(F("- selection sync와 waveform/spectrum 구간 연동")); p_out.println(F("- 타입 메타 캐시와 정렬/필터/다운샘플 preview 실제 연계")); }
