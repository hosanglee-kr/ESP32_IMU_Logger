#include "T20_Mfcc_Inter_052.h"

/*
===============================================================================
소스명: T20_Mfcc_Core_052.cpp
버전: v052

[기능 스펙]
- 모듈 생명주기 관리(begin/start/stop)
- session state / button toggle 처리
- ISR / SensorTask / ProcessTask
- sample ring buffer + sliding window dispatch
- runtime 상태 / 출력 / sequence 관리
- recorder batching/flush 연동 시작점 통합

[향후 단계 TODO]
- event marker API 공개
- multi-user web session 정책
- zero-copy / DMA / cache aligned 최적화
- 전체 JSON 스키마 정식 파서 고도화
- 복수 설정 프로파일 전환 UI
===============================================================================
*/

CL_T20_Mfcc* g_t20_instance = nullptr;

void IRAM_ATTR T20_onBmiDrdyISR(void);
void T20_sensorTask(void* p_arg);
void T20_processTask(void* p_arg);
void T20_recorderTask(void* p_arg);

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
    T20_initProfiles(_impl);

    if (p_cfg != nullptr) {
        _impl->cfg = *p_cfg;
    } else {
        _impl->cfg = T20_makeDefaultConfig();
        ST_T20_Config_t v_loaded_cfg;
        if (T20_loadRuntimeConfigFromLittleFs(&v_loaded_cfg)) {
            _impl->cfg = v_loaded_cfg;
        } else if (T20_loadProfileFromLittleFs(G_T20_CFG_PROFILE_INDEX_DEFAULT, &v_loaded_cfg)) {
            _impl->cfg = v_loaded_cfg;
            _impl->current_profile_index = G_T20_CFG_PROFILE_INDEX_DEFAULT;
        }
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

        if (!T20_createRecorderObjects(_impl)) {
            break;
        }

        _impl->recorder_queue = xQueueCreate(G_T20_RECORDER_QUEUE_LEN, sizeof(ST_T20_RecorderQueueItem_t));
        if (_impl->recorder_queue == nullptr) {
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
        pinMode(_impl->cfg.button.button_pin, _impl->cfg.button.active_low ? INPUT_PULLUP : INPUT);

        if (!T20_initDSP(_impl)) {
            break;
        }

        if (!T20_initBMI270_SPI(_impl)) {
            break;
        }

        T20_loadRecorderIndex(_impl);

        if (!T20_configBMI270_1600Hz_DRDY(_impl)) {
            break;
        }

        if (!T20_configureFilter(_impl)) {
            break;
        }

        if (!T20_recorderBegin(_impl)) {
            break;
        }

        T20_seqInit(&_impl->seq_rb,
                    _impl->cfg.output.sequence_frames,
                    (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));

        _impl->session_state = EN_T20_SESSION_READY;
        ok = true;
    } while (0);

    if (!ok) {
        T20_resetRuntimeResources(_impl);
        return false;
    }

    _impl->initialized = true;
    _impl->web_last_live_change_ms = millis();
    _impl->web_last_status_change_ms = millis();
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (_impl == nullptr || !_impl->initialized || _impl->running) {
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

    BaseType_t r3 = xTaskCreatePinnedToCore(
        T20_recorderTask,
        "T20_Recorder",
        G_T20_RECORDER_TASK_STACK,
        _impl,
        G_T20_RECORDER_TASK_PRIO,
        &_impl->recorder_task_handle,
        1
    );

    if (r1 != pdPASS || r2 != pdPASS || r3 != pdPASS) {
        T20_stopTasks(_impl);
        return false;
    }

    attachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1), T20_onBmiDrdyISR, RISING);
    _impl->running = true;
    T20_webPushLiveNow(_impl);
    return true;
}

void CL_T20_Mfcc::stop(void)
{
    T20_stopTasks(_impl);
    T20_webPushLiveNow(_impl);
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg)
{
    if (_impl == nullptr || !T20_validateConfig(p_cfg) || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    _impl->cfg = *p_cfg;
    bool ok = T20_configureFilter(_impl);

    T20_seqInit(&_impl->seq_rb,
                _impl->cfg.output.sequence_frames,
                (uint16_t)(_impl->cfg.feature.mfcc_coeffs * 3U));

    xSemaphoreGive(_impl->mutex);
    return ok;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const
{
    if (_impl == nullptr || p_cfg_out == nullptr || _impl->mutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    *p_cfg_out = _impl->cfg;
    xSemaphoreGive(_impl->mutex);
}

bool CL_T20_Mfcc::measurementStart(void)
{
    return T20_measurementStart(_impl);
}

bool CL_T20_Mfcc::measurementStop(void)
{
    return T20_measurementStop(_impl);
}

EM_T20_SessionState_t CL_T20_Mfcc::getSessionState(void) const
{
    if (_impl == nullptr) {
        return EN_T20_SESSION_ERROR;
    }
    return _impl->session_state;
}

void CL_T20_Mfcc::pollButton(void)
{
    if (_impl == nullptr || !_impl->initialized) {
        return;
    }

    EM_T20_ButtonEvent_t evt = T20_pollButtonEvent(_impl);
    if (evt == EN_T20_BUTTON_EVENT_SHORT_PRESS &&
        _impl->cfg.button.short_press_toggle_measurement) {
        if (_impl->session_state == EN_T20_SESSION_RECORDING) {
            T20_measurementStop(_impl);
        } else if (_impl->session_state == EN_T20_SESSION_READY) {
            T20_measurementStart(_impl);
        }
    }

    // TODO:
    // - long press -> event marker
    // - long press -> calibration / safe mode / wifi UI toggle
}


bool CL_T20_Mfcc::attachWebStaticFiles(void* p_server)
{
    return T20_webAttachStaticFiles(_impl, p_server);
}

bool CL_T20_Mfcc::attachWebServer(void* p_server, const char* p_base_path)
{
    return T20_webAttach(_impl, p_server, p_base_path);
}

void CL_T20_Mfcc::detachWebServer(void)
{
    T20_webDetach(_impl);
}

bool CL_T20_Mfcc::getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const
{
    if (_impl == nullptr || p_out == nullptr || _impl->mutex == nullptr) {
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
    if (_impl == nullptr || p_out_vec == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool ok = _impl->latest_vector_valid;
    uint16_t need = _impl->latest_feature.vector_len;

    if (!ok || need == 0 || p_len < need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    memcpy(p_out_vec, _impl->latest_feature.vector, sizeof(float) * need);
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

    bool ready = T20_seqIsReady(&_impl->seq_rb);
    xSemaphoreGive(_impl->mutex);
    return ready;
}

bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    if (_impl == nullptr || p_out_seq == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    uint16_t need = (uint16_t)(_impl->seq_rb.frames * _impl->seq_rb.feature_dim);
    if (need == 0 || p_len < need) {
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

bool CL_T20_Mfcc::getLatestWaveFrame(float* p_out_frame, uint16_t p_len, uint16_t* p_out_valid_len) const
{
    if (p_out_frame == nullptr || _impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    const uint16_t v_need = G_T20_FFT_SIZE;
    if (p_len < v_need) {
        xSemaphoreGive(_impl->mutex);
        return false;
    }

    memcpy(p_out_frame, _impl->latest_wave_frame, sizeof(float) * v_need);
    if (p_out_valid_len != nullptr) {
        *p_out_valid_len = v_need;
    }

    xSemaphoreGive(_impl->mutex);
    return true;
}

bool CL_T20_Mfcc::getSequenceShape(uint16_t* p_out_frames, uint16_t* p_out_feature_dim) const
{
    if (_impl->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    if (p_out_frames != nullptr) {
        *p_out_frames = _impl->seq_rb.frames;
    }
    if (p_out_feature_dim != nullptr) {
        *p_out_feature_dim = _impl->seq_rb.feature_dim;
    }

    xSemaphoreGive(_impl->mutex);
    return true;
}


bool CL_T20_Mfcc::applyConfigJson(const char* p_json)
{
    return T20_applyConfigJson(_impl, p_json);
}



bool CL_T20_Mfcc::exportViewerWaveformJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerWaveformJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportViewerSpectrumJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerSpectrumJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportViewerEventsJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerEventsJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportViewerSequenceJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerSequenceJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportViewerOverviewJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerOverviewJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportViewerMultiFrameJson(char* p_out_buf, uint16_t p_len) const
{
    return T20_buildViewerMultiFrameJsonText(_impl, p_out_buf, p_len);
}

bool CL_T20_Mfcc::exportRecorderPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const
{
    return T20_buildRecorderPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}

bool CL_T20_Mfcc::exportRecorderParsedPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const
{
    return T20_buildRecorderParsedPreviewJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}

bool CL_T20_Mfcc::exportRecorderRangeJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) const
{
    return T20_buildRecorderRangeJsonText(_impl, p_out_buf, p_len, p_path, p_offset, p_length);
}

bool CL_T20_Mfcc::exportViewerChartBundleJson(char* p_out_buf, uint16_t p_len, uint16_t p_points) const
{
    return T20_buildViewerChartBundleJsonText(_impl, p_out_buf, p_len, p_points);
}

bool CL_T20_Mfcc::exportRecorderBinaryHeaderJson(char* p_out_buf, uint16_t p_len, const char* p_path) const
{
    return T20_buildRecorderBinaryHeaderJsonText(_impl, p_out_buf, p_len, p_path);
}

bool CL_T20_Mfcc::exportRecorderCsvTableJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const
{
    return T20_buildRecorderCsvTableJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}

bool CL_T20_Mfcc::exportRecorderCsvSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const
{
    return T20_buildRecorderCsvSchemaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}

bool CL_T20_Mfcc::exportRecorderCsvTableFilteredJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, uint16_t p_sort_col, uint16_t p_page, uint16_t p_page_size) const
{
    return T20_buildRecorderCsvTableFilteredJsonText(_impl, p_out_buf, p_len, p_path, p_bytes, p_global_filter, p_sort_col, p_page, p_page_size);
}

bool CL_T20_Mfcc::exportRecorderCsvTableAdvancedJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes, const char* p_global_filter, const char* p_col_filters_csv, uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) const
{
    return T20_buildRecorderCsvTableAdvancedJsonText(_impl, p_out_buf, p_len, p_path, p_bytes, p_global_filter, p_col_filters_csv, p_sort_col, p_sort_dir, p_page, p_page_size);
}

bool CL_T20_Mfcc::exportRecorderCsvTypeMetaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const
{
    return T20_buildRecorderCsvTypeMetaJsonText(_impl, p_out_buf, p_len, p_path, p_bytes);
}

bool CL_T20_Mfcc::requestRecorderFlush(void)
{
    if (_impl == nullptr) {
        return false;
    }
    _impl->recorder_flush_requested = true;
    return true;
}

bool CL_T20_Mfcc::requestRecorderFinalize(void)
{
    if (_impl == nullptr) {
        return false;
    }
    _impl->recorder_finalize_requested = true;
    return true;
}

bool CL_T20_Mfcc::exportRecorderBinaryRecordsJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_count) const
{
    return T20_buildRecorderBinaryRecordsJsonText(_impl, p_out_buf, p_len, p_path, p_count);
}

bool CL_T20_Mfcc::exportRecorderBinaryPayloadSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path) const
{
    return T20_buildRecorderBinaryPayloadSchemaJsonText(_impl, p_out_buf, p_len, p_path);
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
    p_out.printf("Output Mode     : %s\n", _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
    p_out.printf("Seq Frames      : %u\n", _impl->cfg.output.sequence_frames);
    p_out.printf("Stage Count     : %u\n", _impl->cfg.preprocess.pipeline.stage_count);
    p_out.printf("Button Pin      : %u\n", _impl->cfg.button.button_pin);
    p_out.printf("Recorder Enable : %s\n", _impl->cfg.recorder.enable ? "ON" : "OFF");
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
    p_out.printf("Session State   : %d\n", (int)_impl->session_state);
    p_out.printf("Dropped Frames  : %lu\n", (unsigned long)_impl->dropped_frames);
    p_out.printf("Profile Index   : %u\n", _impl->current_profile_index);
    p_out.printf("Samples Written : %lu\n", (unsigned long)_impl->total_samples_written);
    p_out.printf("Next Frame Start: %lu\n", (unsigned long)_impl->next_frame_start_sample);
    p_out.printf("History Count   : %u\n", _impl->mfcc_history_count);
    p_out.printf("Noise Learned   : %u\n", _impl->noise_learned_frames);
    p_out.printf("Seq Frames      : %u\n", _impl->seq_rb.frames);
    p_out.printf("Effective Hop   : %u\n", _impl->viewer_effective_hop_size);
    p_out.printf("Seq Feature Dim : %u\n", _impl->seq_rb.feature_dim);
    p_out.printf("Seq Full        : %s\n", _impl->seq_rb.full ? "YES" : "NO");
    p_out.printf("Rec Last Error  : %s\n", _impl->recorder.last_error_text[0] ? _impl->recorder.last_error_text : "-");
    p_out.printf("Vector Valid    : %s\n", _impl->latest_vector_valid ? "YES" : "NO");
    p_out.printf("Sequence Valid  : %s\n", _impl->latest_sequence_valid ? "YES" : "NO");
    p_out.printf("Recorder Mount  : %s\n", _impl->recorder.mounted ? "YES" : "NO");
    p_out.printf("Recorder Open   : %s\n", _impl->recorder.session_open ? "YES" : "NO");
    p_out.printf("Recorder Queue  : %u\n", (unsigned)uxQueueMessagesWaiting(_impl->recorder_queue));
    p_out.println(F("---------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

bool CL_T20_Mfcc::webPushNow(void)
{
    if (_impl == nullptr) {
        return false;
    }
    return T20_webPushLiveNow(_impl);
}

void CL_T20_Mfcc::printRecorderStatus(Stream& p_out) const
{
    if (_impl->mutex == nullptr || xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        p_out.println(F("mutex timeout"));
        return;
    }

    p_out.println(F("------ T20_Mfcc Recorder Status ------"));
    const char* v_state = "IDLE";
    if (_impl->recorder_state == EN_T20_RECORDER_OPEN) {
        v_state = "OPEN";
    } else if (_impl->recorder_state == EN_T20_RECORDER_RECORDING) {
        v_state = "RECORDING";
    } else if (_impl->recorder_state == EN_T20_RECORDER_STOPPING) {
        v_state = "STOPPING";
    }
    p_out.printf("Recorder State  : %s\n", v_state);
    p_out.printf("Record Count    : %lu\n", (unsigned long)_impl->recorder_record_count);
    p_out.printf("Last Timestamp  : %lu\n", (unsigned long)_impl->recorder_last_timestamp_ms);
    p_out.printf("Batch Pending   : %lu\n", (unsigned long)_impl->recorder_batch_pending);
    p_out.printf("Flush Count     : %lu\n", (unsigned long)_impl->recorder_flush_count);
    p_out.printf("Flush Req       : %s\n", _impl->recorder_flush_requested ? "YES" : "NO");
    p_out.printf("Finalize Req    : %s\n", _impl->recorder_finalize_requested ? "YES" : "NO");
    p_out.printf("Storage Backend : %s\n", _impl->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS ? "LITTLEFS" : "SDMMC");
    p_out.printf("File Path       : %s\n", _impl->recorder_file_path[0] ? _impl->recorder_file_path : "-");
    p_out.println(F("--------------------------------------"));

    xSemaphoreGive(_impl->mutex);
}

void CL_T20_Mfcc::printRoadmapTodo(Stream& p_out) const
{
    p_out.println(F("------ T20_Mfcc Future TODO ------"));
    p_out.println(F("- recorder queue/task batch flush 최적화"));
    p_out.println(F("- SD_MMC 공통 writer 추상화"));
    p_out.println(F("- CSV 서버측 컬럼별 필터/정렬 강화"));
    p_out.println(F("- 멀티 캔버스 동기 zoom/pan 정교화"));
    p_out.println(F("- zero-copy / DMA / cache aligned 최적화"));
    p_out.println(F("----------------------------------"));
}

void CL_T20_Mfcc::printChartSyncStatus(Stream& p_out) const
{
    p_out.println(F("------ T20_Mfcc Chart Sync ------"));
    p_out.println(F("Shared sync state는 웹 UI(JS)에서 관리"));
    p_out.println(F("현재 단계: zoom/pan 공유 상태 + 다중 canvas 적용 골격"));
    p_out.println(F("향후 단계: 실제 동시 렌더/selection 연동 정교화"));
    p_out.println(F("---------------------------------"));
}

void CL_T20_Mfcc::printRecorderBackendStatus(Stream& p_out) const
{
    if (_impl == nullptr) {
        p_out.println(F("recorder backend: no impl"));
        return;
    }

    p_out.println(F("---- T20 Recorder Backend ----"));
    p_out.printf("Storage Backend : %s\n", _impl->recorder_storage_backend == EN_T20_STORAGE_LITTLEFS ? "LITTLEFS" : "SDMMC");
    p_out.printf("SDMMC Mounted   : %s\n", _impl->recorder_sdmmc_mounted ? "YES" : "NO");
    p_out.printf("Mount Path      : %s\n", _impl->recorder_sdmmc_mount_path);
    p_out.printf("Board Hint      : %s\n", _impl->recorder_sdmmc_board_hint[0] ? _impl->recorder_sdmmc_board_hint : "-");
    p_out.printf("ZeroCopy Slot   : %u\n", (unsigned)_impl->recorder_zero_copy_slot_index);
    p_out.println(F("------------------------------"));
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

    ST_T20_FeatureVector_t feat = _impl->latest_feature;
    xSemaphoreGive(_impl->mutex);

    p_out.print(F("Log Mel   : "));
    for (uint16_t i = 0; i < feat.log_mel_len; ++i) p_out.printf("%.4f ", feat.log_mel[i]);
    p_out.println();

    p_out.print(F("MFCC      : "));
    for (uint16_t i = 0; i < feat.mfcc_len; ++i) p_out.printf("%.4f ", feat.mfcc[i]);
    p_out.println();

    p_out.print(F("Delta     : "));
    for (uint16_t i = 0; i < feat.delta_len; ++i) p_out.printf("%.4f ", feat.delta[i]);
    p_out.println();

    p_out.print(F("DeltaDelta: "));
    for (uint16_t i = 0; i < feat.delta2_len; ++i) p_out.printf("%.4f ", feat.delta2[i]);
    p_out.println();

    p_out.printf("Vector Len : %u\n", feat.vector_len);
}

// ============================================================================
// [Session / Button Helpers]
// ============================================================================

bool T20_measurementStart(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->initialized) {
        return false;
    }

    if (p->session_state == EN_T20_SESSION_RECORDING) {
        return true;
    }

    if (p->cfg.recorder.enable) {
        if (!T20_recorderOpenSession(p)) {
            p->session_state = EN_T20_SESSION_ERROR;
            return false;
        }
        (void)T20_recorderWriteConfig(p, &p->cfg);
        (void)T20_recorderEnqueueEvent(p, "measurement_start");
    }

    p->measurement_start_ms = millis();
    p->session_state = EN_T20_SESSION_RECORDING;
    p->next_frame_start_sample = p->total_samples_written;
    return true;
}

bool T20_measurementStop(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->initialized) {
        return false;
    }

    if (p->session_state != EN_T20_SESSION_RECORDING) {
        return false;
    }

    p->measurement_stop_ms = millis();

    if (p->cfg.recorder.enable) {
        (void)T20_recorderEnqueueEvent(p, "measurement_stop");
        T20_recorderCloseSession(p);
    }

    p->session_state = EN_T20_SESSION_READY;
    return true;
}

EM_T20_ButtonEvent_t T20_pollButtonEvent(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return EN_T20_BUTTON_EVENT_NONE;
    }

    bool raw = digitalRead(p->cfg.button.button_pin) ? true : false;
    if (p->cfg.button.active_low) {
        raw = !raw;
    }

    uint32_t now_ms = millis();

    if (raw != p->button_last_raw) {
        p->button_last_raw = raw;
        p->button_last_change_ms = now_ms;
    }

    if ((now_ms - p->button_last_change_ms) < p->cfg.button.debounce_ms) {
        return EN_T20_BUTTON_EVENT_NONE;
    }

    if (raw != p->button_stable_state) {
        p->button_stable_state = raw;

        if (raw) {
            p->button_press_start_ms = now_ms;
            p->button_long_reported = false;
        } else {
            uint32_t press_ms = now_ms - p->button_press_start_ms;
            if (!p->button_long_reported && press_ms < p->cfg.button.long_press_ms) {
                return EN_T20_BUTTON_EVENT_SHORT_PRESS;
            }
        }
    }

    if (p->button_stable_state && !p->button_long_reported) {
        if ((now_ms - p->button_press_start_ms) >= p->cfg.button.long_press_ms) {
            p->button_long_reported = true;
            return EN_T20_BUTTON_EVENT_LONG_PRESS;
        }
    }

    return EN_T20_BUTTON_EVENT_NONE;
}

// ============================================================================
// [ISR / Tasks]
// ============================================================================

void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    if (g_t20_instance == nullptr || g_t20_instance->_impl == nullptr) {
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

        while (notify_count-- > 0U) {
            uint16_t interrupt_status = 0;
            if (p->imu.getInterruptStatus(&interrupt_status) != BMI2_OK) {
                continue;
            }

            if ((interrupt_status & BMI2_ACC_DRDY_INT_MASK) == 0U) {
                continue;
            }

            if (p->imu.getSensorData() != BMI2_OK) {
                continue;
            }

            float sample = T20_selectAxisSample(p);

            uint32_t write_idx = p->sample_write_index % G_T20_SAMPLE_RING_SIZE;
            p->sample_ring[write_idx] = sample;
            p->sample_write_index = (write_idx + 1U) % G_T20_SAMPLE_RING_SIZE;
            p->total_samples_written++;

            uint8_t buf = p->active_fill_buffer;
            uint16_t idx = p->active_sample_index;
            if (idx < G_T20_FFT_SIZE) {
                p->frame_buffer[buf][idx] = sample;
                idx++;
                p->active_sample_index = idx;
                if (idx >= G_T20_FFT_SIZE) {
                    p->active_fill_buffer = (uint8_t)((buf + 1U) % G_T20_RAW_FRAME_BUFFERS);
                    p->active_sample_index = 0;
                }
            }

            (void)T20_tryEnqueueReadyFrames(p);
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

        if (!p->running || p->session_state != EN_T20_SESSION_RECORDING) {
            continue;
        }

        ST_T20_ConfigSnapshot_t cfg_snap;
        if (!T20_buildConfigSnapshot(p, &cfg_snap)) {
            continue;
        }

        ST_T20_PipelineSnapshot_t pipe_snap;
        if (!T20_buildPipelineSnapshot(&cfg_snap.cfg, &pipe_snap)) {
            continue;
        }

        if (!T20_copyFrameFromRing(p,
                                   msg.frame_start_sample,
                                   p->frame_time,
                                   cfg_snap.cfg.feature.frame_size)) {
            continue;
        }

        float mfcc[G_T20_MFCC_COEFFS_MAX] = {0};
        float delta[G_T20_MFCC_COEFFS_MAX] = {0};
        float delta2[G_T20_MFCC_COEFFS_MAX] = {0};

        T20_computeMFCC(p, &cfg_snap, &pipe_snap, p->frame_time, mfcc);
        T20_pushMfccHistory(p, mfcc, cfg_snap.cfg.feature.mfcc_coeffs);
        T20_computeDeltaFromHistory(p,
                                    cfg_snap.cfg.feature.mfcc_coeffs,
                                    cfg_snap.cfg.feature.delta_window,
                                    delta);
        T20_computeDeltaDeltaFromHistory(p,
                                         cfg_snap.cfg.feature.mfcc_coeffs,
                                         delta2);

        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            p->latest_feature.log_mel_len = cfg_snap.cfg.feature.mel_filters;
            p->latest_feature.mfcc_len    = cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.delta_len   = cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.delta2_len  = cfg_snap.cfg.feature.mfcc_coeffs;
            p->latest_feature.vector_len  = cfg_snap.vector_len;

            memcpy(p->latest_feature.log_mel, p->log_mel, sizeof(float) * cfg_snap.cfg.feature.mel_filters);
            memcpy(p->latest_feature.mfcc,   mfcc,   sizeof(float) * cfg_snap.cfg.feature.mfcc_coeffs);
            memcpy(p->latest_feature.delta,  delta,  sizeof(float) * cfg_snap.cfg.feature.mfcc_coeffs);
            memcpy(p->latest_feature.delta2, delta2, sizeof(float) * cfg_snap.cfg.feature.mfcc_coeffs);

            T20_buildVector(mfcc,
                            delta,
                            delta2,
                            cfg_snap.cfg.feature.mfcc_coeffs,
                            p->latest_feature.vector);

            p->latest_vector_valid = true;
            T20_updateOutput(p);

            ST_T20_FeatureVector_t feature_copy = p->latest_feature;
            xSemaphoreGive(p->mutex);

            if (p->cfg.recorder.enable) {
                (void)T20_recorderEnqueueRawFrame(p, p->frame_time, cfg_snap.cfg.feature.frame_size);
                if (p->cfg.recorder.write_feature_vector_csv) {
                    (void)T20_recorderEnqueueFeature(p, &feature_copy);
                }
            }
        }
    }
}

// ============================================================================
// [Core Helpers]
// ============================================================================

bool T20_validateConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) return false;
    if (p_cfg->feature.frame_size != G_T20_FFT_SIZE) return false;
    if (p_cfg->feature.hop_size == 0 || p_cfg->feature.hop_size > p_cfg->feature.frame_size) return false;
    if (p_cfg->feature.sample_rate_hz <= 0.0f) return false;
    if (p_cfg->feature.mel_filters != G_T20_MEL_FILTERS) return false;

    if (p_cfg->feature.mfcc_coeffs == 0 ||
        p_cfg->feature.mfcc_coeffs > G_T20_MFCC_COEFFS_MAX) return false;
    if (p_cfg->feature.mfcc_coeffs > p_cfg->feature.mel_filters) return false;

    if (p_cfg->feature.delta_window == 0 ||
        p_cfg->feature.delta_window > (G_T20_MFCC_HISTORY / 2U)) return false;

    if (p_cfg->output.sequence_frames == 0 ||
        p_cfg->output.sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) return false;

    if (p_cfg->preprocess.pipeline.stage_count > G_T20_PREPROCESS_STAGE_MAX) return false;

    for (uint16_t i = 0; i < p_cfg->preprocess.pipeline.stage_count; ++i) {
        const ST_T20_PreprocessStageConfig_t& st = p_cfg->preprocess.pipeline.stages[i];
        if (!st.enable) continue;

        const float nyquist = p_cfg->feature.sample_rate_hz * 0.5f;

        switch (st.stage_type) {
            case EN_T20_STAGE_PREEMPHASIS:
                if (st.param_1 < 0.0f || st.param_1 > 1.0f) return false;
                break;
            case EN_T20_STAGE_NOISE_GATE:
                if (st.param_1 < 0.0f) return false;
                break;
            case EN_T20_STAGE_BIQUAD_LPF:
            case EN_T20_STAGE_BIQUAD_HPF:
                if (st.param_1 <= 0.0f || st.param_1 >= nyquist) return false;
                if (st.q_factor <= 0.0f) return false;
                break;
            case EN_T20_STAGE_BIQUAD_BPF:
                if (st.param_1 <= 0.0f || st.param_2 <= 0.0f) return false;
                if (st.param_2 <= st.param_1 || st.param_2 >= nyquist) return false;
                if (st.q_factor <= 0.0f) return false;
                break;
            default:
                break;
        }
    }

    if (p_cfg->button.debounce_ms == 0U) return false;
    if (p_cfg->button.long_press_ms < p_cfg->button.debounce_ms) return false;

    if (p_cfg->recorder.enable) {
        if (p_cfg->recorder.root_dir[0] == '\0') return false;
        if (p_cfg->recorder.session_prefix[0] == '\0') return false;
    }

    return true;
}

void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

    detachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1));

    if (p->sensor_task_handle != nullptr) {
        vTaskDelete(p->sensor_task_handle);
        p->sensor_task_handle = nullptr;
    }
    if (p->process_task_handle != nullptr) {
        vTaskDelete(p->process_task_handle);
        p->process_task_handle = nullptr;
    }

    if (p->recorder_task_handle != nullptr) {
        vTaskDelete(p->recorder_task_handle);
        p->recorder_task_handle = nullptr;
    }

    T20_recorderFlushIfNeeded(p, true);
    T20_recorderEnd(p);

    p->running = false;
}

void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;

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
    if (p == nullptr) return;

    p->initialized = false;
    p->running = false;
    p->session_state = EN_T20_SESSION_IDLE;
    p->measurement_start_ms = 0;
    p->measurement_stop_ms = 0;

    p->button_last_raw = false;
    p->button_stable_state = false;
    p->button_last_change_ms = 0;
    p->button_press_start_ms = 0;
    p->button_long_reported = false;

    // recorder File 핸들은 recorderEnd 이후 무효화된 상태로 간주
    memset(&p->recorder, 0, sizeof(p->recorder));

    p->sample_write_index = 0;
    p->total_samples_written = 0;
    p->next_frame_start_sample = 0;

    p->active_fill_buffer = 0;
    p->active_sample_index = 0;
    p->frame_accum_count = 0;
    p->dropped_frames = 0;
    p->mfcc_history_count = 0;
    p->noise_learned_frames = 0;
    p->recorder_state = EN_T20_RECORDER_IDLE;
    p->recorder_record_count = 0;
    p->recorder_last_timestamp_ms = 0;
    p->recorder_batch_pending = 0;
    p->recorder_flush_count = 0;
    p->recorder_flush_requested = false;
    p->recorder_finalize_requested = false;
    p->recorder_storage_backend = EN_T20_STORAGE_LITTLEFS;
    memset(p->recorder_file_path, 0, sizeof(p->recorder_file_path));
    p->latest_vector_valid = false;
    p->latest_sequence_valid = false;
    p->prev_raw_sample = 0.0f;
    p->recorder.recovery_retry_count = 0;
    p->recorder.recovery_fail_count = 0;
    p->recorder.last_recovery_ms = 0;
    memset(p->recorder.last_error_text, 0, sizeof(p->recorder.last_error_text));
    p->rotate_item_count = 0;
    p->web_push_seq = 0;
    p->web_last_live_change_ms = 0;
    p->web_last_status_change_ms = 0;
    p->web_last_force_push_ms = 0;
    p->web_last_status_hash = 0;

    memset(p->sample_ring, 0, sizeof(p->sample_ring));
    memset(p->frame_buffer, 0, sizeof(p->frame_buffer));
    memset(p->frame_accum_buffer, 0, sizeof(p->frame_accum_buffer));
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
    memset(p->rotate_items, 0, sizeof(p->rotate_items));
    memset(p->dsp_cache, 0, sizeof(p->dsp_cache));
    memset(p->twiddle_cache, 0, sizeof(p->twiddle_cache));
    memset(p->profiles, 0, sizeof(p->profiles));
    memset(p->recorder_index_items, 0, sizeof(p->recorder_index_items));
    p->recorder_index_count = 0;
    memset(p->viewer_last_vector, 0, sizeof(p->viewer_last_vector));
    p->viewer_last_vector_len = 0;
    p->viewer_last_frame_id = 0;
    memset(p->viewer_last_log_mel, 0, sizeof(p->viewer_last_log_mel));
    p->viewer_last_log_mel_len = 0;
    memset(p->viewer_last_mfcc, 0, sizeof(p->viewer_last_mfcc));
    p->viewer_last_mfcc_len = 0;
    memset(p->viewer_last_waveform, 0, sizeof(p->viewer_last_waveform));
    p->viewer_last_waveform_len = 0;
    memset(p->viewer_last_spectrum, 0, sizeof(p->viewer_last_spectrum));
    p->viewer_last_spectrum_len = 0;
    memset(p->viewer_recent_waveforms, 0, sizeof(p->viewer_recent_waveforms));
    p->viewer_recent_waveform_count = 0;
    p->viewer_recent_waveform_head = 0;
    p->viewer_effective_hop_size = 0;
    memset(p->viewer_events, 0, sizeof(p->viewer_events));
    p->viewer_event_count = 0;
    p->current_profile_index = G_T20_CFG_PROFILE_INDEX_DEFAULT;
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));
}

void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    T20_stopTasks(p);
    T20_recorderEnd(p);
    T20_releaseSyncObjects(p);
    T20_clearRuntimeState(p);
}

float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p)
{
    switch (p->cfg.preprocess.axis) {
        case EN_T20_AXIS_X: return p->imu.data.accelX;
        case EN_T20_AXIS_Y: return p->imu.data.accelY;
        case EN_T20_AXIS_Z:
        default:            return p->imu.data.accelZ;
    }
}

bool T20_tryEnqueueReadyFrames(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    if (p->session_state != EN_T20_SESSION_RECORDING) return true;

    bool any = false;
    const uint32_t frame_size = p->cfg.feature.frame_size;
    const uint32_t hop_size   = p->cfg.feature.hop_size;

    while (p->total_samples_written >= (p->next_frame_start_sample + frame_size)) {
        ST_T20_FrameMessage_t msg;
        msg.frame_start_sample = p->next_frame_start_sample;

        if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
            ST_T20_FrameMessage_t old_msg;
            if (xQueueReceive(p->frame_queue, &old_msg, 0) == pdTRUE) {
                p->dropped_frames++;
                if (xQueueSend(p->frame_queue, &msg, 0) != pdTRUE) {
                    p->dropped_frames++;
                    return false;
                }
            } else {
                p->dropped_frames++;
                return false;
            }
        }

        p->next_frame_start_sample += hop_size;
        any = true;
    }

    return any;
}

bool T20_copyFrameFromRing(CL_T20_Mfcc::ST_Impl* p,
                           uint32_t p_frame_start_sample,
                           float* p_out_frame,
                           uint16_t p_frame_size)
{
    if (p == nullptr || p_out_frame == nullptr) {
        return false;
    }

    uint32_t latest_written = p->total_samples_written;
    if (latest_written < (uint32_t)(p_frame_start_sample + p_frame_size)) {
        return false;
    }

    if ((latest_written - p_frame_start_sample) > G_T20_SAMPLE_RING_SIZE) {
        return false;
    }

    const uint32_t oldest_kept = (latest_written > G_T20_SAMPLE_RING_SIZE)
                               ? (latest_written - G_T20_SAMPLE_RING_SIZE)
                               : 0U;
    if (p_frame_start_sample < oldest_kept) {
        return false;
    }

    for (uint16_t i = 0; i < p_frame_size; ++i) {
        uint32_t abs_idx = p_frame_start_sample + i;
        uint32_t ring_idx = abs_idx % G_T20_SAMPLE_RING_SIZE;
        p_out_frame[i] = p->sample_ring[ring_idx];
    }

    return true;
}

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim)
{
    if (p == nullptr || p_mfcc == nullptr) return;

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * p_dim);
        p->mfcc_history_count++;
    } else {
        for (uint16_t i = 0; i < (G_T20_MFCC_HISTORY - 1U); ++i) {
            memcpy(p->mfcc_history[i], p->mfcc_history[i + 1U], sizeof(float) * p_dim);
        }
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1U], p_mfcc, sizeof(float) * p_dim);
    }
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                 uint16_t p_dim,
                                 uint16_t p_delta_window,
                                 float* p_delta_out)
{
    if (p == nullptr || p_delta_out == nullptr) return;
    memset(p_delta_out, 0, sizeof(float) * p_dim);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int center = G_T20_MFCC_HISTORY / 2;
    const int N = (int)p_delta_window;

    float den = 0.0f;
    for (int n = 1; n <= N; ++n) {
        den += (float)(n * n);
    }
    den *= 2.0f;

    for (uint16_t c = 0; c < p_dim; ++c) {
        float num = 0.0f;
        for (int n = 1; n <= N; ++n) {
            float plus  = p->mfcc_history[center + n][c];
            float minus = p->mfcc_history[center - n][c];
            num += (float)n * (plus - minus);
        }
        p_delta_out[c] = num / (den + G_T20_EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                      uint16_t p_dim,
                                      float* p_delta2_out)
{
    if (p == nullptr || p_delta2_out == nullptr) return;
    memset(p_delta2_out, 0, sizeof(float) * p_dim);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int center = G_T20_MFCC_HISTORY / 2;
    for (uint16_t c = 0; c < p_dim; ++c) {
        float prev = p->mfcc_history[center - 1][c];
        float curr = p->mfcc_history[center][c];
        float next = p->mfcc_history[center + 1][c];
        p_delta2_out[c] = next - (2.0f * curr) + prev;
    }
}

void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec)
{
    if (p_mfcc == nullptr || p_delta == nullptr || p_delta2 == nullptr || p_out_vec == nullptr) return;

    uint16_t idx = 0;
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_mfcc[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta[i];
    for (uint16_t i = 0; i < p_dim; ++i) p_out_vec[idx++] = p_delta2[i];
}

void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb,
                 uint16_t p_frames,
                 uint16_t p_feature_dim)
{
    if (p_rb == nullptr) return;
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->feature_dim = p_feature_dim;
    p_rb->head = 0;
    p_rb->full = false;
}

void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec)
{
    if (p_rb == nullptr || p_feature_vec == nullptr || p_rb->feature_dim == 0) return;

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

    uint16_t frames = p_rb->frames;
    uint16_t start = p_rb->full ? p_rb->head : 0;
    uint16_t written = 0;

    for (uint16_t i = 0; i < frames; ++i) {
        uint16_t idx = (uint16_t)((start + i) % frames);
        memcpy(&p_out_flat[written * p_rb->feature_dim],
               p_rb->data[idx],
               sizeof(float) * p_rb->feature_dim);
        written++;
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

    if (p->latest_feature.vector_len == 0 ||
        p->latest_feature.vector_len != p->seq_rb.feature_dim) {
        p->latest_sequence_valid = false;
        return;
    }

    T20_seqPush(&p->seq_rb, p->latest_feature.vector);

    if (p->cfg.output.sequence_flatten) {
        T20_seqExportFlatten(&p->seq_rb, p->latest_sequence_flat);
    }

    p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}


bool T20_applyConfigJson(CL_T20_Mfcc::ST_Impl* p, const char* p_json)
{
    if (p == nullptr || p_json == nullptr) {
        return false;
    }

    // 안정성 우선 정책:
    // - 측정 중에는 설정 변경을 차단한다.
    if (p->running) {
        return false;
    }

    ST_T20_Config_t v_cfg = p->cfg;

    auto find_uint = [p_json](const char* key, uint16_t& out_val) -> bool {
        const char* pos = strstr(p_json, key);
        if (pos == nullptr) return false;
        pos = strchr(pos, ':');
        if (pos == nullptr) return false;
        ++pos;
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == '"') ++pos;
        unsigned long v = strtoul(pos, nullptr, 10);
        out_val = (uint16_t)v;
        return true;
    };

    auto find_float = [p_json](const char* key, float& out_val) -> bool {
        const char* pos = strstr(p_json, key);
        if (pos == nullptr) return false;
        pos = strchr(pos, ':');
        if (pos == nullptr) return false;
        ++pos;
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == '"') ++pos;
        out_val = strtof(pos, nullptr);
        return true;
    };

    auto find_bool = [p_json](const char* key, bool& out_val) -> bool {
        const char* pos = strstr(p_json, key);
        if (pos == nullptr) return false;
        pos = strchr(pos, ':');
        if (pos == nullptr) return false;
        ++pos;
        while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r' || *pos == '"') ++pos;
        if (strncmp(pos, "true", 4) == 0 || strncmp(pos, "1", 1) == 0) {
            out_val = true;
            return true;
        }
        if (strncmp(pos, "false", 5) == 0 || strncmp(pos, "0", 1) == 0) {
            out_val = false;
            return true;
        }
        return false;
    };

    // 부분 파싱: 현재 단계에서는 안정적으로 필요한 항목만 수용
    find_uint("frame_size", v_cfg.feature.frame_size);
    find_uint("hop_size", v_cfg.feature.hop_size);
    find_float("sample_rate_hz", v_cfg.feature.sample_rate_hz);
    find_uint("mel_filters", v_cfg.feature.mel_filters);
    find_uint("mfcc_coeffs", v_cfg.feature.mfcc_coeffs);
    find_uint("delta_window", v_cfg.feature.delta_window);
    find_uint("sequence_frames", v_cfg.output.sequence_frames);
    find_bool("sequence_flatten", v_cfg.output.sequence_flatten);
    find_bool("remove_dc", v_cfg.preprocess.remove_dc);
    find_bool("preemphasis_enable", v_cfg.preprocess.preemphasis.enable);
    find_float("preemphasis_alpha", v_cfg.preprocess.preemphasis.alpha);
    find_bool("filter_enable", v_cfg.preprocess.filter.enable);
    find_float("cutoff_hz_1", v_cfg.preprocess.filter.cutoff_hz_1);
    find_float("cutoff_hz_2", v_cfg.preprocess.filter.cutoff_hz_2);
    find_float("q_factor", v_cfg.preprocess.filter.q_factor);
    find_bool("noise_gate_enable", v_cfg.preprocess.noise.enable_gate);
    find_float("gate_threshold_abs", v_cfg.preprocess.noise.gate_threshold_abs);
    find_bool("spectral_subtract_enable", v_cfg.preprocess.noise.enable_spectral_subtract);
    find_float("spectral_subtract_strength", v_cfg.preprocess.noise.spectral_subtract_strength);
    find_uint("noise_learn_frames", v_cfg.preprocess.noise.noise_learn_frames);

    return g_t20_instance != nullptr ? g_t20_instance->setConfig(&v_cfg) : false;
}



bool T20_parseOutputMode(const char* p_json, EM_T20_OutputMode_t* p_out_mode)
{
    if (p_json == nullptr || p_out_mode == nullptr) {
        return false;
    }

    const char* pos = strstr(p_json, "output_mode");
    if (pos == nullptr) {
        return false;
    }

    if (strstr(pos, "sequence") != nullptr) {
        *p_out_mode = EN_T20_OUTPUT_SEQUENCE;
        return true;
    }

    if (strstr(pos, "vector") != nullptr) {
        *p_out_mode = EN_T20_OUTPUT_VECTOR;
        return true;
    }

    return false;
}

bool T20_parseFilterType(const char* p_json, EM_T20_FilterType_t* p_out_type)
{
    if (p_json == nullptr || p_out_type == nullptr) {
        return false;
    }

    const char* pos = strstr(p_json, "filter_type");
    if (pos == nullptr) {
        return false;
    }

    if (strstr(pos, "lpf") != nullptr) {
        *p_out_type = EN_T20_FILTER_LPF;
        return true;
    }
    if (strstr(pos, "hpf") != nullptr) {
        *p_out_type = EN_T20_FILTER_HPF;
        return true;
    }
    if (strstr(pos, "bpf") != nullptr) {
        *p_out_type = EN_T20_FILTER_BPF;
        return true;
    }
    if (strstr(pos, "off") != nullptr) {
        *p_out_type = EN_T20_FILTER_OFF;
        return true;
    }

    // 숫자형도 허용
    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    ++pos;
    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"') ++pos;
    unsigned long v = strtoul(pos, nullptr, 10);
    if (v <= (unsigned long)EN_T20_FILTER_BPF) {
        *p_out_type = (EM_T20_FilterType_t)v;
        return true;
    }

    return false;
}



void T20_rotateListPush(CL_T20_Mfcc::ST_Impl* p,
                        const char* p_raw,
                        const char* p_meta,
                        const char* p_event,
                        const char* p_feature,
                        uint32_t p_rotate_id)
{
    if (p == nullptr) {
        return;
    }

    if (p->rotate_item_count < G_T20_RECORDER_MAX_ROTATE_LIST) {
        for (int i = (int)p->rotate_item_count; i > 0; --i) {
            p->rotate_items[i] = p->rotate_items[i - 1];
        }
        p->rotate_item_count++;
    } else {
        for (int i = G_T20_RECORDER_MAX_ROTATE_LIST - 1; i > 0; --i) {
            p->rotate_items[i] = p->rotate_items[i - 1];
        }
    }

    ST_T20_RotateItem_t& v_item = p->rotate_items[0];
    memset(&v_item, 0, sizeof(v_item));
    if (p_raw != nullptr)     strncpy(v_item.raw_path, p_raw, sizeof(v_item.raw_path) - 1);
    if (p_meta != nullptr)    strncpy(v_item.meta_path, p_meta, sizeof(v_item.meta_path) - 1);
    if (p_event != nullptr)   strncpy(v_item.event_path, p_event, sizeof(v_item.event_path) - 1);
    if (p_feature != nullptr) strncpy(v_item.feature_path, p_feature, sizeof(v_item.feature_path) - 1);
    v_item.rotate_id = p_rotate_id;
    v_item.valid = true;
}

bool T20_rotateListDeleteFile(CL_T20_Mfcc::ST_Impl* p, const char* p_path)
{
    (void)p;
    if (p_path == nullptr || p_path[0] == '\0') {
        return false;
    }
#if __has_include(<SD_MMC.h>)
    return SD_MMC.remove(p_path);
#else
    return false;
#endif
}

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return 0;
    }

    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) {
        h ^= v;
        h *= 16777619u;
    };

    mix((uint32_t)p->initialized);
    mix((uint32_t)p->running);
    mix((uint32_t)p->measurement_active);
    mix((uint32_t)p->session_state);
    mix((uint32_t)p->dropped_frames);
    mix((uint32_t)p->recorder.queued_record_count);
    mix((uint32_t)p->recorder.dropped_record_count);
    mix((uint32_t)p->recorder.written_record_count);
    mix((uint32_t)p->latest_vector_valid);
    mix((uint32_t)p->latest_sequence_valid);
    mix((uint32_t)p->seq_rb.full);
    mix((uint32_t)p->seq_rb.frames);
    mix((uint32_t)p->seq_rb.feature_dim);

    return h;
}


void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p, uint16_t p_keep_count)
{
    if (p == nullptr) {
        return;
    }

    if (p_keep_count > G_T20_RECORDER_MAX_ROTATE_LIST) {
        p_keep_count = G_T20_RECORDER_MAX_ROTATE_LIST;
    }

    while (p->rotate_item_count > p_keep_count) {
        ST_T20_RotateItem_t& v_item = p->rotate_items[p->rotate_item_count - 1];
        if (v_item.valid) {
            T20_rotateListDeleteFile(p, v_item.raw_path);
            T20_rotateListDeleteFile(p, v_item.meta_path);
            T20_rotateListDeleteFile(p, v_item.event_path);
            T20_rotateListDeleteFile(p, v_item.feature_path);
        }
        memset(&v_item, 0, sizeof(v_item));
        p->rotate_item_count--;
    }
}

void T20_recorderSetLastError(CL_T20_Mfcc::ST_Impl* p, const char* p_text)
{
    if (p == nullptr) {
        return;
    }

    memset(p->recorder.last_error_text, 0, sizeof(p->recorder.last_error_text));
    if (p_text != nullptr) {
        strncpy(p->recorder.last_error_text, p_text, sizeof(p->recorder.last_error_text) - 1);
    }
}


bool T20_buildConfigJsonText(const ST_T20_Config_t* p_cfg, char* p_out_buf, uint16_t p_len)
{
    if (p_cfg == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    int n = snprintf(
        p_out_buf,
        p_len,
        "{"
        "\"feature\":{\"fft_size\":%u,\"sample_rate_hz\":%.3f,\"mel_filters\":%u,\"mfcc_coeffs\":%u,\"delta_window\":%u},"
        "\"output\":{\"output_mode\":\"%s\",\"sequence_frames\":%u,\"sequence_flatten\":%s},"
        "\"preprocess\":{\"axis\":\"%s\",\"remove_dc\":%s,"
        "\"preemphasis\":{\"enable\":%s,\"alpha\":%.3f},"
        "\"filter\":{\"enable\":%s,\"type\":%d,\"cutoff_hz_1\":%.3f,\"cutoff_hz_2\":%.3f,\"q_factor\":%.3f},"
        "\"noise\":{\"enable_gate\":%s,\"gate_threshold_abs\":%.6f,\"enable_spectral_subtract\":%s,"
        "\"spectral_subtract_strength\":%.3f,\"noise_learn_frames\":%u}}"
        "}",
        p_cfg->feature.fft_size,
        p_cfg->feature.sample_rate_hz,
        p_cfg->feature.mel_filters,
        p_cfg->feature.mfcc_coeffs,
        p_cfg->feature.delta_window,
        (p_cfg->output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence",
        p_cfg->output.sequence_frames,
        p_cfg->output.sequence_flatten ? "true" : "false",
        (p_cfg->preprocess.axis == EN_T20_AXIS_X) ? "x" :
        (p_cfg->preprocess.axis == EN_T20_AXIS_Y) ? "y" : "z",
        p_cfg->preprocess.remove_dc ? "true" : "false",
        p_cfg->preprocess.preemphasis.enable ? "true" : "false",
        p_cfg->preprocess.preemphasis.alpha,
        p_cfg->preprocess.filter.enable ? "true" : "false",
        (int)p_cfg->preprocess.filter.type,
        p_cfg->preprocess.filter.cutoff_hz_1,
        p_cfg->preprocess.filter.cutoff_hz_2,
        p_cfg->preprocess.filter.q_factor,
        p_cfg->preprocess.noise.enable_gate ? "true" : "false",
        p_cfg->preprocess.noise.gate_threshold_abs,
        p_cfg->preprocess.noise.enable_spectral_subtract ? "true" : "false",
        p_cfg->preprocess.noise.spectral_subtract_strength,
        p_cfg->preprocess.noise.noise_learn_frames
    );

    return (n > 0 && n < p_len);
}

static const char* T20_findJsonKeyPos(const char* p_json, const char* p_key)
{
    if (p_json == nullptr || p_key == nullptr) {
        return nullptr;
    }

    char v_key_pat[96] = {0};
    snprintf(v_key_pat, sizeof(v_key_pat), "\"%s\"", p_key);
    return strstr(p_json, v_key_pat);
}

bool T20_jsonFindFloat(const char* p_json, const char* p_key, float* p_out)
{
    const char* pos = T20_findJsonKeyPos(p_json, p_key);
    if (pos == nullptr || p_out == nullptr) {
        return false;
    }
    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    pos++;
    *p_out = (float)atof(pos);
    return true;
}

bool T20_jsonFindInt(const char* p_json, const char* p_key, int32_t* p_out)
{
    const char* pos = T20_findJsonKeyPos(p_json, p_key);
    if (pos == nullptr || p_out == nullptr) {
        return false;
    }
    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    pos++;
    *p_out = (int32_t)atoi(pos);
    return true;
}

bool T20_jsonFindBool(const char* p_json, const char* p_key, bool* p_out)
{
    const char* pos = T20_findJsonKeyPos(p_json, p_key);
    if (pos == nullptr || p_out == nullptr) {
        return false;
    }
    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (strncmp(pos, "true", 4) == 0) {
        *p_out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *p_out = false;
        return true;
    }
    return false;
}

bool T20_jsonFindString(const char* p_json, const char* p_key, char* p_out, uint16_t p_out_len)
{
    const char* pos = T20_findJsonKeyPos(p_json, p_key);
    if (pos == nullptr || p_out == nullptr || p_out_len == 0) {
        return false;
    }
    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '\"') {
        return false;
    }
    pos++;

    uint16_t i = 0;
    while (*pos != '\0' && *pos != '\"' && i + 1 < p_out_len) {
        p_out[i++] = *pos++;
    }
    p_out[i] = '\0';
    return (i > 0);
}

bool T20_parseConfigJsonText(const char* p_json_text, ST_T20_Config_t* p_cfg_out)
{
    if (p_json_text == nullptr || p_cfg_out == nullptr) {
        return false;
    }

    ST_T20_Config_t v_cfg = T20_makeDefaultConfig();
    int32_t v_i32 = 0;
    float v_f32 = 0.0f;
    bool v_b = false;
    char v_str[32] = {0};

    if (T20_jsonFindIntInSection(p_json_text, "feature", "fft_size", &v_i32)) v_cfg.feature.fft_size = (uint16_t)v_i32;
    if (T20_jsonFindFloatInSection(p_json_text, "feature", "sample_rate_hz", &v_f32)) v_cfg.feature.sample_rate_hz = v_f32;
    if (T20_jsonFindIntInSection(p_json_text, "feature", "mel_filters", &v_i32)) v_cfg.feature.mel_filters = (uint16_t)v_i32;
    if (T20_jsonFindIntInSection(p_json_text, "feature", "mfcc_coeffs", &v_i32)) v_cfg.feature.mfcc_coeffs = (uint16_t)v_i32;
    if (T20_jsonFindIntInSection(p_json_text, "feature", "delta_window", &v_i32)) v_cfg.feature.delta_window = (uint16_t)v_i32;

    if (T20_jsonFindStringInSection(p_json_text, "output", "output_mode", v_str, sizeof(v_str))) {
        v_cfg.output.output_mode = (strcmp(v_str, "sequence") == 0) ? EN_T20_OUTPUT_SEQUENCE : EN_T20_OUTPUT_VECTOR;
    }
    if (T20_jsonFindIntInSection(p_json_text, "output", "sequence_frames", &v_i32)) v_cfg.output.sequence_frames = (uint16_t)v_i32;
    if (T20_jsonFindBoolInSection(p_json_text, "output", "sequence_flatten", &v_b)) v_cfg.output.sequence_flatten = v_b;

    if (T20_jsonFindStringInSection(p_json_text, "preprocess", "axis", v_str, sizeof(v_str))) {
        if (strcmp(v_str, "x") == 0) v_cfg.preprocess.axis = EN_T20_AXIS_X;
        else if (strcmp(v_str, "y") == 0) v_cfg.preprocess.axis = EN_T20_AXIS_Y;
        else v_cfg.preprocess.axis = EN_T20_AXIS_Z;
    }
    if (T20_jsonFindBoolInSection(p_json_text, "preprocess", "remove_dc", &v_b)) v_cfg.preprocess.remove_dc = v_b;

    if (T20_jsonFindBoolInSection(p_json_text, "preemphasis", "enable", &v_b)) v_cfg.preprocess.preemphasis.enable = v_b;
    if (T20_jsonFindFloatInSection(p_json_text, "preemphasis", "alpha", &v_f32)) v_cfg.preprocess.preemphasis.alpha = v_f32;

    if (T20_jsonFindBoolInSection(p_json_text, "noise", "enable_gate", &v_b)) v_cfg.preprocess.noise.enable_gate = v_b;
    if (T20_jsonFindBoolInSection(p_json_text, "noise", "enable_spectral_subtract", &v_b)) v_cfg.preprocess.noise.enable_spectral_subtract = v_b;
    if (T20_jsonFindFloatInSection(p_json_text, "noise", "gate_threshold_abs", &v_f32)) v_cfg.preprocess.noise.gate_threshold_abs = v_f32;
    if (T20_jsonFindFloatInSection(p_json_text, "noise", "spectral_subtract_strength", &v_f32)) v_cfg.preprocess.noise.spectral_subtract_strength = v_f32;
    if (T20_jsonFindIntInSection(p_json_text, "noise", "noise_learn_frames", &v_i32)) v_cfg.preprocess.noise.noise_learn_frames = (uint16_t)v_i32;

    if (T20_jsonFindBoolInSection(p_json_text, "filter", "enable", &v_b)) v_cfg.preprocess.filter.enable = v_b;
    if (T20_jsonFindIntInSection(p_json_text, "filter", "type", &v_i32)) v_cfg.preprocess.filter.type = (EM_T20_FilterType_t)v_i32;
    if (T20_jsonFindFloatInSection(p_json_text, "filter", "cutoff_hz_1", &v_f32)) v_cfg.preprocess.filter.cutoff_hz_1 = v_f32;
    if (T20_jsonFindFloatInSection(p_json_text, "filter", "cutoff_hz_2", &v_f32)) v_cfg.preprocess.filter.cutoff_hz_2 = v_f32;
    if (T20_jsonFindFloatInSection(p_json_text, "filter", "q_factor", &v_f32)) v_cfg.preprocess.filter.q_factor = v_f32;

    if (!T20_validateConfig(&v_cfg)) {
        return false;
    }

    *p_cfg_out = v_cfg;
    return true;
}

bool T20_saveRuntimeConfigToLittleFs(const ST_T20_Config_t* p_cfg)
{
#if T20_HAS_LITTLEFS
    if (p_cfg == nullptr) {
        return false;
    }

    char v_json[G_T20_CFG_JSON_BUFFER_SIZE] = {0};
    if (!T20_buildConfigJsonText(p_cfg, v_json, sizeof(v_json))) {
        return false;
    }

    File v_f = LittleFS.open(G_T20_CFG_RUNTIME_PATH, "w");
    if (!v_f) {
        return false;
    }
    v_f.print(v_json);
    v_f.close();
    return true;
#else
    (void)p_cfg;
    return false;
#endif
}

bool T20_loadRuntimeConfigFromLittleFs(ST_T20_Config_t* p_cfg_out)
{
#if T20_HAS_LITTLEFS
    if (p_cfg_out == nullptr) {
        return false;
    }

    if (!LittleFS.exists(G_T20_CFG_RUNTIME_PATH)) {
        return false;
    }

    File v_f = LittleFS.open(G_T20_CFG_RUNTIME_PATH, "r");
    if (!v_f) {
        return false;
    }

    char v_json[G_T20_CFG_JSON_BUFFER_SIZE] = {0};
    size_t v_n = v_f.readBytes(v_json, sizeof(v_json) - 1);
    v_f.close();
    v_json[v_n] = '\0';

    return T20_parseConfigJsonText(v_json, p_cfg_out);
#else
    (void)p_cfg_out;
    return false;
#endif
}


bool T20_buildProfilePath(uint8_t p_profile_index, char* p_out_path, uint16_t p_out_len)
{
    if (p_out_path == nullptr || p_out_len == 0) {
        return false;
    }

    if (p_profile_index >= G_T20_CFG_PROFILE_COUNT) {
        return false;
    }

    int n = snprintf(p_out_path, p_out_len, "%s%u%s",
                     G_T20_CFG_PROFILE_BASE_PATH,
                     (unsigned)p_profile_index,
                     G_T20_CFG_PROFILE_SUFFIX);
    return (n > 0 && n < p_out_len);
}

void T20_initProfiles(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < G_T20_CFG_PROFILE_COUNT; ++i) {
        p->profiles[i].valid = T20_buildProfilePath(i, p->profiles[i].path, sizeof(p->profiles[i].path));
        snprintf(p->profiles[i].name, sizeof(p->profiles[i].name), "profile_%u", (unsigned)i);
    }
}

bool T20_saveProfileToLittleFs(uint8_t p_profile_index, const ST_T20_Config_t* p_cfg)
{
#if T20_HAS_LITTLEFS
    if (p_cfg == nullptr) {
        return false;
    }

    char v_path[48] = {0};
    if (!T20_buildProfilePath(p_profile_index, v_path, sizeof(v_path))) {
        return false;
    }

    char v_json[G_T20_CFG_JSON_BUFFER_SIZE] = {0};
    if (!T20_buildConfigJsonText(p_cfg, v_json, sizeof(v_json))) {
        return false;
    }

    File v_f = LittleFS.open(v_path, "w");
    if (!v_f) {
        return false;
    }
    v_f.print(v_json);
    v_f.close();
    return true;
#else
    (void)p_profile_index;
    (void)p_cfg;
    return false;
#endif
}

bool T20_loadProfileFromLittleFs(uint8_t p_profile_index, ST_T20_Config_t* p_cfg_out)
{
#if T20_HAS_LITTLEFS
    if (p_cfg_out == nullptr) {
        return false;
    }

    char v_path[48] = {0};
    if (!T20_buildProfilePath(p_profile_index, v_path, sizeof(v_path))) {
        return false;
    }

    if (!LittleFS.exists(v_path)) {
        return false;
    }

    File v_f = LittleFS.open(v_path, "r");
    if (!v_f) {
        return false;
    }

    char v_json[G_T20_CFG_JSON_BUFFER_SIZE] = {0};
    size_t v_n = v_f.readBytes(v_json, sizeof(v_json) - 1);
    v_f.close();
    v_json[v_n] = '\0';

    return T20_parseConfigJsonText(v_json, p_cfg_out);
#else
    (void)p_profile_index;
    (void)p_cfg_out;
    return false;
#endif
}

bool T20_jsonFindSection(const char* p_json, const char* p_key, ST_T20_JsonSection_t* p_out)
{
    if (p_json == nullptr || p_key == nullptr || p_out == nullptr) {
        return false;
    }

    char v_pat[64] = {0};
    snprintf(v_pat, sizeof(v_pat), "\"%s\"", p_key);
    const char* v_pos = strstr(p_json, v_pat);
    if (v_pos == nullptr) {
        return false;
    }

    v_pos = strchr(v_pos, '{');
    if (v_pos == nullptr) {
        return false;
    }

    int v_depth = 0;
    const char* v_scan = v_pos;
    while (*v_scan != '\0') {
        if (*v_scan == '{') v_depth++;
        else if (*v_scan == '}') {
            v_depth--;
            if (v_depth == 0) {
                p_out->found = true;
                p_out->start = (uint16_t)(v_pos - p_json);
                p_out->end   = (uint16_t)(v_scan - p_json);
                return true;
            }
        }
        v_scan++;
    }

    return false;
}


bool T20_jsonFindBoolInSection(const char* p_json, const char* p_section, const char* p_key, bool* p_out)
{
    ST_T20_JsonSection_t v_sec = {0};
    if (!T20_jsonFindSection(p_json, p_section, &v_sec)) {
        return false;
    }

    char v_sub[G_T20_JSON_SECTION_BUF_SIZE] = {0};
    uint16_t v_len = (uint16_t)(v_sec.end - v_sec.start + 1);
    if (v_len >= sizeof(v_sub)) v_len = sizeof(v_sub) - 1;
    memcpy(v_sub, &p_json[v_sec.start], v_len);
    v_sub[v_len] = '\0';
    return T20_jsonFindBool(v_sub, p_key, p_out);
}

bool T20_jsonFindIntInSection(const char* p_json, const char* p_section, const char* p_key, int32_t* p_out)
{
    ST_T20_JsonSection_t v_sec = {0};
    if (!T20_jsonFindSection(p_json, p_section, &v_sec)) {
        return false;
    }

    char v_sub[G_T20_JSON_SECTION_BUF_SIZE] = {0};
    uint16_t v_len = (uint16_t)(v_sec.end - v_sec.start + 1);
    if (v_len >= sizeof(v_sub)) v_len = sizeof(v_sub) - 1;
    memcpy(v_sub, &p_json[v_sec.start], v_len);
    v_sub[v_len] = '\0';
    return T20_jsonFindInt(v_sub, p_key, p_out);
}

bool T20_jsonFindFloatInSection(const char* p_json, const char* p_section, const char* p_key, float* p_out)
{
    ST_T20_JsonSection_t v_sec = {0};
    if (!T20_jsonFindSection(p_json, p_section, &v_sec)) {
        return false;
    }

    char v_sub[G_T20_JSON_SECTION_BUF_SIZE] = {0};
    uint16_t v_len = (uint16_t)(v_sec.end - v_sec.start + 1);
    if (v_len >= sizeof(v_sub)) v_len = sizeof(v_sub) - 1;
    memcpy(v_sub, &p_json[v_sec.start], v_len);
    v_sub[v_len] = '\0';
    return T20_jsonFindFloat(v_sub, p_key, p_out);
}

bool T20_jsonFindStringInSection(const char* p_json, const char* p_section, const char* p_key, char* p_out, uint16_t p_len)
{
    ST_T20_JsonSection_t v_sec = {0};
    if (!T20_jsonFindSection(p_json, p_section, &v_sec)) {
        return false;
    }

    char v_sub[G_T20_JSON_SECTION_BUF_SIZE] = {0};
    uint16_t v_len = (uint16_t)(v_sec.end - v_sec.start + 1);
    if (v_len >= sizeof(v_sub)) v_len = sizeof(v_sub) - 1;
    memcpy(v_sub, &p_json[v_sec.start], v_len);
    v_sub[v_len] = '\0';
    return T20_jsonFindString(v_sub, p_key, p_out, p_len);
}

bool T20_buildConfigSchemaJsonText(char* p_out_buf, uint16_t p_len)
{
    if (p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    int n = snprintf(
        p_out_buf, p_len,
        "{"
        "\"version\":\"%s\","
        "\"feature\":{\"fft_size\":%u,\"mel_filters\":%u,\"mfcc_coeffs_default\":%u,\"mfcc_coeffs_max\":%u},"
        "\"output\":{\"sequence_frames_default\":%u,\"sequence_frames_max\":%u},"
        "\"preemphasis\":{\"alpha_min\":0.0,\"alpha_max\":1.0},"
        "\"filter\":{\"types\":[0,1,2,3]},"
        "\"notes\":\"TODO: 다음 단계에서 정식 JSON schema validator 연동\""
        "}",
        G_T20_VERSION_STR,
        (unsigned)G_T20_FFT_SIZE,
        (unsigned)G_T20_MEL_FILTERS,
        (unsigned)G_T20_MFCC_COEFFS_DEFAULT,
        (unsigned)G_T20_MFCC_COEFFS_MAX,
        (unsigned)G_T20_SEQUENCE_FRAMES_DEFAULT,
        (unsigned)G_T20_SEQUENCE_FRAMES_MAX
    );

    return (n > 0 && n < p_len);
}


bool T20_loadRecorderIndex(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    memset(p->recorder_index_items, 0, sizeof(p->recorder_index_items));
    p->recorder_index_count = 0;
    memset(p->viewer_last_vector, 0, sizeof(p->viewer_last_vector));
    p->viewer_last_vector_len = 0;
    p->viewer_last_frame_id = 0;
    memset(p->viewer_last_log_mel, 0, sizeof(p->viewer_last_log_mel));
    p->viewer_last_log_mel_len = 0;
    memset(p->viewer_last_mfcc, 0, sizeof(p->viewer_last_mfcc));
    p->viewer_last_mfcc_len = 0;
    memset(p->viewer_last_waveform, 0, sizeof(p->viewer_last_waveform));
    p->viewer_last_waveform_len = 0;
    memset(p->viewer_last_spectrum, 0, sizeof(p->viewer_last_spectrum));
    p->viewer_last_spectrum_len = 0;
    memset(p->viewer_recent_waveforms, 0, sizeof(p->viewer_recent_waveforms));
    p->viewer_recent_waveform_count = 0;
    p->viewer_recent_waveform_head = 0;
    p->viewer_effective_hop_size = 0;
    memset(p->viewer_events, 0, sizeof(p->viewer_events));
    p->viewer_event_count = 0;

    if (!LittleFS.exists(G_T20_RECORDER_INDEX_PATH)) {
        return true;
    }

    File v_f = LittleFS.open(G_T20_RECORDER_INDEX_PATH, "r");
    if (!v_f) {
        return false;
    }

    String v_text = v_f.readString();
    v_f.close();

    int v_pos = 0;
    while (v_pos < (int)v_text.length() && p->recorder_index_count < G_T20_RECORDER_MAX_LIST_ITEMS) {
        int v_path = v_text.indexOf("\"path\":\"", v_pos);
        if (v_path < 0) {
            break;
        }
        v_path += 8;
        int v_path_end = v_text.indexOf("\"", v_path);
        if (v_path_end < 0) {
            break;
        }

        int v_sum = v_text.indexOf("\"summary_path\":\"", v_path_end);
        int v_sum_start = (v_sum >= 0) ? v_sum + 16 : -1;
        int v_sum_end = (v_sum_start >= 0) ? v_text.indexOf("\"", v_sum_start) : -1;

        ST_T20_RecorderIndexItem_t* p_item = &p->recorder_index_items[p->recorder_index_count];
        String v_p = v_text.substring(v_path, v_path_end);
        strlcpy(p_item->path, v_p.c_str(), sizeof(p_item->path));

        if (v_sum_start >= 0 && v_sum_end > v_sum_start) {
            String v_s = v_text.substring(v_sum_start, v_sum_end);
            strlcpy(p_item->summary_path, v_s.c_str(), sizeof(p_item->summary_path));
        }

        p_item->valid = true;
        p->recorder_index_count++;
        v_pos = v_path_end + 1;
    }

    return true;
}

bool T20_saveRecorderIndex(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    File v_f = LittleFS.open(G_T20_RECORDER_INDEX_PATH, "w");
    if (!v_f) {
        return false;
    }

    v_f.print("{\"items\":[");
    bool v_first = true;
    for (uint16_t i = 0; i < p->recorder_index_count; ++i) {
        ST_T20_RecorderIndexItem_t* p_item = &p->recorder_index_items[i];
        if (!p_item->valid) {
            continue;
        }

        if (!v_first) {
            v_f.print(",");
        }
        v_first = false;

        v_f.printf(
            "{\"path\":\"%s\",\"summary_path\":\"%s\",\"meta_path\":\"%s\",\"size_bytes\":%lu,\"frame_count\":%lu,\"crc32\":%lu,\"created_ms\":%lu}",
            p_item->path,
            p_item->summary_path,
            p_item->meta_path,
            (unsigned long)p_item->size_bytes,
            (unsigned long)p_item->frame_count,
            (unsigned long)p_item->crc32,
            (unsigned long)p_item->created_ms
        );
    }
    v_f.print("]}");
    v_f.close();
    return true;
}

bool T20_buildRecorderIndexJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    String v_json = "{\"items\":[";
    bool v_first = true;
    for (uint16_t i = 0; i < p->recorder_index_count; ++i) {
        ST_T20_RecorderIndexItem_t* p_item = &p->recorder_index_items[i];
        if (!p_item->valid) {
            continue;
        }

        if (!v_first) {
            v_json += ",";
        }
        v_first = false;

        v_json += "{\"path\":\"";
        v_json += p_item->path;
        v_json += "\",\"summary_path\":\"";
        v_json += p_item->summary_path;
        v_json += "\",\"meta_path\":\"";
        v_json += p_item->meta_path;
        v_json += "\",\"size_bytes\":";
        v_json += String((unsigned long)p_item->size_bytes);
        v_json += ",\"frame_count\":";
        v_json += String((unsigned long)p_item->frame_count);
        v_json += ",\"crc32\":";
        v_json += String((unsigned long)p_item->crc32);
        v_json += ",\"created_ms\":";
        v_json += String((unsigned long)p_item->created_ms);
        v_json += "}";
    }
    v_json += "]}";

    if ((uint16_t)(v_json.length() + 1) > p_len) {
        return false;
    }

    memcpy(p_out_buf, v_json.c_str(), v_json.length() + 1);
    return true;
}





bool T20_applyConfigJsonText(CL_T20_Mfcc::ST_Impl* p, const char* p_json_text)
{
    if (p == nullptr || p_json_text == nullptr) {
        return false;
    }

    JsonDocument v_doc;
    DeserializationError v_err = deserializeJson(v_doc, p_json_text);
    if (v_err) {
        return false;
    }

    ST_T20_Config_t v_cfg = p->cfg;

    JsonObject v_pre = v_doc["preprocess"];
    if (!v_pre.isNull()) {
        if (v_pre["axis"].is<int>()) v_cfg.preprocess.axis = (EM_T20_AxisType_t)((int)v_pre["axis"]);
        if (v_pre["remove_dc"].is<bool>()) v_cfg.preprocess.remove_dc = (bool)v_pre["remove_dc"];

        JsonObject v_pe = v_pre["preemphasis"];
        if (!v_pe.isNull()) {
            if (v_pe["enable"].is<bool>()) v_cfg.preprocess.preemphasis.enable = (bool)v_pe["enable"];
            if (v_pe["alpha"].is<float>()) v_cfg.preprocess.preemphasis.alpha = (float)v_pe["alpha"];
        }

        JsonObject v_filter = v_pre["filter"];
        if (!v_filter.isNull()) {
            if (v_filter["enable"].is<bool>()) v_cfg.preprocess.filter.enable = (bool)v_filter["enable"];
            if (v_filter["type"].is<int>()) v_cfg.preprocess.filter.type = (EM_T20_FilterType_t)((int)v_filter["type"]);
            if (v_filter["cutoff_hz_1"].is<float>()) v_cfg.preprocess.filter.cutoff_hz_1 = (float)v_filter["cutoff_hz_1"];
            if (v_filter["cutoff_hz_2"].is<float>()) v_cfg.preprocess.filter.cutoff_hz_2 = (float)v_filter["cutoff_hz_2"];
            if (v_filter["q_factor"].is<float>()) v_cfg.preprocess.filter.q_factor = (float)v_filter["q_factor"];
        }

        JsonObject v_noise = v_pre["noise"];
        if (!v_noise.isNull()) {
            if (v_noise["enable_gate"].is<bool>()) v_cfg.preprocess.noise.enable_gate = (bool)v_noise["enable_gate"];
            if (v_noise["gate_threshold_abs"].is<float>()) v_cfg.preprocess.noise.gate_threshold_abs = (float)v_noise["gate_threshold_abs"];
            if (v_noise["enable_spectral_subtract"].is<bool>()) v_cfg.preprocess.noise.enable_spectral_subtract = (bool)v_noise["enable_spectral_subtract"];
            if (v_noise["spectral_subtract_strength"].is<float>()) v_cfg.preprocess.noise.spectral_subtract_strength = (float)v_noise["spectral_subtract_strength"];
            if (v_noise["noise_learn_frames"].is<unsigned>()) v_cfg.preprocess.noise.noise_learn_frames = (uint16_t)((unsigned)v_noise["noise_learn_frames"]);
        }
    }

    JsonObject v_feat = v_doc["feature"];
    if (!v_feat.isNull()) {
        if (v_feat["fft_size"].is<unsigned>()) v_cfg.feature.fft_size = (uint16_t)((unsigned)v_feat["fft_size"]);
        if (v_feat["sample_rate_hz"].is<float>()) v_cfg.feature.sample_rate_hz = (float)v_feat["sample_rate_hz"];
        if (v_feat["mel_filters"].is<unsigned>()) v_cfg.feature.mel_filters = (uint16_t)((unsigned)v_feat["mel_filters"]);
        if (v_feat["mfcc_coeffs"].is<unsigned>()) v_cfg.feature.mfcc_coeffs = (uint16_t)((unsigned)v_feat["mfcc_coeffs"]);
        if (v_feat["delta_window"].is<unsigned>()) v_cfg.feature.delta_window = (uint16_t)((unsigned)v_feat["delta_window"]);
    }

    JsonObject v_out = v_doc["output"];
    if (!v_out.isNull()) {
        if (v_out["output_mode"].is<int>()) v_cfg.output.output_mode = (EM_T20_OutputMode_t)((int)v_out["output_mode"]);
        if (v_out["sequence_frames"].is<unsigned>()) v_cfg.output.sequence_frames = (uint16_t)((unsigned)v_out["sequence_frames"]);
        if (v_out["sequence_flatten"].is<bool>()) v_cfg.output.sequence_flatten = (bool)v_out["sequence_flatten"];
    }

    if (!T20_validateConfig(&v_cfg)) {
        return false;
    }

    p->cfg = v_cfg;
    T20_configureFilter(p);
    T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3));
    return true;
}

bool T20_getQueryParamPath(AsyncWebServerRequest* p_request, const char* p_name, char* p_out_path, uint16_t p_path_len)
{
    if (p_request == nullptr || p_name == nullptr || p_out_path == nullptr || p_path_len == 0) {
        return false;
    }

    if (!p_request->hasParam(p_name)) {
        return false;
    }

    String v_val = p_request->getParam(p_name)->value();
    if (v_val.length() == 0 || v_val.length() >= p_path_len) {
        return false;
    }

    if (v_val.indexOf("..") >= 0) {
        return false;
    }

    strlcpy(p_out_path, v_val.c_str(), p_path_len);
    return true;
}

bool T20_buildViewerDataJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["frame_id"] = p->viewer_last_frame_id;
    v_doc["vector_len"] = p->viewer_last_vector_len;
    v_doc["log_mel_len"] = p->viewer_last_log_mel_len;
    v_doc["mfcc_len"] = p->viewer_last_mfcc_len;
    v_doc["todo"] = "TODO: 이후 단계에서 파형/스펙트럼/이벤트/오버랩 시계열 viewer 확장";

    JsonArray v_vec = v_doc["vector"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_vector_len; ++i) {
        v_vec.add(p->viewer_last_vector[i]);
    }

    JsonArray v_mel = v_doc["log_mel"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_log_mel_len; ++i) {
        v_mel.add(p->viewer_last_log_mel[i]);
    }

    JsonArray v_mfcc = v_doc["mfcc"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_mfcc_len; ++i) {
        v_mfcc.add(p->viewer_last_mfcc[i]);
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildRecorderManifestJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["index_path"] = G_T20_RECORDER_INDEX_PATH;
    v_doc["manifest_path"] = G_T20_RECORDER_MANIFEST_PATH;
    v_doc["count"] = p->recorder_index_count;
    v_doc["todo"] = "TODO: 이후 단계에서 checksum/version/signature/range-download 메타데이터 확장";

    JsonArray v_items = v_doc["items"].to<JsonArray>();
    for (uint16_t i = 0; i < p->recorder_index_count; ++i) {
        ST_T20_RecorderIndexItem_t* p_item = &p->recorder_index_items[i];
        if (!p_item->valid) {
            continue;
        }
        JsonObject v_item = v_items.add<JsonObject>();
        v_item["path"] = p_item->path;
        v_item["summary"] = p_item->summary_path;
        v_item["meta"] = p_item->meta_path;
        v_item["size_bytes"] = p_item->size_bytes;
        v_item["frame_count"] = p_item->frame_count;
        v_item["crc32"] = p_item->crc32;
        v_item["created_ms"] = p_item->created_ms;
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


uint16_t T20_getQueryParamUint(AsyncWebServerRequest* p_request,
                               const char* p_name,
                               uint16_t p_default_value,
                               uint16_t p_min_value,
                               uint16_t p_max_value)
{
    if (p_request == nullptr || p_name == nullptr) {
        return p_default_value;
    }

    if (!p_request->hasParam(p_name)) {
        return p_default_value;
    }

    int v_value = p_request->getParam(p_name)->value().toInt();
    if (v_value < (int)p_min_value) {
        v_value = p_min_value;
    }
    if (v_value > (int)p_max_value) {
        v_value = p_max_value;
    }
    return (uint16_t)v_value;
}

void T20_pushViewerEvent(CL_T20_Mfcc::ST_Impl* p, const char* p_kind, const char* p_text)
{
    if (p == nullptr || p_kind == nullptr || p_text == nullptr) {
        return;
    }

    if (p->viewer_event_count < G_T20_VIEWER_EVENT_MAX) {
        ST_T20_ViewerEvent_t* p_evt = &p->viewer_events[p->viewer_event_count++];
        p_evt->frame_id = p->viewer_last_frame_id;
        strlcpy(p_evt->kind, p_kind, sizeof(p_evt->kind));
        strlcpy(p_evt->text, p_text, sizeof(p_evt->text));
        return;
    }

    for (uint16_t i = 0; i < (G_T20_VIEWER_EVENT_MAX - 1); ++i) {
        p->viewer_events[i] = p->viewer_events[i + 1];
    }

    ST_T20_ViewerEvent_t* p_evt = &p->viewer_events[G_T20_VIEWER_EVENT_MAX - 1];
    p_evt->frame_id = p->viewer_last_frame_id;
    strlcpy(p_evt->kind, p_kind, sizeof(p_evt->kind));
    strlcpy(p_evt->text, p_text, sizeof(p_evt->text));
}

bool T20_buildViewerWaveformJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["frame_id"] = p->viewer_last_frame_id;
    v_doc["points_len"] = p->viewer_last_waveform_len;
    v_doc["todo"] = "TODO: 이후 단계에서 hop_size/overlap 기반 다중 frame waveform viewer 확장";

    JsonArray v_arr = v_doc["waveform"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_waveform_len; ++i) {
        v_arr.add(p->viewer_last_waveform[i]);
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildViewerSpectrumJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["frame_id"] = p->viewer_last_frame_id;
    v_doc["points_len"] = p->viewer_last_spectrum_len;
    v_doc["sample_rate_hz"] = p->cfg.feature.sample_rate_hz;
    v_doc["todo"] = "TODO: 이후 단계에서 mel/spectrum overlay 및 줌 레벨 확장";

    JsonArray v_arr = v_doc["spectrum"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_last_spectrum_len; ++i) {
        v_arr.add(p->viewer_last_spectrum[i]);
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildViewerEventsJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["count"] = p->viewer_event_count;
    v_doc["todo"] = "TODO: 이후 단계에서 버튼/세션/recorder/에러 이벤트 timeline 확장";

    JsonArray v_arr = v_doc["events"].to<JsonArray>();
    for (uint16_t i = 0; i < p->viewer_event_count; ++i) {
        JsonObject v_evt = v_arr.add<JsonObject>();
        v_evt["frame_id"] = p->viewer_events[i].frame_id;
        v_evt["kind"] = p->viewer_events[i].kind;
        v_evt["text"] = p->viewer_events[i].text;
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* p_request,
                                 const char* p_name,
                                 uint32_t p_default_value,
                                 uint32_t p_min_value,
                                 uint32_t p_max_value)
{
    if (p_request == nullptr || p_name == nullptr) {
        return p_default_value;
    }

    if (!p_request->hasParam(p_name)) {
        return p_default_value;
    }

    uint32_t v_value = (uint32_t)p_request->getParam(p_name)->value().toInt();
    if (v_value < p_min_value) {
        v_value = p_min_value;
    }
    if (v_value > p_max_value) {
        v_value = p_max_value;
    }
    return v_value;
}

bool T20_appendRangeHex(const uint8_t* p_data, uint32_t p_len, String& p_out_hex)
{
    static const char* s_hex = "0123456789ABCDEF";

    if (p_data == nullptr) {
        return false;
    }

    p_out_hex.reserve((size_t)p_len * 2U + 1U);
    for (uint32_t i = 0; i < p_len; ++i) {
        uint8_t v = p_data[i];
        p_out_hex += s_hex[(v >> 4) & 0x0F];
        p_out_hex += s_hex[v & 0x0F];
    }
    return true;
}

bool T20_buildViewerSequenceJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["ready"] = p->latest_sequence_valid;
    v_doc["frames"] = p->seq_rb.frames;
    v_doc["feature_dim"] = p->seq_rb.feature_dim;
    v_doc["todo"] = "TODO: 이후 단계에서 hop_size 기반 겹침 윈도우 sequence timeline 차트 확장";

    JsonArray v_arr = v_doc["sequence"].to<JsonArray>();
    if (p->latest_sequence_valid) {
        uint32_t v_total = (uint32_t)p->seq_rb.frames * (uint32_t)p->seq_rb.feature_dim;
        for (uint32_t i = 0; i < v_total; ++i) {
            v_arr.add(p->latest_sequence_flat[i]);
        }
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}

bool T20_buildViewerOverviewJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["frame_id"] = p->viewer_last_frame_id;
    v_doc["initialized"] = p->initialized;
    v_doc["running"] = p->running;
    v_doc["dropped_frames"] = p->dropped_frames;
    v_doc["latest_vector_valid"] = p->latest_vector_valid;
    v_doc["latest_sequence_valid"] = p->latest_sequence_valid;
    v_doc["waveform_len"] = p->viewer_last_waveform_len;
    v_doc["spectrum_len"] = p->viewer_last_spectrum_len;
    v_doc["mfcc_len"] = p->viewer_last_mfcc_len;
    v_doc["event_count"] = p->viewer_event_count;
    v_doc["sequence_frames"] = p->seq_rb.frames;
    v_doc["sequence_feature_dim"] = p->seq_rb.feature_dim;
    v_doc["todo"] = "TODO: 이후 단계에서 overview에 세션/버튼/recorder batching 통계 추가";

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


void T20_pushViewerWaveformHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_waveform, uint16_t p_len)
{
    if (p == nullptr || p_waveform == nullptr || p_len == 0) {
        return;
    }

    uint16_t v_len = p_len;
    if (v_len > G_T20_VIEWER_WAVEFORM_POINTS_MAX) {
        v_len = G_T20_VIEWER_WAVEFORM_POINTS_MAX;
    }

    memcpy(p->viewer_recent_waveforms[p->viewer_recent_waveform_head], p_waveform, sizeof(float) * v_len);

    p->viewer_recent_waveform_head++;
    if (p->viewer_recent_waveform_head >= G_T20_VIEWER_MULTI_FRAMES_MAX) {
        p->viewer_recent_waveform_head = 0;
    }

    if (p->viewer_recent_waveform_count < G_T20_VIEWER_MULTI_FRAMES_MAX) {
        p->viewer_recent_waveform_count++;
    }
}

bool T20_buildViewerMultiFrameJsonText(CL_T20_Mfcc::ST_Impl* p, char* p_out_buf, uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["frame_count"] = p->viewer_recent_waveform_count;
    v_doc["points_len"] = p->viewer_last_waveform_len;
    v_doc["todo"] = "TODO: 이후 단계에서 hop_size/overlap 기반 multi-frame viewer와 차트 확대/축소 연동";

    JsonArray v_frames = v_doc["frames"].to<JsonArray>();
    uint16_t v_start = 0;
    if (p->viewer_recent_waveform_count == G_T20_VIEWER_MULTI_FRAMES_MAX) {
        v_start = p->viewer_recent_waveform_head;
    }

    for (uint16_t i = 0; i < p->viewer_recent_waveform_count; ++i) {
        uint16_t v_idx = (uint16_t)((v_start + i) % G_T20_VIEWER_MULTI_FRAMES_MAX);
        JsonArray v_arr = v_frames.add<JsonArray>();
        for (uint16_t k = 0; k < p->viewer_last_waveform_len; ++k) {
            v_arr.add(p->viewer_recent_waveforms[v_idx][k]);
        }
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


bool T20_pushSlidingSample(CL_T20_Mfcc::ST_Impl* p, float p_sample, uint8_t* p_frame_index_out)
{
    if (p == nullptr || p_frame_index_out == nullptr) {
        return false;
    }

    uint16_t v_fft_size = p->cfg.feature.fft_size;
    uint16_t v_hop_size = p->cfg.feature.hop_size;
    if (v_hop_size == 0 || v_hop_size > v_fft_size) {
        v_hop_size = v_fft_size;
    }

    if (p->frame_accum_count < G_T20_FRAME_ACCUM_CAPACITY) {
        p->frame_accum_buffer[p->frame_accum_count++] = p_sample;
    } else {
        memmove(&p->frame_accum_buffer[0],
                &p->frame_accum_buffer[1],
                sizeof(float) * (G_T20_FRAME_ACCUM_CAPACITY - 1));
        p->frame_accum_buffer[G_T20_FRAME_ACCUM_CAPACITY - 1] = p_sample;
    }

    p->viewer_effective_hop_size = v_hop_size;

    if (p->frame_accum_count < v_fft_size) {
        p->active_sample_index = p->frame_accum_count;
        return false;
    }

    uint8_t v_buf = p->active_fill_buffer;
    memcpy(p->frame_buffer[v_buf], p->frame_accum_buffer, sizeof(float) * v_fft_size);
    *p_frame_index_out = v_buf;

    p->active_fill_buffer = (uint8_t)((v_buf + 1U) % G_T20_RAW_FRAME_BUFFERS);

    if (v_hop_size >= p->frame_accum_count) {
        p->frame_accum_count = 0;
    } else {
        uint16_t v_remain = (uint16_t)(p->frame_accum_count - v_hop_size);
        memmove(&p->frame_accum_buffer[0],
                &p->frame_accum_buffer[v_hop_size],
                sizeof(float) * v_remain);
        p->frame_accum_count = v_remain;
    }

    p->active_sample_index = p->frame_accum_count;
    return true;
}

bool T20_buildRecorderParsedPreviewJsonText(CL_T20_Mfcc::ST_Impl* p,
                                            char* p_out_buf,
                                            uint16_t p_len,
                                            const char* p_path,
                                            uint32_t p_bytes)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0 || p_path == nullptr) {
        return false;
    }

    File v_file = LittleFS.open(p_path, "r");
    if (!v_file) {
        return false;
    }

    if (p_bytes == 0) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_DEFAULT;
    }
    if (p_bytes > G_T20_PREVIEW_TEXT_BYTES_MAX) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_MAX;
    }

    std::unique_ptr<char[]> v_buf(new char[p_bytes + 1]);
    if (!v_buf) {
        v_file.close();
        return false;
    }

    size_t v_read = v_file.readBytes(v_buf.get(), p_bytes);
    v_file.close();
    v_buf[v_read] = '\0';

    JsonDocument v_doc;
    v_doc["path"] = p_path;
    v_doc["bytes"] = (uint32_t)v_read;

    String v_text = String(v_buf.get());
    v_text.replace("\r\n", "\n");

    JsonArray v_lines = v_doc["lines"].to<JsonArray>();
    int v_start = 0;
    uint16_t v_added = 0;
    while (v_start < (int)v_text.length() && v_added < G_T20_PREVIEW_LINES_MAX) {
        int v_end = v_text.indexOf('\n', v_start);
        if (v_end < 0) {
            v_end = v_text.length();
        }
        String v_line = v_text.substring(v_start, v_end);
        v_lines.add(v_line);
        v_added++;
        v_start = v_end + 1;
    }

    v_doc["todo"] = "TODO: 이후 단계에서 CSV 컬럼 파싱 및 binary header decode 정식 구현";

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


static bool T20_isLikelyIntegerText(const String& p_text)
{
    if (p_text.length() == 0) {
        return false;
    }

    int v_start = 0;
    if (p_text[0] == '-' || p_text[0] == '+') {
        v_start = 1;
    }
    if (v_start >= (int)p_text.length()) {
        return false;
    }

    for (int i = v_start; i < (int)p_text.length(); ++i) {
        if (!isDigit((unsigned char)p_text[i])) {
            return false;
        }
    }
    return true;
}

static bool T20_isLikelyFloatText(const String& p_text)
{
    if (T20_isLikelyIntegerText(p_text)) {
        return true;
    }

    bool v_seen_dot = false;
    bool v_seen_digit = false;
    int v_start = 0;
    if (p_text.length() > 0 && (p_text[0] == '-' || p_text[0] == '+')) {
        v_start = 1;
    }

    for (int i = v_start; i < (int)p_text.length(); ++i) {
        char v_ch = p_text[i];
        if (isDigit((unsigned char)v_ch)) {
            v_seen_digit = true;
            continue;
        }
        if (v_ch == '.' && !v_seen_dot) {
            v_seen_dot = true;
            continue;
        }
        return false;
    }

    return v_seen_digit;
}

static String T20_guessCsvCellType(const String& p_text)
{
    if (p_text.length() == 0) {
        return "empty";
    }

    String v_lower = p_text;
    v_lower.toLowerCase();

    if (v_lower == "null" || v_lower == "none" || v_lower == "nan") {
        return "empty";
    }
    if (v_lower == "true" || v_lower == "false") {
        return "bool";
    }
    if (T20_isLikelyDateTimeText(p_text)) {
        return "datetime";
    }
    if (T20_isLikelyDateText(p_text)) {
        return "date";
    }
    if (T20_isLikelyIntegerText(p_text)) {
        return "int";
    }
    if (T20_isLikelyFloatText(p_text)) {
        return "float";
    }
    return "string";
}

static bool T20_decodeRecorderBinaryHeaderStrict(const uint8_t* p_buf,
                                                 size_t p_len,
                                                 ST_T20_RecorderBinaryHeader_t* p_out)
{
    if (p_buf == nullptr || p_out == nullptr) {
        return false;
    }
    if (p_len < sizeof(ST_T20_RecorderBinaryHeader_t)) {
        return false;
    }

    memcpy(p_out, p_buf, sizeof(ST_T20_RecorderBinaryHeader_t));

    if (!(p_out->magic[0] == 'T' &&
          p_out->magic[1] == '2' &&
          p_out->magic[2] == '0' &&
          p_out->magic[3] == 'B')) {
        return false;
    }

    if (p_out->version != G_T20_BINARY_HEADER_VERSION_SUPPORTED) {
        return false;
    }

    if (p_out->header_size < sizeof(ST_T20_RecorderBinaryHeader_t)) {
        return false;
    }

    if (p_out->fft_size != G_T20_FFT_SIZE) {
        return false;
    }

    if (p_out->mfcc_dim == 0 || p_out->mfcc_dim > G_T20_MFCC_COEFFS_MAX) {
        return false;
    }

    if (p_out->mel_filters != G_T20_MEL_FILTERS) {
        return false;
    }

    if (p_out->sequence_frames > G_T20_SEQUENCE_FRAMES_MAX) {
        return false;
    }

    return true;
}

bool T20_buildRecorderCsvSchemaJsonText(CL_T20_Mfcc::ST_Impl* p,
                                        char* p_out_buf,
                                        uint16_t p_len,
                                        const char* p_path,
                                        uint32_t p_bytes)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0 || p_path == nullptr) {
        return false;
    }

    File v_file = LittleFS.open(p_path, "r");
    if (!v_file) {
        return false;
    }

    if (p_bytes == 0) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_DEFAULT;
    }
    if (p_bytes > G_T20_PREVIEW_TEXT_BYTES_MAX) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_MAX;
    }

    std::unique_ptr<char[]> v_buf(new char[p_bytes + 1]);
    if (!v_buf) {
        v_file.close();
        return false;
    }

    size_t v_read = v_file.readBytes(v_buf.get(), p_bytes);
    v_file.close();
    v_buf[v_read] = 0;

    String v_text = String(v_buf.get());
    v_text.replace("\r\n", "\n");

    int v_end = v_text.indexOf('\n');
    if (v_end < 0) {
        v_end = v_text.length();
    }

    String v_header = v_text.substring(0, v_end);
    int v_row_start = v_end + 1;

    const uint16_t v_max_cols = G_T20_CSV_PREVIEW_COLUMNS_MAX;
    String v_names[G_T20_CSV_PREVIEW_COLUMNS_MAX];
    String v_types[G_T20_CSV_PREVIEW_COLUMNS_MAX];
    uint16_t v_col_count = 0;

    int v_col_start = 0;
    while (v_col_start <= (int)v_header.length() && v_col_count < v_max_cols) {
        int v_col_end = v_header.indexOf(',', v_col_start);
        if (v_col_end < 0) {
            v_col_end = v_header.length();
        }
        v_names[v_col_count] = v_header.substring(v_col_start, v_col_end);
        v_types[v_col_count] = "empty";
        v_col_count++;
        v_col_start = v_col_end + 1;
        if (v_col_end >= (int)v_header.length()) {
            break;
        }
    }

    uint16_t v_seen_rows = 0;
    while (v_row_start < (int)v_text.length() && v_seen_rows < G_T20_CSV_PREVIEW_ROWS_MAX) {
        int v_line_end = v_text.indexOf('\n', v_row_start);
        if (v_line_end < 0) {
            v_line_end = v_text.length();
        }
        String v_line = v_text.substring(v_row_start, v_line_end);

        int v_cell_start = 0;
        for (uint16_t c = 0; c < v_col_count; ++c) {
            int v_cell_end = v_line.indexOf(',', v_cell_start);
            if (v_cell_end < 0) {
                v_cell_end = v_line.length();
            }
            String v_cell = v_line.substring(v_cell_start, v_cell_end);
            String v_guess = T20_guessCsvCellType(v_cell);

            v_types[c] = T20_upgradeCsvTypeGuess(v_types[c], v_guess);

            v_cell_start = v_cell_end + 1;
            if (v_cell_end >= (int)v_line.length()) {
                break;
            }
        }

        v_seen_rows++;
        v_row_start = v_line_end + 1;
    }

    JsonDocument v_doc;
    v_doc["path"] = p_path;
    v_doc["bytes"] = (uint32_t)v_read;
    v_doc["mode"] = "csv_schema";
    v_doc["rows_sampled"] = v_seen_rows;
    v_doc["todo"] = "TODO: 이후 단계에서 CSV 타입 추론 고도화 및 nullable/date/datetime 추론 추가";

    JsonArray v_cols = v_doc["columns"].to<JsonArray>();
    for (uint16_t c = 0; c < v_col_count; ++c) {
        JsonObject v_col = v_cols.add<JsonObject>();
        v_col["index"] = c;
        v_col["name"] = v_names[c];
        v_col["type_guess"] = v_types[c];
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


static bool T20_isLikelyDateText(const String& p_text)
{
    if (p_text.length() != 10) {
        return false;
    }
    return isDigit((unsigned char)p_text[0]) &&
           isDigit((unsigned char)p_text[1]) &&
           isDigit((unsigned char)p_text[2]) &&
           isDigit((unsigned char)p_text[3]) &&
           p_text[4] == '-' &&
           isDigit((unsigned char)p_text[5]) &&
           isDigit((unsigned char)p_text[6]) &&
           p_text[7] == '-' &&
           isDigit((unsigned char)p_text[8]) &&
           isDigit((unsigned char)p_text[9]);
}

static bool T20_isLikelyDateTimeText(const String& p_text)
{
    if (p_text.length() < 19) {
        return false;
    }
    if (!T20_isLikelyDateText(p_text.substring(0, 10))) {
        return false;
    }
    if (!(p_text[10] == 'T' || p_text[10] == ' ')) {
        return false;
    }
    return isDigit((unsigned char)p_text[11]) &&
           isDigit((unsigned char)p_text[12]) &&
           p_text[13] == ':' &&
           isDigit((unsigned char)p_text[14]) &&
           isDigit((unsigned char)p_text[15]) &&
           p_text[16] == ':' &&
           isDigit((unsigned char)p_text[17]) &&
           isDigit((unsigned char)p_text[18]);
}

static String T20_upgradeCsvTypeGuess(const String& p_old_type, const String& p_new_type)
{
    if (p_old_type == "empty") {
        return p_new_type;
    }
    if (p_old_type == p_new_type) {
        return p_old_type;
    }
    if (p_new_type == "empty") {
        return p_old_type;
    }
    if (p_old_type == "string" || p_new_type == "string") {
        return "string";
    }
    if ((p_old_type == "datetime" && p_new_type == "date") ||
        (p_old_type == "date" && p_new_type == "datetime")) {
        return "datetime";
    }
    if ((p_old_type == "float" && p_new_type == "int") ||
        (p_old_type == "int" && p_new_type == "float")) {
        return "float";
    }
    if ((p_old_type == "bool" && (p_new_type == "int" || p_new_type == "float")) ||
        (p_new_type == "bool" && (p_old_type == "int" || p_old_type == "float"))) {
        return "string";
    }
    return "string";
}

bool T20_buildRecorderBinaryPayloadSchemaJsonText(CL_T20_Mfcc::ST_Impl* p,
                                                  char* p_out_buf,
                                                  uint16_t p_len,
                                                  const char* p_path)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0 || p_path == nullptr) {
        return false;
    }

    File v_file = LittleFS.open(p_path, "r");
    if (!v_file) {
        return false;
    }

    uint8_t v_header_buf[sizeof(ST_T20_RecorderBinaryHeader_t)] = {0};
    size_t v_got = v_file.read(v_header_buf, sizeof(v_header_buf));
    ST_T20_RecorderBinaryHeader_t v_hdr;
    bool v_strict = T20_decodeRecorderBinaryHeaderStrict(v_header_buf, v_got, &v_hdr);
    v_file.close();

    JsonDocument v_doc;
    v_doc["path"] = p_path;
    v_doc["strict_header"] = v_strict;
    v_doc["mode"] = "binary_payload_schema";
    v_doc["todo"] = "TODO: 이후 단계에서 binary payload record schema를 writer와 완전 일치시킬 것";

    if (v_strict) {
        JsonObject v_schema = v_doc["schema"].to<JsonObject>();
        v_schema["header_struct"] = "ST_T20_RecorderBinaryHeader_t";
        v_schema["record_prefix_struct"] = "ST_T20_RecorderBinaryRecord_t";
        v_schema["record_kind_default"] = G_T20_BINARY_RECORD_KIND_VECTOR;
        v_schema["value_count"] = v_hdr.mfcc_dim;
        v_schema["record_prefix_bytes"] = sizeof(ST_T20_RecorderBinaryRecord_t);
        v_schema["payload_bytes"] = (uint32_t)(sizeof(float) * v_hdr.mfcc_dim);
        v_schema["record_bytes"] = (uint32_t)(sizeof(ST_T20_RecorderBinaryRecord_t) + sizeof(float) * v_hdr.mfcc_dim);
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


static bool T20_matchCsvColumnFiltersCore(const std::vector<String>& p_cells,
                                          const std::vector<String>& p_filters)
{
    if (p_filters.empty()) {
        return true;
    }

    size_t v_n = p_filters.size();
    if (v_n > p_cells.size()) {
        v_n = p_cells.size();
    }

    for (size_t i = 0; i < v_n; ++i) {
        if (p_filters[i].length() == 0) {
            continue;
        }

        String v_cell = p_cells[i];
        String v_filter = p_filters[i];
        v_cell.toLowerCase();
        v_filter.toLowerCase();

        if (v_cell.indexOf(v_filter) < 0) {
            return false;
        }
    }

    return true;
}

/*
TODO:
- CSV table API에 column_filters[] 쿼리 파라미터를 정식 연결
- 현재 단계에서는 JS 렌더링 측 컬럼 필터 우선, 서버측 helper는 미리 준비
*/


static void T20_splitCsvColumnFilters(const String& p_csv, std::vector<String>& p_out)
{
    p_out.clear();

    int v_start = 0;
    while (v_start <= (int)p_csv.length() && p_out.size() < G_T20_CSV_SERVER_MAX_COL_FILTERS) {
        int v_end = p_csv.indexOf('|', v_start);
        if (v_end < 0) {
            v_end = p_csv.length();
        }
        p_out.push_back(p_csv.substring(v_start, v_end));
        v_start = v_end + 1;
        if (v_end >= (int)p_csv.length()) {
            break;
        }
    }
}

bool T20_buildRecorderCsvTableColumnFilteredJsonText(CL_T20_Mfcc::ST_Impl* p,
                                                     char* p_out_buf,
                                                     uint16_t p_len,
                                                     const char* p_path,
                                                     uint32_t p_bytes,
                                                     const char* p_global_filter,
                                                     const char* p_col_filters_csv,
                                                     uint16_t p_sort_col,
                                                     uint16_t p_page,
                                                     uint16_t p_page_size)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0 || p_path == nullptr) {
        return false;
    }

    File v_file = LittleFS.open(p_path, "r");
    if (!v_file) {
        return false;
    }

    if (p_bytes == 0) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_DEFAULT;
    }
    if (p_bytes > G_T20_PREVIEW_TEXT_BYTES_MAX) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_MAX;
    }

    std::unique_ptr<char[]> v_buf(new char[p_bytes + 1]);
    if (!v_buf) {
        v_file.close();
        return false;
    }

    size_t v_read = v_file.readBytes(v_buf.get(), p_bytes);
    v_file.close();
    v_buf[v_read] = 0;

    String v_text = String(v_buf.get());
    v_text.replace("\r\n", "\n");

    std::vector<std::vector<String>> v_rows;
    int v_line_start = 0;
    while (v_line_start <= (int)v_text.length() && v_rows.size() < G_T20_CSV_SERVER_MAX_ROWS) {
        int v_line_end = v_text.indexOf('\n', v_line_start);
        if (v_line_end < 0) {
            v_line_end = v_text.length();
        }

        String v_line = v_text.substring(v_line_start, v_line_end);
        std::vector<String> v_cells;
        int v_cell_start = 0;
        while (v_cell_start <= (int)v_line.length()) {
            int v_cell_end = v_line.indexOf(',', v_cell_start);
            if (v_cell_end < 0) {
                v_cell_end = v_line.length();
            }
            v_cells.push_back(v_line.substring(v_cell_start, v_cell_end));
            v_cell_start = v_cell_end + 1;
            if (v_cell_end >= (int)v_line.length()) {
                break;
            }
        }
        v_rows.push_back(v_cells);

        v_line_start = v_line_end + 1;
        if (v_line_end >= (int)v_text.length()) {
            break;
        }
    }

    if (v_rows.empty()) {
        return false;
    }

    std::vector<String> v_header = v_rows.front();
    std::vector<std::vector<String>> v_data(v_rows.begin() + 1, v_rows.end());

    String v_filter = p_global_filter ? String(p_global_filter) : String("");
    std::vector<String> v_col_filters;
    T20_splitCsvColumnFilters(String(p_col_filters_csv ? p_col_filters_csv : ""), v_col_filters);

    std::vector<std::vector<String>> v_filtered;
    for (const auto& v_row : v_data) {
        if (!T20_csvRowMatchesGlobalFilter(v_row, v_filter)) {
            continue;
        }
        if (!T20_matchCsvColumnFiltersCore(v_row, v_col_filters)) {
            continue;
        }
        v_filtered.push_back(v_row);
    }

    std::sort(v_filtered.begin(), v_filtered.end(),
        [p_sort_col](const std::vector<String>& a, const std::vector<String>& b) {
            String va = (p_sort_col < a.size()) ? a[p_sort_col] : "";
            String vb = (p_sort_col < b.size()) ? b[p_sort_col] : "";
            return va < vb;
        });

    if (p_page_size == 0) {
        p_page_size = G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT;
    }
    if (p_page_size > G_T20_CSV_TABLE_PAGE_SIZE_MAX) {
        p_page_size = G_T20_CSV_TABLE_PAGE_SIZE_MAX;
    }

    size_t v_total_rows = v_filtered.size();
    size_t v_total_pages = (v_total_rows == 0) ? 1 : ((v_total_rows + p_page_size - 1) / p_page_size);
    if (p_page >= v_total_pages) {
        p_page = 0;
    }

    size_t v_start = p_page * p_page_size;
    size_t v_end = std::min(v_start + p_page_size, v_total_rows);

    JsonDocument v_doc;
    v_doc["path"] = p_path;
    v_doc["bytes"] = (uint32_t)v_read;
    v_doc["mode"] = "csv_table_column_filtered";
    v_doc["global_filter"] = v_filter;
    v_doc["col_filters_csv"] = String(p_col_filters_csv ? p_col_filters_csv : "");
    v_doc["sort_col"] = p_sort_col;
    v_doc["page"] = p_page;
    v_doc["page_size"] = p_page_size;
    v_doc["total_rows"] = (uint32_t)v_total_rows;
    v_doc["total_pages"] = (uint32_t)v_total_pages;
    v_doc["todo"] = "TODO: 이후 단계에서 asc/desc, 숫자형/date 정렬 강화";

    JsonArray v_rows_json = v_doc["rows"].to<JsonArray>();
    JsonArray v_header_json = v_rows_json.add<JsonArray>();
    for (const auto& v_col : v_header) {
        v_header_json.add(v_col);
    }

    for (size_t i = v_start; i < v_end; ++i) {
        JsonArray v_row_json = v_rows_json.add<JsonArray>();
        for (const auto& v_cell : v_filtered[i]) {
            v_row_json.add(v_cell);
        }
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


bool T20_parseSortableNumber(const String& p_text, double* p_out_value)
{
    if (p_out_value == nullptr || p_text.length() == 0) {
        return false;
    }

    char v_buf[64] = {0};
    size_t v_n = p_text.length();
    if (v_n >= sizeof(v_buf)) {
        v_n = sizeof(v_buf) - 1;
    }
    memcpy(v_buf, p_text.c_str(), v_n);
    v_buf[v_n] = 0;

    char* v_end = nullptr;
    double v_value = strtod(v_buf, &v_end);
    if (v_end == v_buf || *v_end != 0) {
        return false;
    }

    *p_out_value = v_value;
    return true;
}

bool T20_parseSortableDateTimeKey(const String& p_text, uint64_t* p_out_key)
{
    if (p_out_key == nullptr) {
        return false;
    }

    String v = p_text;
    v.trim();

    if (v.length() == 10) {
        if (!(isDigit((unsigned char)v[0]) && isDigit((unsigned char)v[1]) &&
              isDigit((unsigned char)v[2]) && isDigit((unsigned char)v[3]) &&
              v[4] == '-' &&
              isDigit((unsigned char)v[5]) && isDigit((unsigned char)v[6]) &&
              v[7] == '-' &&
              isDigit((unsigned char)v[8]) && isDigit((unsigned char)v[9]))) {
            return false;
        }
        uint64_t key = 0;
        key += (uint64_t)v.substring(0, 4).toInt() * 10000000000ULL;
        key += (uint64_t)v.substring(5, 7).toInt() * 100000000ULL;
        key += (uint64_t)v.substring(8, 10).toInt() * 1000000ULL;
        *p_out_key = key;
        return true;
    }

    if (v.length() >= 19) {
        if (!(isDigit((unsigned char)v[0]) && isDigit((unsigned char)v[1]) &&
              isDigit((unsigned char)v[2]) && isDigit((unsigned char)v[3]) &&
              v[4] == '-' &&
              isDigit((unsigned char)v[5]) && isDigit((unsigned char)v[6]) &&
              v[7] == '-' &&
              isDigit((unsigned char)v[8]) && isDigit((unsigned char)v[9]) &&
              (v[10] == 'T' || v[10] == ' ') &&
              isDigit((unsigned char)v[11]) && isDigit((unsigned char)v[12]) &&
              v[13] == ':' &&
              isDigit((unsigned char)v[14]) && isDigit((unsigned char)v[15]) &&
              v[16] == ':' &&
              isDigit((unsigned char)v[17]) && isDigit((unsigned char)v[18]))) {
            return false;
        }
        uint64_t key = 0;
        key += (uint64_t)v.substring(0, 4).toInt() * 10000000000ULL;
        key += (uint64_t)v.substring(5, 7).toInt() * 100000000ULL;
        key += (uint64_t)v.substring(8, 10).toInt() * 1000000ULL;
        key += (uint64_t)v.substring(11, 13).toInt() * 10000ULL;
        key += (uint64_t)v.substring(14, 16).toInt() * 100ULL;
        key += (uint64_t)v.substring(17, 19).toInt();
        *p_out_key = key;
        return true;
    }

    return false;
}

static void T20_splitCsvColumnFiltersAdv(const String& p_csv, std::vector<String>& p_out)
{
    p_out.clear();
    int v_start = 0;
    while (v_start <= (int)p_csv.length() && p_out.size() < G_T20_CSV_SERVER_MAX_COL_FILTERS) {
        int v_end = p_csv.indexOf('|', v_start);
        if (v_end < 0) {
            v_end = p_csv.length();
        }
        p_out.push_back(p_csv.substring(v_start, v_end));
        v_start = v_end + 1;
        if (v_end >= (int)p_csv.length()) {
            break;
        }
    }
}

static int T20_compareCsvCellAdvanced(const String& p_a, const String& p_b)
{
    double va = 0.0, vb = 0.0;
    bool na = T20_parseSortableNumber(p_a, &va);
    bool nb = T20_parseSortableNumber(p_b, &vb);
    if (na && nb) {
        if (va < vb) return -1;
        if (va > vb) return 1;
        return 0;
    }

    uint64_t da = 0, db = 0;
    bool ta = T20_parseSortableDateTimeKey(p_a, &da);
    bool tb = T20_parseSortableDateTimeKey(p_b, &db);
    if (ta && tb) {
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }

    if (p_a < p_b) return -1;
    if (p_a > p_b) return 1;
    return 0;
}

static bool T20_matchCsvColumnFiltersAdvanced(const std::vector<String>& p_cells,
                                              const std::vector<String>& p_filters)
{
    if (p_filters.empty()) {
        return true;
    }

    size_t v_n = p_filters.size();
    if (v_n > p_cells.size()) {
        v_n = p_cells.size();
    }

    for (size_t i = 0; i < v_n; ++i) {
        if (p_filters[i].length() == 0) {
            continue;
        }
        String v_cell = p_cells[i];
        String v_filter = p_filters[i];
        v_cell.toLowerCase();
        v_filter.toLowerCase();
        if (v_cell.indexOf(v_filter) < 0) {
            return false;
        }
    }
    return true;
}

bool T20_buildRecorderCsvTableAdvancedJsonText(CL_T20_Mfcc::ST_Impl* p,
                                               char* p_out_buf,
                                               uint16_t p_len,
                                               const char* p_path,
                                               uint32_t p_bytes,
                                               const char* p_global_filter,
                                               const char* p_col_filters_csv,
                                               uint16_t p_sort_col,
                                               uint16_t p_sort_dir,
                                               uint16_t p_page,
                                               uint16_t p_page_size)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0 || p_path == nullptr) {
        return false;
    }

    File v_file = LittleFS.open(p_path, "r");
    if (!v_file) {
        return false;
    }

    if (p_bytes == 0) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_DEFAULT;
    }
    if (p_bytes > G_T20_PREVIEW_TEXT_BYTES_MAX) {
        p_bytes = G_T20_PREVIEW_TEXT_BYTES_MAX;
    }

    std::unique_ptr<char[]> v_buf(new char[p_bytes + 1]);
    if (!v_buf) {
        v_file.close();
        return false;
    }

    size_t v_read = v_file.readBytes(v_buf.get(), p_bytes);
    v_file.close();
    v_buf[v_read] = 0;

    String v_text = String(v_buf.get());
    v_text.replace("\r\n", "\n");

    std::vector<std::vector<String>> v_rows;
    int v_line_start = 0;
    while (v_line_start <= (int)v_text.length() && v_rows.size() < G_T20_CSV_SERVER_MAX_ROWS) {
        int v_line_end = v_text.indexOf('\n', v_line_start);
        if (v_line_end < 0) {
            v_line_end = v_text.length();
        }

        String v_line = v_text.substring(v_line_start, v_line_end);
        std::vector<String> v_cells;
        int v_cell_start = 0;
        while (v_cell_start <= (int)v_line.length()) {
            int v_cell_end = v_line.indexOf(',', v_cell_start);
            if (v_cell_end < 0) {
                v_cell_end = v_line.length();
            }
            v_cells.push_back(v_line.substring(v_cell_start, v_cell_end));
            v_cell_start = v_cell_end + 1;
            if (v_cell_end >= (int)v_line.length()) {
                break;
            }
        }
        v_rows.push_back(v_cells);

        v_line_start = v_line_end + 1;
        if (v_line_end >= (int)v_text.length()) {
            break;
        }
    }

    if (v_rows.empty()) {
        return false;
    }

    std::vector<String> v_header = v_rows.front();
    std::vector<std::vector<String>> v_data(v_rows.begin() + 1, v_rows.end());

    String v_filter = p_global_filter ? String(p_global_filter) : String("");
    std::vector<String> v_col_filters;
    T20_splitCsvColumnFiltersAdv(String(p_col_filters_csv ? p_col_filters_csv : ""), v_col_filters);

    std::vector<std::vector<String>> v_filtered;
    for (const auto& v_row : v_data) {
        if (!T20_csvRowMatchesGlobalFilter(v_row, v_filter)) {
            continue;
        }
        if (!T20_matchCsvColumnFiltersAdvanced(v_row, v_col_filters)) {
            continue;
        }
        v_filtered.push_back(v_row);
    }

    std::sort(v_filtered.begin(), v_filtered.end(),
        [p_sort_col, p_sort_dir](const std::vector<String>& a, const std::vector<String>& b) {
            String va = (p_sort_col < a.size()) ? a[p_sort_col] : "";
            String vb = (p_sort_col < b.size()) ? b[p_sort_col] : "";
            int cmp = T20_compareCsvCellAdvanced(va, vb);
            if (p_sort_dir == G_T20_CSV_SORT_DESC) {
                cmp = -cmp;
            }
            return cmp < 0;
        });

    if (p_page_size == 0) {
        p_page_size = G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT;
    }
    if (p_page_size > G_T20_CSV_TABLE_PAGE_SIZE_MAX) {
        p_page_size = G_T20_CSV_TABLE_PAGE_SIZE_MAX;
    }

    size_t v_total_rows = v_filtered.size();
    size_t v_total_pages = (v_total_rows == 0) ? 1 : ((v_total_rows + p_page_size - 1) / p_page_size);
    if (p_page >= v_total_pages) {
        p_page = 0;
    }

    size_t v_start = p_page * p_page_size;
    size_t v_end = std::min(v_start + p_page_size, v_total_rows);

    JsonDocument v_doc;
    v_doc["path"] = p_path;
    v_doc["bytes"] = (uint32_t)v_read;
    v_doc["mode"] = "csv_table_advanced";
    v_doc["global_filter"] = v_filter;
    v_doc["col_filters_csv"] = String(p_col_filters_csv ? p_col_filters_csv : "");
    v_doc["sort_col"] = p_sort_col;
    v_doc["sort_dir"] = p_sort_dir;
    v_doc["page"] = p_page;
    v_doc["page_size"] = p_page_size;
    v_doc["total_rows"] = (uint32_t)v_total_rows;
    v_doc["total_pages"] = (uint32_t)v_total_pages;
    v_doc["todo"] = "TODO: 이후 단계에서 타입 메타 캐시/컬럼 메타정보와 정렬 연동";

    JsonArray v_rows_json = v_doc["rows"].to<JsonArray>();
    JsonArray v_header_json = v_rows_json.add<JsonArray>();
    for (const auto& v_col : v_header) {
        v_header_json.add(v_col);
    }

    for (size_t i = v_start; i < v_end; ++i) {
        JsonArray v_row_json = v_rows_json.add<JsonArray>();
        for (const auto& v_cell : v_filtered[i]) {
            v_row_json.add(v_cell);
        }
    }

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


bool T20_buildRenderSelectionSyncJsonText(CL_T20_Mfcc::ST_Impl* p,
                                          char* p_out_buf,
                                          uint16_t p_len)
{
    if (p == nullptr || p_out_buf == nullptr || p_len == 0) {
        return false;
    }

    JsonDocument v_doc;
    v_doc["mode"] = "render_selection_sync";
    v_doc["series_max"] = G_T20_RECORDER_RENDER_SYNC_SERIES_MAX;
    v_doc["selection_sync_max"] = G_T20_RENDER_SELECTION_SYNC_MAX;
    v_doc["todo"] = "TODO: 이후 단계에서 실제 selection 범위/캔버스별 동기 상태 연결";

    size_t v_need = measureJson(v_doc) + 1;
    if (v_need > p_len) {
        return false;
    }

    serializeJson(v_doc, p_out_buf, p_len);
    return true;
}


bool T20_parseHttpRangeHeader(const String& p_range,
                              uint32_t p_file_size,
                              uint32_t* p_offset_out,
                              uint32_t* p_length_out)
{
    if (p_offset_out == nullptr || p_length_out == nullptr) {
        return false;
    }

    *p_offset_out = 0;
    *p_length_out = 0;

    if (!p_range.startsWith("bytes=")) {
        return false;
    }

    int v_dash = p_range.indexOf('-');
    if (v_dash < 0) {
        return false;
    }

    String v_start = p_range.substring(6, v_dash);
    String v_end   = p_range.substring(v_dash + 1);

    uint32_t v_offset = 0;
    uint32_t v_end_pos = 0;

    if (v_start.length() == 0) {
        return false;
    }

    v_offset = (uint32_t)v_start.toInt();
    if (v_offset >= p_file_size) {
        return false;
    }

    if (v_end.length() == 0) {
        v_end_pos = p_file_size - 1;
    } else {
        v_end_pos = (uint32_t)v_end.toInt();
        if (v_end_pos >= p_file_size) {
            v_end_pos = p_file_size - 1;
        }
        if (v_end_pos < v_offset) {
            return false;
        }
    }

    *p_offset_out = v_offset;
    *p_length_out = (v_end_pos - v_offset) + 1U;
    return true;
}
