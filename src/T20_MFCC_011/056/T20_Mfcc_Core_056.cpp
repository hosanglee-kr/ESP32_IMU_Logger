#include "T20_Mfcc_Inter_056.h"
#include <algorithm>
#include <vector>

/* ============================================================================
 * File: T20_Mfcc_Core_056.cpp
 * Summary:
 *   Core / Config / JSON / Viewer / CSV helper
 *
 * [이번 버전 목적]
 * - 컴파일 복구 우선
 * - Core / JSON / Viewer / CSV helper를 단일 리비전 기준으로 다시 정렬
 * - 실제 고도화 기능은 TODO로 유지
 * ========================================================================== */

CL_T20_Mfcc* g_t20_instance = nullptr;

void IRAM_ATTR T20_onBmiDrdyISR(void) {}
void T20_sensorTask(void* p_arg)      { (void)p_arg; vTaskDelay(portMAX_DELAY); }
void T20_processTask(void* p_arg)     { (void)p_arg; vTaskDelay(portMAX_DELAY); }
void T20_recorderTask(void* p_arg)    { (void)p_arg; vTaskDelay(portMAX_DELAY); }


static bool T20_takeMutex(const SemaphoreHandle_t p_mutex)
{
    if (p_mutex == nullptr) return false;
    return (xSemaphoreTake(p_mutex, pdMS_TO_TICKS(100)) == pdTRUE);
}

static void T20_giveMutex(const SemaphoreHandle_t p_mutex)
{
    if (p_mutex != nullptr) {
        xSemaphoreGive(p_mutex);
    }
}


CL_T20_Mfcc::CL_T20_Mfcc()
: _impl(new ST_Impl())
{
    g_t20_instance = this;
}

CL_T20_Mfcc::~CL_T20_Mfcc()
{
    stop();
    delete _impl;
    _impl = nullptr;
    if (g_t20_instance == this) {
        g_t20_instance = nullptr;
    }
}

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
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    if (p->sensor_task_handle != nullptr)  { vTaskDelete(p->sensor_task_handle);  p->sensor_task_handle = nullptr; }
    if (p->process_task_handle != nullptr) { vTaskDelete(p->process_task_handle); p->process_task_handle = nullptr; }
    if (p->recorder_task_handle != nullptr){ vTaskDelete(p->recorder_task_handle);p->recorder_task_handle = nullptr; }
    p->running = false;
}

void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    if (p->frame_queue != nullptr)    { vQueueDelete(p->frame_queue);    p->frame_queue = nullptr; }
    if (p->recorder_queue != nullptr) { vQueueDelete(p->recorder_queue); p->recorder_queue = nullptr; }
    if (p->mutex != nullptr)          { vSemaphoreDelete(p->mutex);      p->mutex = nullptr; }
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
    memset(p->frame_buffer, 0, sizeof(p->frame_buffer));
    memset(p->work_frame, 0, sizeof(p->work_frame));
    memset(p->temp_frame, 0, sizeof(p->temp_frame));
    memset(p->window, 0, sizeof(p->window));
    memset(p->power, 0, sizeof(p->power));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mfcc_history, 0, sizeof(p->mfcc_history));
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

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p)
{
    (void)p;
    return 0.0f;
}

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

    const uint16_t v_center = G_T20_MFCC_HISTORY / 2U;
    float v_den = 0.0f;
    for (uint16_t n = 1; n <= p_delta_window; ++n) v_den += (float)(n * n);
    v_den *= 2.0f;

    for (uint16_t c = 0; c < p_dim; ++c) {
        float v_num = 0.0f;
        for (uint16_t n = 1; n <= p_delta_window; ++n) {
            v_num += (float)n * (p->mfcc_history[v_center + n][c] - p->mfcc_history[v_center - n][c]);
        }
        p_delta_out[c] = v_num / (v_den + G_T20_EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out)
{
    if (p == nullptr || p_delta2_out == nullptr) return;
    memset(p_delta2_out, 0, sizeof(float) * p_dim);
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) return;
    const uint16_t v_center = G_T20_MFCC_HISTORY / 2U;
    for (uint16_t c = 0; c < p_dim; ++c) {
        p_delta2_out[c] = p->mfcc_history[v_center + 1U][c] - (2.0f * p->mfcc_history[v_center][c]) + p->mfcc_history[v_center - 1U][c];
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
    if (p_rb->head >= p_rb->frames) {
        p_rb->head = 0;
        p_rb->full = true;
    }
}

bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb)
{
    return (p_rb != nullptr) ? p_rb->full : false;
}

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
    if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) {
        p->latest_sequence_valid = false;
        return;
    }
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
    if (_impl->frame_queue == nullptr || _impl->recorder_queue == nullptr) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    _impl->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);
    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);
    pinMode(G_T20_PIN_BMI_INT1, INPUT);

    T20_initProfiles(_impl);
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
    T20_initDSP(_impl);

    _impl->initialized = true;
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (_impl == nullptr || !_impl->initialized || _impl->running) return false;
    _impl->running = true;
    return true;
}

void CL_T20_Mfcc::stop(void)
{
    if (_impl == nullptr) return;
    T20_stopTasks(_impl);
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr || p_cfg == nullptr || !T20_validateConfig(p_cfg)) return false;
    if (!T20_takeMutex(_impl->mutex)) return false;
    _impl->cfg = *p_cfg;
    _impl->viewer_effective_hop_size = p_cfg->feature.hop_size;
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames, (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));
    T20_giveMutex(_impl->mutex);
    return true;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const
{
    if (_impl == nullptr || p_cfg_out == nullptr) return;
    *p_cfg_out = _impl->cfg;
}

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

bool CL_T20_Mfcc::isSequenceReady(void) const
{
    return (_impl != nullptr) ? T20_seqIsReady(&_impl->seq_rb) : false;
}

bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    if (_impl == nullptr || p_out_seq == nullptr) return false;
    uint16_t need = (uint16_t)(_impl->seq_rb.frames * _impl->seq_rb.feature_dim);
    if (need == 0 || p_len < need || !_impl->latest_sequence_valid) return false;
    T20_seqExportFlatten(&_impl->seq_rb, p_out_seq);
    return true;
}

bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const
{
    return getLatestSequenceFlat(p_out_seq, p_len);
}

bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out)
{
    if (p_cfg_out == nullptr) return false;
    *p_cfg_out = T20_makeDefaultConfig();
    return true;
}

bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out)
{
    (void)p_profile_index;
    return T20_loadRuntimeConfigFromLittleFs(p_cfg_out);
}

bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    p->recorder_index_count = 0;
    return true;
}

bool T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg)
{
    (void)p_profile_index; (void)p_cfg;
    return true;
}

bool T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg)
{
    (void)p_cfg;
    return true;
}

bool T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out)
{
    (void)p_json_text;
    if (p_cfg_out == nullptr) return false;
    *p_cfg_out = T20_makeDefaultConfig();
    return true;
}

bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json)
{
    return T20_applyConfigJsonText(p, p_json);
}

bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text)
{
    if (p == nullptr || p_json_text == nullptr) return false;
    ST_T20_Config_t cfg = p->cfg;
    if (!T20_parseConfigJsonText(p_json_text, &cfg)) return false;
    p->cfg = cfg;
    return true;
}

static bool T20_jsonWriteDoc(const JsonDocument& p_doc, char* p_out_buf, uint16_t p_len)
{
    if (p_out_buf == nullptr || p_len == 0) return false;
    size_t need = measureJson(p_doc) + 1U;
    if (need > p_len) return false;
    serializeJson(p_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildConfigJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["frame_size"] = p->cfg.feature.frame_size;
    doc["hop_size"] = p->cfg.feature.hop_size;
    doc["output_mode"] = (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len)
{
    JsonDocument doc;
    doc["mode"] = "config_schema";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_waveform";
    doc["len"] = p->viewer_last_waveform_len;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_spectrum";
    doc["len"] = p->viewer_last_spectrum_len;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_data";
    doc["frame_id"] = p->viewer_last_frame_id;
    doc["vector_len"] = p->viewer_last_vector_len;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_events";
    JsonArray arr = doc["events"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_event_count && i < G_T20_VIEWER_EVENT_MAX; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["frame_id"] = p->viewer_events[i].frame_id;
        o["kind"] = p->viewer_events[i].kind;
        o["text"] = p->viewer_events[i].text;
    }
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_sequence";
    doc["ready"] = p->latest_sequence_valid;
    doc["frames"] = p->seq_rb.frames;
    doc["feature_dim"] = p->seq_rb.feature_dim;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    return T20_buildViewerDataJsonText(p, p_out_buf, p_len);
}

bool T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "viewer_multi_frame";
    doc["count"] = p->viewer_recent_waveform_count;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildViewerChartBundleJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, uint16_t p_points)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "viewer_chart_bundle";
    doc["points"] = p_points;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "recorder_manifest";
    doc["file_path"] = p->recorder_file_path;
    doc["record_count"] = p->recorder_record_count;
    doc["backend"] = (p->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS) ? "littlefs" : "sdmmc";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "recorder_index";
    doc["count"] = p->recorder_index_count;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes)
{
    (void)p; (void)p_bytes;
    JsonDocument doc;
    doc["mode"] = "recorder_preview";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes)
{
    (void)p; (void)p_bytes;
    JsonDocument doc;
    doc["mode"] = "recorder_parsed_preview";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderRangeJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "recorder_range";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["offset"] = p_offset;
    doc["length"] = p_length;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderBinaryHeaderJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "binary_header";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["magic"] = G_T20_BINARY_MAGIC;
    doc["version"] = G_T20_BINARY_VERSION;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderCsvTableJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes)
{
    return T20_buildRecorderCsvTableAdvancedJsonText(p, p_out_buf, p_len, p_path, p_bytes, "", "", 0, G_T20_CSV_SORT_ASC, 0, G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT);
}

bool T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes)
{
    return T20_buildRecorderCsvTypeMetaJsonText(p, p_out_buf, p_len, p_path, p_bytes);
}

bool T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int* p_out_value)
{
    (void)p_json; (void)p_section; (void)p_key;
    if (p_out_value == nullptr) return false;
    return false;
}

bool T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out_value)
{
    (void)p_json; (void)p_section; (void)p_key;
    if (p_out_value == nullptr) return false;
    return false;
}

bool T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out_value)
{
    (void)p_json; (void)p_section; (void)p_key;
    if (p_out_value == nullptr) return false;
    return false;
}

bool T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out_buf, uint16_t p_len)
{
    (void)p_json; (void)p_section; (void)p_key;
    if (p_out_buf == nullptr || p_len == 0) return false;
    p_out_buf[0] = 0;
    return false;
}

bool T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode)
{
    (void)p_json;
    if (p_out_mode == nullptr) return false;
    *p_out_mode = EN_T20_OUTPUT_VECTOR;
    return true;
}

bool T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type)
{
    (void)p_json;
    if (p_out_type == nullptr) return false;
    *p_out_type = EN_T20_FILTER_HPF;
    return true;
}

bool T20_parseHttpRangeHeader(const String& p_range, uint32_t p_file_size, uint32_t* p_offset_out, uint32_t* p_length_out)
{
    if (p_offset_out == nullptr || p_length_out == nullptr) return false;
    *p_offset_out = 0;
    *p_length_out = p_file_size;
    if (!p_range.startsWith("bytes=")) return false;
    int dash = p_range.indexOf('-');
    if (dash < 0) return false;
    uint32_t start = (uint32_t)p_range.substring(6, dash).toInt();
    uint32_t end = (uint32_t)p_range.substring(dash + 1).toInt();
    if (start >= p_file_size) return false;
    if (end == 0 || end >= p_file_size) end = p_file_size - 1U;
    if (end < start) return false;
    *p_offset_out = start;
    *p_length_out = (end - start) + 1U;
    return true;
}

bool T20_isLikelyDateText(const String& p_text)
{
    return (p_text.length() == 10 && p_text.indexOf('-') == 4);
}

bool T20_isLikelyDateTimeText(const String& p_text)
{
    return (p_text.length() >= 19 && p_text.indexOf('-') == 4 && (p_text.indexOf('T') == 10 || p_text.indexOf(' ') == 10));
}

String T20_upgradeCsvTypeGuess(const String& p_current, const String& p_cell)
{
    if (p_cell.length() == 0) return (p_current.length() > 0) ? p_current : String("text");
    double v_num = 0.0;
    if (p_cell.toFloat() != 0.0f || p_cell == "0") return String("number");
    if (T20_isLikelyDateTimeText(p_cell) || T20_isLikelyDateText(p_cell)) return String("date");
    return (p_current.length() > 0) ? p_current : String("text");
}

bool T20_csvRowMatchesGlobalFilter(const std::vector<String>& p_row, const String& p_filter)
{
    if (p_filter.length() == 0) return true;
    String f = p_filter;
    f.toLowerCase();
    for (const auto& cell : p_row) {
        String v = cell;
        v.toLowerCase();
        if (v.indexOf(f) >= 0) return true;
    }
    return false;
}

bool T20_buildRecorderCsvTypeMetaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes)
{
    (void)p; (void)p_bytes;
    JsonDocument doc;
    doc["mode"] = "csv_type_meta";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["todo"] = "TODO: 타입 메타 캐시 고도화";
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes,
                                               const char* p_global_filter, const char* p_col_filters_csv,
                                               uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size)
{
    (void)p; (void)p_bytes;
    JsonDocument doc;
    doc["mode"] = "csv_table_advanced";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["global_filter"] = (p_global_filter != nullptr) ? p_global_filter : "";
    doc["col_filters"] = (p_col_filters_csv != nullptr) ? p_col_filters_csv : "";
    doc["sort_col"] = p_sort_col;
    doc["sort_dir"] = p_sort_dir;
    doc["page"] = p_page;
    doc["page_size"] = p_page_size;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderBinaryRecordsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit)
{
    if (p == nullptr) return false;
    JsonDocument doc;
    doc["mode"] = "binary_records";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["offset"] = p_offset;
    doc["limit"] = p_limit;
    doc["record_count"] = p->recorder_record_count;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len, const char* p_path)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "binary_payload_schema";
    doc["path"] = (p_path != nullptr) ? p_path : "";
    doc["vector_max"] = G_T20_FEATURE_DIM_MAX;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "render_selection_sync";
    doc["series_max"] = G_T20_RECORDER_RENDER_SYNC_SERIES_MAX;
    doc["selection_sync_max"] = G_T20_RENDER_SELECTION_SYNC_MAX;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

bool T20_buildTypeMetaPreviewLinkJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    (void)p;
    JsonDocument doc;
    doc["mode"] = "type_meta_preview_link";
    doc["preview_link_max"] = G_T20_TYPE_META_PREVIEW_LINK_MAX;
    return T20_jsonWriteDoc(doc, p_out_buf, p_len);
}

/* ============================================================================
 * [공개 export wrapper]
 * ========================================================================== */
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
bool CL_T20_Mfcc::exportRecorderCsvTableAdvancedJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) const
{
    return T20_buildRecorderCsvTableAdvancedJsonText(_impl, p_out_buf, p_len, p_path, p_bytes, p_global_filter, p_col_filters_csv, p_sort_col, p_sort_dir, p_page, p_page_size);
}
bool CL_T20_Mfcc::exportRecorderBinaryRecordsJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) const { return T20_buildRecorderBinaryRecordsJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_limit); }
bool CL_T20_Mfcc::exportRecorderBinaryPayloadSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path) const { return T20_buildRecorderBinaryPayloadSchemaJsonText(_impl, p_out_buf, p_len, p_path); }
bool CL_T20_Mfcc::exportRenderSelectionSyncJson(char* p_out_buf, uint16_t p_len) const { return T20_buildRenderSelectionSyncJsonText(_impl, p_out_buf, p_len); }
bool CL_T20_Mfcc::exportTypeMetaPreviewLinkJson(char* p_out_buf, uint16_t p_len) const { return T20_buildTypeMetaPreviewLinkJsonText(_impl, p_out_buf, p_len); }

/* ============================================================================
 * [print helper]
 * ========================================================================== */
void CL_T20_Mfcc::printConfig(Stream& p_out) const
{
    p_out.println(F("----------- T20_Mfcc Config -----------"));
    p_out.printf("FrameSize   : %u\n", _impl->cfg.feature.frame_size);
    p_out.printf("HopSize     : %u\n", _impl->cfg.feature.hop_size);
    p_out.printf("MFCC Coeffs : %u\n", _impl->cfg.feature.mfcc_coeffs);
    p_out.printf("Output Mode : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
    p_out.println(F("---------------------------------------"));
}

void CL_T20_Mfcc::printStatus(Stream& p_out) const
{
    p_out.println(F("----------- T20_Mfcc Status -----------"));
    p_out.printf("Initialized  : %s\n", _impl->initialized ? "YES" : "NO");
    p_out.printf("Running      : %s\n", _impl->running ? "YES" : "NO");
    p_out.printf("Record Count : %lu\n", (unsigned long)_impl->recorder_record_count);
    p_out.printf("Dropped      : %lu\n", (unsigned long)_impl->dropped_frames);
    p_out.println(F("---------------------------------------"));
}

void CL_T20_Mfcc::printLatest(Stream& p_out) const
{
    p_out.println(F("----------- T20_Mfcc Latest -----------"));
    p_out.printf("Frame ID     : %lu\n", (unsigned long)_impl->viewer_last_frame_id);
    p_out.printf("Vector Len   : %u\n", _impl->viewer_last_vector_len);
    p_out.println(F("---------------------------------------"));
}

void CL_T20_Mfcc::printChartSyncStatus(Stream& p_out) const
{
    p_out.println(F("Chart sync: skeleton ready"));
}

void CL_T20_Mfcc::printRecorderBackendStatus(Stream& p_out) const
{
    p_out.printf("Recorder Backend : %s\n", _impl->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS ? "LITTLEFS" : "SDMMC");
    p_out.printf("Recorder File    : %s\n", _impl->recorder_file_path);
    p_out.printf("Last Error       : %s\n", _impl->recorder_last_error);
}

void CL_T20_Mfcc::printTypeMetaStatus(Stream& p_out) const
{
    p_out.println(F("TypeMeta/Preview link: skeleton ready"));
}

void CL_T20_Mfcc::printRoadmapTodo(Stream& p_out) const
{
    p_out.println(F("TODO:"));
    p_out.println(F("- SD_MMC 보드별 pin/mode/profile 실제 적용"));
    p_out.println(F("- zero-copy / DMA / cache aligned write 경로 실제화"));
    p_out.println(F("- selection sync와 waveform/spectrum 구간 연동"));
    p_out.println(F("- 타입 메타 캐시와 정렬/필터/다운샘플 preview 실제 연계"));
}
