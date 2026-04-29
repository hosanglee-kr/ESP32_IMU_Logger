/* ============================================================================
 * File: T415_ConfigMgr_009.cpp
 * Summary: Dynamic JSON Configuration Manager Implementation
 * * [AI 메모: 마이그레이션 적용 사항]
 * 1. Web API의 Partial Update 로직 처리를 위한 _applyJson 모듈화 완료.
 * 2. 원자적 저장을 우회하지 않도록 updateFromJson 인터페이스 구현 완료.
 * 3. [Const-Correctness 방어]: ArduinoJson V7 환경에서 const JsonDocument를 
 * 다룰 때 발생하는 InvalidConversion 에러를 막기 위해 추출되는 모든 객체는 
 * 반드시 JsonObjectConst 및 JsonArrayConst로 취급한다.
 * ========================================================================== */
#include "T415_ConfigMgr_009.hpp"
#include "esp_log.h"

static const char* TAG = "T415_CFG";

T415_ConfigManager::T415_ConfigManager() {
    _lock = xSemaphoreCreateMutex();
    _isLoaded = false;
    _loadDefaults(); 
}

T415_ConfigManager::~T415_ConfigManager() {
    if (_lock) vSemaphoreDelete(_lock);
}

bool T415_ConfigManager::init() {
    if (!LittleFS.begin(true)) {
        ESP_LOGE(TAG, "LittleFS Mount Failed!");
        return false;
    }

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
    // 1. Dsp
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

    // 2. Feature
    _config.feature.band_rms_count = SmeaConfig::Feature::BAND_RMS_COUNT_DEF;
    for (int i = 0; i < SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST; i++) {
        _config.feature.band_ranges[i][0] = SmeaConfig::Feature::BAND_RANGES_DEF[i][0];
        _config.feature.band_ranges[i][1] = SmeaConfig::Feature::BAND_RANGES_DEF[i][1];
    }
    for (int i = 0; i < SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST; i++) {
        _config.feature.ceps_targets[i] = 0.0f; 
    }
    _config.feature.spatial_freq_min_hz = 100.0f;
    _config.feature.spatial_freq_max_hz = 4000.0f;

    // 3. Decision
    _config.decision.rule_enrg_threshold = SmeaConfig::Decision::RULE_ENRG_THRESHOLD_DEF;
    _config.decision.rule_stddev_threshold = SmeaConfig::Decision::RULE_STDDEV_THRESHOLD_DEF;
    _config.decision.test_ng_min_energy = SmeaConfig::Decision::TEST_NG_MIN_ENERGY_DEF;
    _config.decision.min_trigger_count = SmeaConfig::Decision::MIN_TRIGGER_COUNT_DEF;
    _config.decision.noise_profile_sec = SmeaConfig::Decision::NOISE_PROFILE_SEC_DEF;
    _config.decision.valid_start_sec = SmeaConfig::Decision::VALID_START_SEC_DEF;
    _config.decision.valid_end_sec = SmeaConfig::Decision::VALID_END_SEC_DEF;
    _config.decision.sta_lta_threshold = 3.0f;

    // 4. Storage
    _config.storage.pre_trigger_sec = SmeaConfig::Storage::PRE_TRIGGER_SEC_DEF;
    _config.storage.rotate_mb = SmeaConfig::Storage::ROTATE_MB_DEF;
    _config.storage.rotate_min = SmeaConfig::Storage::ROTATE_MIN_DEF;
    _config.storage.idle_flush_ms = SmeaConfig::Storage::IDLE_FLUSH_MS_DEF;

    // 5. Mqtt
    strlcpy(_config.mqtt.mqtt_broker, "", sizeof(_config.mqtt.mqtt_broker));
    _config.mqtt.retry_interval_ms = SmeaConfig::Mqtt::RETRY_INTERVAL_MS_DEF;
    _config.mqtt.default_port = SmeaConfig::Mqtt::DEFAULT_PORT_DEF;

    // 6. WiFi 
    _config.wifi.mode = SmeaConfig::Network::WIFI_MODE_DEF; 
    strlcpy(_config.wifi.ap_ssid, SmeaConfig::Network::AP_SSID_DEF, sizeof(_config.wifi.ap_ssid));
    strlcpy(_config.wifi.ap_password, SmeaConfig::Network::AP_PW_DEF, sizeof(_config.wifi.ap_password));
    strlcpy(_config.wifi.ap_ip, SmeaConfig::Network::AP_IP_DEF, sizeof(_config.wifi.ap_ip));
    
    // Multi-AP 비우기
    for (int i = 0; i < SmeaConfig::NetworkLimit::MAX_MULTI_AP_CONST; i++) {
        _config.wifi.multi_ap[i].ssid[0] = '\0';
        _config.wifi.multi_ap[i].password[0] = '\0';
        _config.wifi.multi_ap[i].use_static_ip = false;
        _config.wifi.multi_ap[i].local_ip[0] = '\0';
        _config.wifi.multi_ap[i].gateway[0] = '\0';
        _config.wifi.multi_ap[i].subnet[0] = '\0';
        _config.wifi.multi_ap[i].dns1[0] = '\0';
        _config.wifi.multi_ap[i].dns2[0] = '\0';
    }
}

// Const-Correctness 준수 (JsonObject -> JsonObjectConst 등)
void T415_ConfigManager::_applyJson(const JsonDocument& p_doc) {
    JsonObjectConst v_dsp = p_doc["dsp"];
    if (!v_dsp.isNull()) {
        _config.dsp.window_ms 			= v_dsp["window_ms"] 			| _config.dsp.window_ms;
        _config.dsp.hop_ms 				= v_dsp["hop_ms"] 				| _config.dsp.hop_ms;
        _config.dsp.notch_freq_hz 		= v_dsp["notch_freq_hz"] 		| _config.dsp.notch_freq_hz;
        _config.dsp.notch_freq_2_hz 	= v_dsp["notch_freq_2_hz"] 		| _config.dsp.notch_freq_2_hz;
        _config.dsp.notch_q_factor 		= v_dsp["notch_q_factor"] 		| _config.dsp.notch_q_factor;
        _config.dsp.pre_emphasis_alpha 	= v_dsp["pre_emphasis_alpha"] 	| _config.dsp.pre_emphasis_alpha;
        _config.dsp.beamforming_gain 	= v_dsp["beamforming_gain"] | _config.dsp.beamforming_gain;
        _config.dsp.fir_lpf_cutoff 		= v_dsp["fir_lpf_cutoff"] | _config.dsp.fir_lpf_cutoff;
        _config.dsp.fir_hpf_cutoff 		= v_dsp["fir_hpf_cutoff"] | _config.dsp.fir_hpf_cutoff;
        _config.dsp.median_window 		= v_dsp["median_window"] | _config.dsp.median_window;
        _config.dsp.noise_gate_thresh 	= v_dsp["noise_gate_thresh"] | _config.dsp.noise_gate_thresh;
        _config.dsp.noise_learn_frames 	= v_dsp["noise_learn_frames"] | _config.dsp.noise_learn_frames;
        _config.dsp.spectral_sub_gain 	= v_dsp["spectral_sub_gain"] | _config.dsp.spectral_sub_gain;
    }

    JsonObjectConst v_feature = p_doc["feature"];
    if (!v_feature.isNull()) {
        _config.feature.band_rms_count = v_feature["band_rms_count"] | _config.feature.band_rms_count;
        JsonArrayConst v_ranges = v_feature["band_ranges"];
        if (!v_ranges.isNull()) {
            int i = 0;
            for (JsonArrayConst v_band : v_ranges) {
                if (i >= SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST) break;
                _config.feature.band_ranges[i][0] = v_band[0] | _config.feature.band_ranges[i][0];
                _config.feature.band_ranges[i][1] = v_band[1] | _config.feature.band_ranges[i][1];
                i++;
            }
        }
        JsonArrayConst v_ceps = v_feature["ceps_targets"];
        if (!v_ceps.isNull()) {
            int i = 0;
            for (float v_val : v_ceps) {
                if (i >= SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST) break;
                _config.feature.ceps_targets[i] = v_val;
                i++;
            }
        }
        _config.feature.spatial_freq_min_hz = v_feature["spatial_freq_min_hz"] | _config.feature.spatial_freq_min_hz;
        _config.feature.spatial_freq_max_hz = v_feature["spatial_freq_max_hz"] | _config.feature.spatial_freq_max_hz;
    }

    JsonObjectConst v_decision = p_doc["decision"];
    if (!v_decision.isNull()) {
        _config.decision.rule_enrg_threshold = v_decision["rule_enrg_threshold"] | _config.decision.rule_enrg_threshold;
        _config.decision.rule_stddev_threshold = v_decision["rule_stddev_threshold"] | _config.decision.rule_stddev_threshold;
        _config.decision.test_ng_min_energy = v_decision["test_ng_min_energy"] | _config.decision.test_ng_min_energy;
        _config.decision.min_trigger_count = v_decision["min_trigger_count"] | _config.decision.min_trigger_count;
        _config.decision.noise_profile_sec = v_decision["noise_profile_sec"] | _config.decision.noise_profile_sec;
        _config.decision.valid_start_sec = v_decision["valid_start_sec"] | _config.decision.valid_start_sec;
        _config.decision.valid_end_sec = v_decision["valid_end_sec"] | _config.decision.valid_end_sec;
        _config.decision.sta_lta_threshold = v_decision["sta_lta_threshold"] | _config.decision.sta_lta_threshold;
    }

    JsonObjectConst v_storage = p_doc["storage"];
    if (!v_storage.isNull()) {
        _config.storage.pre_trigger_sec = v_storage["pre_trigger_sec"] | _config.storage.pre_trigger_sec;
        _config.storage.rotate_mb = v_storage["rotate_mb"] | _config.storage.rotate_mb;
        _config.storage.rotate_min = v_storage["rotate_min"] | _config.storage.rotate_min;
        _config.storage.idle_flush_ms = v_storage["idle_flush_ms"] | _config.storage.idle_flush_ms;
    }

    JsonObjectConst v_mqtt = p_doc["mqtt"];
    if (!v_mqtt.isNull()) {
        strlcpy(_config.mqtt.mqtt_broker, v_mqtt["mqtt_broker"] | _config.mqtt.mqtt_broker, sizeof(_config.mqtt.mqtt_broker));
        _config.mqtt.retry_interval_ms = v_mqtt["retry_interval_ms"] | _config.mqtt.retry_interval_ms;
        _config.mqtt.default_port = v_mqtt["default_port"] | _config.mqtt.default_port;
    }

    JsonObjectConst v_wifi = p_doc["wifi"];
    if (!v_wifi.isNull()) {
        _config.wifi.mode = v_wifi["mode"] | _config.wifi.mode;
        strlcpy(_config.wifi.ap_ssid, v_wifi["ap_ssid"] | _config.wifi.ap_ssid, SmeaConfig::NetworkLimit::MAX_SSID_LEN_CONST);
        strlcpy(_config.wifi.ap_password, v_wifi["ap_password"] | _config.wifi.ap_password, SmeaConfig::NetworkLimit::MAX_PW_LEN_CONST);
        strlcpy(_config.wifi.ap_ip, v_wifi["ap_ip"] | _config.wifi.ap_ip, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);

        JsonArrayConst v_multi = v_wifi["multi_ap"];
        if (!v_multi.isNull()) {
            int i = 0;
            for (JsonObjectConst v_ap : v_multi) {
                if (i >= SmeaConfig::NetworkLimit::MAX_MULTI_AP_CONST) break;
                strlcpy(_config.wifi.multi_ap[i].ssid, v_ap["ssid"] | _config.wifi.multi_ap[i].ssid, SmeaConfig::NetworkLimit::MAX_SSID_LEN_CONST);
                strlcpy(_config.wifi.multi_ap[i].password, v_ap["password"] | _config.wifi.multi_ap[i].password, SmeaConfig::NetworkLimit::MAX_PW_LEN_CONST);
                _config.wifi.multi_ap[i].use_static_ip = v_ap["use_static_ip"] | _config.wifi.multi_ap[i].use_static_ip;
                strlcpy(_config.wifi.multi_ap[i].local_ip, v_ap["local_ip"] | _config.wifi.multi_ap[i].local_ip, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);
                strlcpy(_config.wifi.multi_ap[i].gateway, v_ap["gateway"] | _config.wifi.multi_ap[i].gateway, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);
                strlcpy(_config.wifi.multi_ap[i].subnet, v_ap["subnet"] | _config.wifi.multi_ap[i].subnet, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);
                strlcpy(_config.wifi.multi_ap[i].dns1, v_ap["dns1"] | _config.wifi.multi_ap[i].dns1, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);
                strlcpy(_config.wifi.multi_ap[i].dns2, v_ap["dns2"] | _config.wifi.multi_ap[i].dns2, SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST);
                i++;
            }
        }
    }
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

    _applyJson(v_doc);

    xSemaphoreGive(_lock);
    return true;
}

// 문자열 JSON을 수신받아 안전하게 갱신 후 저장
bool T415_ConfigManager::updateFromJson(const char* p_jsonString) {
    JsonDocument v_doc;
    DeserializationError v_err = deserializeJson(v_doc, p_jsonString);
    
    if (v_err) {
        ESP_LOGE(TAG, "Update JSON Parse Error: %s", v_err.c_str());
        return false;
    }

    xSemaphoreTake(_lock, portMAX_DELAY);
    _applyJson(v_doc);
    xSemaphoreGive(_lock);

    return save(); // 변경된 최종 풀세트 구조체를 원자적으로 파일에 저장
}

bool T415_ConfigManager::save() {
    xSemaphoreTake(_lock, portMAX_DELAY);

    JsonDocument v_doc;

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

    JsonObject v_feature = v_doc["feature"].to<JsonObject>();
    v_feature["band_rms_count"] = _config.feature.band_rms_count;
    JsonArray v_ranges = v_feature["band_ranges"].to<JsonArray>();
    for (int i = 0; i < SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST; i++) {
        JsonArray v_band = v_ranges.add<JsonArray>();
        v_band.add(_config.feature.band_ranges[i][0]);
        v_band.add(_config.feature.band_ranges[i][1]);
    }
    JsonArray v_ceps = v_feature["ceps_targets"].to<JsonArray>();
    for (int i = 0; i < SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST; i++) {
        v_ceps.add(_config.feature.ceps_targets[i]);
    }
    v_feature["spatial_freq_min_hz"] = _config.feature.spatial_freq_min_hz;
    v_feature["spatial_freq_max_hz"] = _config.feature.spatial_freq_max_hz;

    JsonObject v_decision = v_doc["decision"].to<JsonObject>();
    v_decision["rule_enrg_threshold"] = _config.decision.rule_enrg_threshold;
    v_decision["rule_stddev_threshold"] = _config.decision.rule_stddev_threshold;
    v_decision["test_ng_min_energy"] = _config.decision.test_ng_min_energy;
    v_decision["min_trigger_count"] = _config.decision.min_trigger_count;
    v_decision["noise_profile_sec"] = _config.decision.noise_profile_sec;
    v_decision["valid_start_sec"] = _config.decision.valid_start_sec;
    v_decision["valid_end_sec"] = _config.decision.valid_end_sec;
    v_decision["sta_lta_threshold"] = _config.decision.sta_lta_threshold;

    JsonObject v_storage = v_doc["storage"].to<JsonObject>();
    v_storage["pre_trigger_sec"] = _config.storage.pre_trigger_sec;
    v_storage["rotate_mb"] = _config.storage.rotate_mb;
    v_storage["rotate_min"] = _config.storage.rotate_min;
    v_storage["idle_flush_ms"] = _config.storage.idle_flush_ms;

    JsonObject v_mqtt = v_doc["mqtt"].to<JsonObject>();
    v_mqtt["mqtt_broker"] = _config.mqtt.mqtt_broker;
    v_mqtt["retry_interval_ms"] = _config.mqtt.retry_interval_ms;
    v_mqtt["default_port"] = _config.mqtt.default_port;

    JsonObject v_wifi = v_doc["wifi"].to<JsonObject>();
    v_wifi["mode"] = _config.wifi.mode;
    v_wifi["ap_ssid"] = _config.wifi.ap_ssid;
    v_wifi["ap_password"] = _config.wifi.ap_password;
    v_wifi["ap_ip"] = _config.wifi.ap_ip;

    JsonArray v_multi = v_wifi["multi_ap"].to<JsonArray>();
    for (int i = 0; i < SmeaConfig::NetworkLimit::MAX_MULTI_AP_CONST; i++) {
        JsonObject v_ap = v_multi.add<JsonObject>();
        v_ap["ssid"] = _config.wifi.multi_ap[i].ssid;
        v_ap["password"] = _config.wifi.multi_ap[i].password;
        v_ap["use_static_ip"] = _config.wifi.multi_ap[i].use_static_ip;
        v_ap["local_ip"] = _config.wifi.multi_ap[i].local_ip;
        v_ap["gateway"] = _config.wifi.multi_ap[i].gateway;
        v_ap["subnet"] = _config.wifi.multi_ap[i].subnet;
        v_ap["dns1"] = _config.wifi.multi_ap[i].dns1;
        v_ap["dns2"] = _config.wifi.multi_ap[i].dns2;
    }

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
    v_copy = _config; 
    xSemaphoreGive(_lock);
    return v_copy;
}

bool T415_ConfigManager::updateConfig(const DynamicConfig& p_newConfig) {
    xSemaphoreTake(_lock, portMAX_DELAY);
    _config = p_newConfig;
    xSemaphoreGive(_lock);
    return save();
}
