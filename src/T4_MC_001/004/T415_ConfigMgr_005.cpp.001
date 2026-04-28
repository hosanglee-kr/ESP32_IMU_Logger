/* ============================================================================
 * File: T415_ConfigMgr_005.cpp
 * Summary: Dynamic JSON Configuration Manager Implementation
 * * [AI 메모: 전체 필드 매핑 완료]
 * 1. dsp, feature, decision, storage, mqtt 5개 그룹의 총 29개 필드 100% 매핑.
 * 2. ArduinoJson V7.4.x의 JsonArray를 활용한 2차원 배열(band_ranges) 입출력.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * ========================================================================== */
#include "T415_ConfigMgr_005.hpp"
#include "esp_log.h"

static const char* TAG = "T415_CFG";

T415_ConfigManager::T415_ConfigManager() {
    _lock = xSemaphoreCreateMutex();
    _isLoaded = false;
    _loadDefaults(); // 생성 시 메모리를 기본값으로 채워둠
}

T415_ConfigManager::~T415_ConfigManager() {
    if (_lock) vSemaphoreDelete(_lock);
}

bool T415_ConfigManager::init() {
    if (!LittleFS.begin(true)) {
        ESP_LOGE(TAG, "LittleFS Mount Failed!");
        return false;
    }

    // 설정 파일이 없으면 기본값으로 새 파일 생성
    if (!LittleFS.exists(SmeaConfig::Path::SYS_CFG_JSON_DEF)) {
        ESP_LOGW(TAG, "Config not found. Creating default...");
        _loadDefaults();
        save();
    } else {
        load();
    }
    
    _isLoaded = true;
    return true;
}

void T415_ConfigManager::_loadDefaults() {
    // 1. Dsp (13개 필드)
    _config.dsp.window_ms = SmeaConfig::Dsp::WINDOW_MS_DEF;
    _config.dsp.hop_ms = SmeaConfig::Dsp::HOP_MS_DEF;
    _config.dsp.notch_freq_hz = SmeaConfig::Dsp::NOTCH_FREQ_HZ_DEF;
    _config.dsp.notch_freq_2_hz = SmeaConfig::Dsp::NOTCH_FREQ_2_HZ_DEF;
    _config.dsp.notch_q_factor = SmeaConfig::Dsp::NOTCH_Q_FACTOR_DEF;
    _config.dsp.pre_emphasis_alpha = SmeaConfig::Dsp::PRE_EMPHASIS_ALPHA_DEF;
    _config.dsp.beamforming_gain = SmeaConfig::Dsp::BEAMFORMING_GAIN_DEF;
    _config.dsp.fir_lpf_cutoff = SmeaConfig::Dsp::FIR_LPF_CUTOFF_DEF;
    _config.dsp.fir_hpf_cutoff = SmeaConfig::Dsp::FIR_HPF_CUTOFF_DEF;
    _config.dsp.median_window = SmeaConfig::Dsp::MEDIAN_WINDOW_DEF;
    _config.dsp.noise_gate_thresh = SmeaConfig::Dsp::NOISE_GATE_THRESH_DEF;
    _config.dsp.noise_learn_frames = SmeaConfig::Dsp::NOISE_LEARN_FRAMES_DEF;
    _config.dsp.spectral_sub_gain = SmeaConfig::Dsp::SPECTRAL_SUB_GAIN_DEF;

    // 2. Feature (2개 필드)
    _config.feature.band_rms_count = SmeaConfig::Feature::BAND_RMS_COUNT_DEF;
    for (int i = 0; i < SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST; i++) {
        _config.feature.band_ranges[i][0] = SmeaConfig::Feature::BAND_RANGES_DEF[i][0];
        _config.feature.band_ranges[i][1] = SmeaConfig::Feature::BAND_RANGES_DEF[i][1];
    }

    // 3. Decision (7개 필드)
    _config.decision.rule_enrg_threshold = SmeaConfig::Decision::RULE_ENRG_THRESHOLD_DEF;
    _config.decision.rule_stddev_threshold = SmeaConfig::Decision::RULE_STDDEV_THRESHOLD_DEF;
    _config.decision.test_ng_min_energy = SmeaConfig::Decision::TEST_NG_MIN_ENERGY_DEF;
    _config.decision.min_trigger_count = SmeaConfig::Decision::MIN_TRIGGER_COUNT_DEF;
    _config.decision.noise_profile_sec = SmeaConfig::Decision::NOISE_PROFILE_SEC_DEF;
    _config.decision.valid_start_sec = SmeaConfig::Decision::VALID_START_SEC_DEF;
    _config.decision.valid_end_sec = SmeaConfig::Decision::VALID_END_SEC_DEF;

    // 4. Storage (4개 필드)
    _config.storage.pre_trigger_sec = SmeaConfig::Storage::PRE_TRIGGER_SEC_DEF;
    _config.storage.rotate_mb = SmeaConfig::Storage::ROTATE_MB_DEF;
    _config.storage.rotate_min = SmeaConfig::Storage::ROTATE_MIN_DEF;
    _config.storage.idle_flush_ms = SmeaConfig::Storage::IDLE_FLUSH_MS_DEF;

    // 5. Mqtt (3개 필드)
    strlcpy(_config.mqtt.mqtt_broker, "", sizeof(_config.mqtt.mqtt_broker));
    _config.mqtt.retry_interval_ms = SmeaConfig::Mqtt::RETRY_INTERVAL_MS_DEF;
    _config.mqtt.default_port = SmeaConfig::Mqtt::DEFAULT_PORT_DEF;
}

bool T415_ConfigManager::load() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    
    File v_file = LittleFS.open(SmeaConfig::Path::SYS_CFG_JSON_DEF, "r");
    if (!v_file) {
        xSemaphoreGive(_lock);
        return false;
    }

    JsonDocument v_doc; 
    DeserializationError v_err = deserializeJson(v_doc, v_file);
    v_file.close();

    if (v_err) {
        ESP_LOGE(TAG, "JSON Parse Error: %s", v_err.c_str());
        xSemaphoreGive(_lock);
        return false;
    }

    // 1. Dsp 파싱 (키가 없을 경우 기존 구조체 값을 파괴하지 않고 파이프(|)로 방어)
    JsonObject v_dsp = v_doc["dsp"];
    if (!v_dsp.isNull()) {
        _config.dsp.window_ms = v_dsp["window_ms"] | _config.dsp.window_ms;
        _config.dsp.hop_ms = v_dsp["hop_ms"] | _config.dsp.hop_ms;
        _config.dsp.notch_freq_hz = v_dsp["notch_freq_hz"] | _config.dsp.notch_freq_hz;
        _config.dsp.notch_freq_2_hz = v_dsp["notch_freq_2_hz"] | _config.dsp.notch_freq_2_hz;
        _config.dsp.notch_q_factor = v_dsp["notch_q_factor"] | _config.dsp.notch_q_factor;
        _config.dsp.pre_emphasis_alpha = v_dsp["pre_emphasis_alpha"] | _config.dsp.pre_emphasis_alpha;
        _config.dsp.beamforming_gain = v_dsp["beamforming_gain"] | _config.dsp.beamforming_gain;
        _config.dsp.fir_lpf_cutoff = v_dsp["fir_lpf_cutoff"] | _config.dsp.fir_lpf_cutoff;
        _config.dsp.fir_hpf_cutoff = v_dsp["fir_hpf_cutoff"] | _config.dsp.fir_hpf_cutoff;
        _config.dsp.median_window = v_dsp["median_window"] | _config.dsp.median_window;
        _config.dsp.noise_gate_thresh = v_dsp["noise_gate_thresh"] | _config.dsp.noise_gate_thresh;
        _config.dsp.noise_learn_frames = v_dsp["noise_learn_frames"] | _config.dsp.noise_learn_frames;
        _config.dsp.spectral_sub_gain = v_dsp["spectral_sub_gain"] | _config.dsp.spectral_sub_gain;
    }

    // 2. Feature 파싱 (2차원 배열 언패킹 포함)
    JsonObject v_feature = v_doc["feature"];
    if (!v_feature.isNull()) {
        _config.feature.band_rms_count = v_feature["band_rms_count"] | _config.feature.band_rms_count;
        
        JsonArray v_ranges = v_feature["band_ranges"];
        if (!v_ranges.isNull()) {
            int i = 0;
            for (JsonArray v_band : v_ranges) {
                if (i >= SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST) break;
                _config.feature.band_ranges[i][0] = v_band[0] | _config.feature.band_ranges[i][0];
                _config.feature.band_ranges[i][1] = v_band[1] | _config.feature.band_ranges[i][1];
                i++;
            }
        }
    }

    // 3. Decision 파싱
    JsonObject v_decision = v_doc["decision"];
    if (!v_decision.isNull()) {
        _config.decision.rule_enrg_threshold = v_decision["rule_enrg_threshold"] | _config.decision.rule_enrg_threshold;
        _config.decision.rule_stddev_threshold = v_decision["rule_stddev_threshold"] | _config.decision.rule_stddev_threshold;
        _config.decision.test_ng_min_energy = v_decision["test_ng_min_energy"] | _config.decision.test_ng_min_energy;
        _config.decision.min_trigger_count = v_decision["min_trigger_count"] | _config.decision.min_trigger_count;
        _config.decision.noise_profile_sec = v_decision["noise_profile_sec"] | _config.decision.noise_profile_sec;
        _config.decision.valid_start_sec = v_decision["valid_start_sec"] | _config.decision.valid_start_sec;
        _config.decision.valid_end_sec = v_decision["valid_end_sec"] | _config.decision.valid_end_sec;
    }

    // 4. Storage 파싱
    JsonObject v_storage = v_doc["storage"];
    if (!v_storage.isNull()) {
        _config.storage.pre_trigger_sec = v_storage["pre_trigger_sec"] | _config.storage.pre_trigger_sec;
        _config.storage.rotate_mb = v_storage["rotate_mb"] | _config.storage.rotate_mb;
        _config.storage.rotate_min = v_storage["rotate_min"] | _config.storage.rotate_min;
        _config.storage.idle_flush_ms = v_storage["idle_flush_ms"] | _config.storage.idle_flush_ms;
    }

    // 5. Mqtt 파싱
    JsonObject v_mqtt = v_doc["mqtt"];
    if (!v_mqtt.isNull()) {
        strlcpy(_config.mqtt.mqtt_broker, v_mqtt["mqtt_broker"] | _config.mqtt.mqtt_broker, sizeof(_config.mqtt.mqtt_broker));
        _config.mqtt.retry_interval_ms = v_mqtt["retry_interval_ms"] | _config.mqtt.retry_interval_ms;
        _config.mqtt.default_port = v_mqtt["default_port"] | _config.mqtt.default_port;
    }

    xSemaphoreGive(_lock);
    return true;
}

bool T415_ConfigManager::save() {
    xSemaphoreTake(_lock, portMAX_DELAY);

    JsonDocument v_doc;

    // 1. Dsp 직렬화
    JsonObject v_dsp = v_doc["dsp"].to<JsonObject>();
    v_dsp["window_ms"] = _config.dsp.window_ms;
    v_dsp["hop_ms"] = _config.dsp.hop_ms;
    v_dsp["notch_freq_hz"] = _config.dsp.notch_freq_hz;
    v_dsp["notch_freq_2_hz"] = _config.dsp.notch_freq_2_hz;
    v_dsp["notch_q_factor"] = _config.dsp.notch_q_factor;
    v_dsp["pre_emphasis_alpha"] = _config.dsp.pre_emphasis_alpha;
    v_dsp["beamforming_gain"] = _config.dsp.beamforming_gain;
    v_dsp["fir_lpf_cutoff"] = _config.dsp.fir_lpf_cutoff;
    v_dsp["fir_hpf_cutoff"] = _config.dsp.fir_hpf_cutoff;
    v_dsp["median_window"] = _config.dsp.median_window;
    v_dsp["noise_gate_thresh"] = _config.dsp.noise_gate_thresh;
    v_dsp["noise_learn_frames"] = _config.dsp.noise_learn_frames;
    v_dsp["spectral_sub_gain"] = _config.dsp.spectral_sub_gain;

    // 2. Feature 직렬화 (2차원 배열 패킹)
    JsonObject v_feature = v_doc["feature"].to<JsonObject>();
    v_feature["band_rms_count"] = _config.feature.band_rms_count;
    
    JsonArray v_ranges = v_feature["band_ranges"].to<JsonArray>();
    for (int i = 0; i < SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST; i++) {
        JsonArray v_band = v_ranges.add<JsonArray>();
        v_band.add(_config.feature.band_ranges[i][0]);
        v_band.add(_config.feature.band_ranges[i][1]);
    }

    // 3. Decision 직렬화
    JsonObject v_decision = v_doc["decision"].to<JsonObject>();
    v_decision["rule_enrg_threshold"] = _config.decision.rule_enrg_threshold;
    v_decision["rule_stddev_threshold"] = _config.decision.rule_stddev_threshold;
    v_decision["test_ng_min_energy"] = _config.decision.test_ng_min_energy;
    v_decision["min_trigger_count"] = _config.decision.min_trigger_count;
    v_decision["noise_profile_sec"] = _config.decision.noise_profile_sec;
    v_decision["valid_start_sec"] = _config.decision.valid_start_sec;
    v_decision["valid_end_sec"] = _config.decision.valid_end_sec;

    // 4. Storage 직렬화
    JsonObject v_storage = v_doc["storage"].to<JsonObject>();
    v_storage["pre_trigger_sec"] = _config.storage.pre_trigger_sec;
    v_storage["rotate_mb"] = _config.storage.rotate_mb;
    v_storage["rotate_min"] = _config.storage.rotate_min;
    v_storage["idle_flush_ms"] = _config.storage.idle_flush_ms;

    // 5. Mqtt 직렬화
    JsonObject v_mqtt = v_doc["mqtt"].to<JsonObject>();
    v_mqtt["mqtt_broker"] = _config.mqtt.mqtt_broker;
    v_mqtt["retry_interval_ms"] = _config.mqtt.retry_interval_ms;
    v_mqtt["default_port"] = _config.mqtt.default_port;

    // [방어] 정전 벽돌화 방지용 Atomic Write 적용
    File v_file = LittleFS.open("/sys/config.tmp", "w");
    if (!v_file) {
        xSemaphoreGive(_lock);
        return false;
    }

    serializeJson(v_doc, v_file);
    v_file.close();

    LittleFS.remove(SmeaConfig::Path::SYS_CFG_JSON_DEF);
    LittleFS.rename("/sys/config.tmp", SmeaConfig::Path::SYS_CFG_JSON_DEF);

    xSemaphoreGive(_lock);
    return true;
}

void T415_ConfigManager::resetToDefault() {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _loadDefaults();
    xSemaphoreGive(_lock);
    save();
}

DynamicConfig T415_ConfigManager::getConfig() {
    DynamicConfig v_copy;
    xSemaphoreTake(_lock, portMAX_DELAY);
    v_copy = _config; // 락 안에서 값 복사 (Race Condition 차단)
    xSemaphoreGive(_lock);
    return v_copy;
}

bool T415_ConfigManager::updateConfig(const DynamicConfig& p_newConfig) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _config = p_newConfig;
    xSemaphoreGive(_lock);
    return save();
}
