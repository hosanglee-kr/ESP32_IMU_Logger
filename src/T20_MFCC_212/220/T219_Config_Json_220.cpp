
/* ============================================================================
 * File: T219_Config_Json_220.cpp
 * Summary: System Configuration JSON Serialization Implementation
 * ========================================================================== */
#include "T219_Config_Json_220.h"

bool CL_T20_ConfigJson::parseFromJson(const char* json_str, ST_T20_Config_t& out_cfg) {
    if (!json_str) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json_str);
    if (error) return false;

    return parseFromJson(doc, out_cfg);
}

bool CL_T20_ConfigJson::parseFromJson(const JsonDocument& doc, ST_T20_Config_t& out_cfg) {
    // V7.4.x 규격: const JsonDocument에서 값을 읽을 때는 JsonObjectConst 사용

    // 1. Sensor
    JsonObjectConst s = doc["sensor"];
    if (s) {
        out_cfg.sensor.axis        = (EM_T20_SensorAxis_t)(s["axis"] | out_cfg.sensor.axis);
        out_cfg.sensor.accel_range = (EM_T20_AccelRange_t)(s["accel_range"] | out_cfg.sensor.accel_range);
        out_cfg.sensor.gyro_range  = (EM_T20_GyroRange_t)(s["gyro_range"] | out_cfg.sensor.gyro_range);
    }

    // 2. DSP (Preprocess)
    JsonObjectConst d = doc["dsp"];
    if (d) {
        out_cfg.preprocess.remove_dc = d["remove_dc"] | out_cfg.preprocess.remove_dc;

        JsonObjectConst flt = d["filter"];
        if (flt) {
            out_cfg.preprocess.filter.enable      = flt["enable"] | out_cfg.preprocess.filter.enable;
            out_cfg.preprocess.filter.type        = (EM_T20_FilterType_t)(flt["type"] | out_cfg.preprocess.filter.type);
            out_cfg.preprocess.filter.cutoff_hz_1 = flt["cutoff_hz_1"] | out_cfg.preprocess.filter.cutoff_hz_1;
            out_cfg.preprocess.filter.q_factor    = flt["q_factor"] | out_cfg.preprocess.filter.q_factor;
        }

        JsonObjectConst pre = d["preemphasis"];
        if (pre) {
            out_cfg.preprocess.preemphasis.enable = pre["enable"] | out_cfg.preprocess.preemphasis.enable;
            out_cfg.preprocess.preemphasis.alpha  = pre["alpha"] | out_cfg.preprocess.preemphasis.alpha;
        }

        JsonObjectConst nz = d["noise"];
        if (nz) {
            out_cfg.preprocess.noise.enable_gate                = nz["enable_gate"] | out_cfg.preprocess.noise.enable_gate;
            out_cfg.preprocess.noise.gate_threshold_abs         = nz["gate_threshold_abs"] | out_cfg.preprocess.noise.gate_threshold_abs;
            out_cfg.preprocess.noise.mode                       = (EM_T20_NoiseMode_t)(nz["mode"] | out_cfg.preprocess.noise.mode);
            out_cfg.preprocess.noise.spectral_subtract_strength = nz["spectral_subtract_strength"] | out_cfg.preprocess.noise.spectral_subtract_strength;
            out_cfg.preprocess.noise.adaptive_alpha             = nz["adaptive_alpha"] | out_cfg.preprocess.noise.adaptive_alpha;
            out_cfg.preprocess.noise.noise_learn_frames         = nz["noise_learn_frames"] | out_cfg.preprocess.noise.noise_learn_frames;
        }
    }

    // 3. Feature
    JsonObjectConst f = doc["feature"];
    if (f) {
        out_cfg.feature.hop_size    = f["hop_size"] | out_cfg.feature.hop_size;
        out_cfg.feature.mfcc_coeffs = f["mfcc_coeffs"] | out_cfg.feature.mfcc_coeffs;
        out_cfg.feature.fft_size    = (EM_T20_FftSize_t)(f["fft_size"] | (int)out_cfg.feature.fft_size);
        out_cfg.feature.axis_count  = (EM_T20_AxisCount_t)(f["axis_count"] | (int)out_cfg.feature.axis_count);
    }

    // 4. Output
    JsonObjectConst o = doc["output"];
    if (o) {
        out_cfg.output.enabled         = o["enabled"] | out_cfg.output.enabled;
        out_cfg.output.output_sequence = o["output_sequence"] | out_cfg.output.output_sequence;
        out_cfg.output.sequence_frames = o["sequence_frames"] | out_cfg.output.sequence_frames;
    }

    // 5. Storage
    JsonObjectConst st = doc["storage"];
    if (st) {
        out_cfg.storage.rotation_mb     = st["rotation_mb"] | out_cfg.storage.rotation_mb;
        out_cfg.storage.rotation_min    = st["rotation_min"] | out_cfg.storage.rotation_min;
        out_cfg.storage.save_raw        = st["save_raw"] | out_cfg.storage.save_raw;
        out_cfg.storage.rotate_keep_max = st["rotate_keep_max"] | out_cfg.storage.rotate_keep_max;
        out_cfg.storage.idle_flush_ms   = st["idle_flush_ms"] | out_cfg.storage.idle_flush_ms;
    }

    // 6. Trigger (HW Power & SW Event 분리)
    JsonObjectConst tr = doc["trigger"];
    if (tr) {
        JsonObjectConst hw = tr["hw_power"];
        if (hw) {
            out_cfg.trigger.hw_power.use_deep_sleep    = hw["use_deep_sleep"] | out_cfg.trigger.hw_power.use_deep_sleep;
            out_cfg.trigger.hw_power.sleep_timeout_sec = hw["sleep_timeout_sec"] | out_cfg.trigger.hw_power.sleep_timeout_sec;
            out_cfg.trigger.hw_power.wake_threshold_g  = hw["wake_threshold_g"] | out_cfg.trigger.hw_power.wake_threshold_g;
            out_cfg.trigger.hw_power.duration_x20ms    = hw["duration_x20ms"] | out_cfg.trigger.hw_power.duration_x20ms;
        }

        JsonObjectConst sw = tr["sw_event"];
        if (sw) {
            out_cfg.trigger.sw_event.hold_time_ms        = sw["hold_time_ms"] | out_cfg.trigger.sw_event.hold_time_ms;
            out_cfg.trigger.sw_event.use_rms             = sw["use_rms"] | out_cfg.trigger.sw_event.use_rms;
            out_cfg.trigger.sw_event.rms_threshold_power = sw["rms_threshold_power"] | out_cfg.trigger.sw_event.rms_threshold_power;

            JsonArrayConst arr = sw["bands"];
            if (arr) {
                for (int i = 0; i < T20::C10_DSP::TRIGGER_BANDS_MAX && i < arr.size(); i++) {
                    out_cfg.trigger.sw_event.bands[i].enable    = arr[i]["enable"] | out_cfg.trigger.sw_event.bands[i].enable;
                    out_cfg.trigger.sw_event.bands[i].start_hz  = arr[i]["start_hz"] | out_cfg.trigger.sw_event.bands[i].start_hz;
                    out_cfg.trigger.sw_event.bands[i].end_hz    = arr[i]["end_hz"] | out_cfg.trigger.sw_event.bands[i].end_hz;
                    out_cfg.trigger.sw_event.bands[i].threshold = arr[i]["threshold"] | out_cfg.trigger.sw_event.bands[i].threshold;
                }
            }
        }
    }

    // === 7. WiFi ===
    JsonObjectConst wf = doc["wifi"];
    if (wf) {
        out_cfg.wifi.mode = (EM_T20_WiFiMode_t)(wf["mode"] | out_cfg.wifi.mode);
        strlcpy(out_cfg.wifi.ap_ssid, wf["ap_ssid"] | out_cfg.wifi.ap_ssid, sizeof(out_cfg.wifi.ap_ssid));
        strlcpy(out_cfg.wifi.ap_password, wf["ap_password"] | out_cfg.wifi.ap_password, sizeof(out_cfg.wifi.ap_password));
        strlcpy(out_cfg.wifi.ap_ip, wf["ap_ip"] | out_cfg.wifi.ap_ip, sizeof(out_cfg.wifi.ap_ip));

        JsonArrayConst multi = wf["multi_ap"];
        if (multi) {
            for (int i = 0; i < T20::C10_Net::WIFI_MULTI_MAX && i < multi.size(); i++) {
                strlcpy(out_cfg.wifi.multi_ap[i].ssid, multi[i]["ssid"] | out_cfg.wifi.multi_ap[i].ssid, sizeof(out_cfg.wifi.multi_ap[i].ssid));
                strlcpy(out_cfg.wifi.multi_ap[i].password, multi[i]["password"] | out_cfg.wifi.multi_ap[i].password, sizeof(out_cfg.wifi.multi_ap[i].password));
                out_cfg.wifi.multi_ap[i].use_static_ip = multi[i]["use_static_ip"] | out_cfg.wifi.multi_ap[i].use_static_ip;
                strlcpy(out_cfg.wifi.multi_ap[i].local_ip, multi[i]["local_ip"] | out_cfg.wifi.multi_ap[i].local_ip, sizeof(out_cfg.wifi.multi_ap[i].local_ip));
                strlcpy(out_cfg.wifi.multi_ap[i].gateway, multi[i]["gateway"] | out_cfg.wifi.multi_ap[i].gateway, sizeof(out_cfg.wifi.multi_ap[i].gateway));
                strlcpy(out_cfg.wifi.multi_ap[i].subnet, multi[i]["subnet"] | out_cfg.wifi.multi_ap[i].subnet, sizeof(out_cfg.wifi.multi_ap[i].subnet));
                strlcpy(out_cfg.wifi.multi_ap[i].dns1, multi[i]["dns1"] | out_cfg.wifi.multi_ap[i].dns1, sizeof(out_cfg.wifi.multi_ap[i].dns1));
                strlcpy(out_cfg.wifi.multi_ap[i].dns2, multi[i]["dns2"] | out_cfg.wifi.multi_ap[i].dns2, sizeof(out_cfg.wifi.multi_ap[i].dns2));
            }
        }
    }

    // 8. MQTT
    JsonObjectConst mq = doc["mqtt"];
    if (mq) {
        out_cfg.mqtt.enable = mq["enable"] | out_cfg.mqtt.enable;
        strlcpy(out_cfg.mqtt.broker, mq["broker"] | out_cfg.mqtt.broker, sizeof(out_cfg.mqtt.broker));
        out_cfg.mqtt.port = mq["port"] | out_cfg.mqtt.port;
        strlcpy(out_cfg.mqtt.topic_root, mq["topic_root"] | out_cfg.mqtt.topic_root, sizeof(out_cfg.mqtt.topic_root));
    }

    // 9. System
    JsonObjectConst sys = doc["system"];
    if (sys) {
        out_cfg.system.auto_start  = sys["auto_start"] | out_cfg.system.auto_start;
        out_cfg.system.watchdog_ms = sys["watchdog_ms"] | out_cfg.system.watchdog_ms;
        out_cfg.system.button_pin  = sys["button_pin"] | out_cfg.system.button_pin;
    }

    return true;
}

void CL_T20_ConfigJson::buildJson(const ST_T20_Config_t& cfg, JsonDocument& out_doc) {
    out_doc.clear();

    JsonObject s       = out_doc["sensor"].to<JsonObject>();
    s["axis"]          = (int)cfg.sensor.axis;
    s["accel_range"]   = (int)cfg.sensor.accel_range;
    s["gyro_range"]    = (int)cfg.sensor.gyro_range;

    JsonObject d       = out_doc["dsp"].to<JsonObject>();
    d["remove_dc"]     = cfg.preprocess.remove_dc;

    JsonObject pre = d["preemphasis"].to<JsonObject>();
    pre["enable"]  = cfg.preprocess.preemphasis.enable;
    pre["alpha"]   = cfg.preprocess.preemphasis.alpha;

    JsonObject flt     = d["filter"].to<JsonObject>();
    flt["enable"]      = cfg.preprocess.filter.enable;
    flt["type"]        = (int)cfg.preprocess.filter.type;
    flt["cutoff_hz_1"] = cfg.preprocess.filter.cutoff_hz_1;
    flt["q_factor"]    = cfg.preprocess.filter.q_factor;

    JsonObject nz                    = d["noise"].to<JsonObject>();
    nz["enable_gate"]                = cfg.preprocess.noise.enable_gate;
    nz["gate_threshold_abs"]         = cfg.preprocess.noise.gate_threshold_abs;
    nz["mode"]                       = (int)cfg.preprocess.noise.mode;
    nz["spectral_subtract_strength"] = cfg.preprocess.noise.spectral_subtract_strength;
    nz["adaptive_alpha"]             = cfg.preprocess.noise.adaptive_alpha;
    nz["noise_learn_frames"]         = cfg.preprocess.noise.noise_learn_frames;

    JsonObject f       = out_doc["feature"].to<JsonObject>();
    f["hop_size"]      = cfg.feature.hop_size;
    f["mfcc_coeffs"]   = cfg.feature.mfcc_coeffs;
    f["fft_size"]      = (int)cfg.feature.fft_size;
    f["axis_count"]    = (int)cfg.feature.axis_count;

    JsonObject o             = out_doc["output"].to<JsonObject>();
    o["enabled"]             = cfg.output.enabled;
    o["output_sequence"]     = cfg.output.output_sequence;
    o["sequence_frames"]     = cfg.output.sequence_frames;

    // === WiFi ===
    JsonObject wf       = out_doc["wifi"].to<JsonObject>();
    wf["mode"]          = (int)cfg.wifi.mode;
    wf["ap_ssid"]       = cfg.wifi.ap_ssid;
    wf["ap_password"]   = cfg.wifi.ap_password;
    wf["ap_ip"]         = cfg.wifi.ap_ip;

    JsonObject st           = out_doc["storage"].to<JsonObject>();
    st["rotation_mb"]       = cfg.storage.rotation_mb;
    st["rotation_min"]      = cfg.storage.rotation_min;
    st["save_raw"]          = cfg.storage.save_raw;
    st["rotate_keep_max"]   = cfg.storage.rotate_keep_max;
    st["idle_flush_ms"]     = cfg.storage.idle_flush_ms;


    // t(HW Power & SW Event 분리 적용) ===
    JsonObject tr = out_doc["trigger"].to<JsonObject>();
    
    JsonObject hw = tr["hw_power"].to<JsonObject>();
    hw["use_deep_sleep"]    = cfg.trigger.hw_power.use_deep_sleep;
    hw["sleep_timeout_sec"] = cfg.trigger.hw_power.sleep_timeout_sec;
    hw["wake_threshold_g"]  = cfg.trigger.hw_power.wake_threshold_g;
    hw["duration_x20ms"]    = cfg.trigger.hw_power.duration_x20ms;

    JsonObject sw = tr["sw_event"].to<JsonObject>();
    sw["hold_time_ms"]        = cfg.trigger.sw_event.hold_time_ms;
    sw["use_rms"]             = cfg.trigger.sw_event.use_rms;
    sw["rms_threshold_power"] = cfg.trigger.sw_event.rms_threshold_power;

    JsonArray arr = sw["bands"].to<JsonArray>();
    for (int i = 0; i < T20::C10_DSP::TRIGGER_BANDS_MAX; i++) {
        JsonObject b   = arr.add<JsonObject>();
        b["enable"]    = cfg.trigger.sw_event.bands[i].enable;
        b["start_hz"]  = cfg.trigger.sw_event.bands[i].start_hz;
        b["end_hz"]    = cfg.trigger.sw_event.bands[i].end_hz;
        b["threshold"] = cfg.trigger.sw_event.bands[i].threshold;
    }
    

    JsonArray multi = wf["multi_ap"].to<JsonArray>();
    for (int i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
        JsonObject ap = multi.add<JsonObject>();
        ap["ssid"]          = cfg.wifi.multi_ap[i].ssid;
        ap["password"]      = cfg.wifi.multi_ap[i].password;
        ap["use_static_ip"] = cfg.wifi.multi_ap[i].use_static_ip;
        ap["local_ip"]      = cfg.wifi.multi_ap[i].local_ip;
        ap["gateway"]       = cfg.wifi.multi_ap[i].gateway;
        ap["subnet"]        = cfg.wifi.multi_ap[i].subnet;
        ap["dns1"]          = cfg.wifi.multi_ap[i].dns1;
        ap["dns2"]          = cfg.wifi.multi_ap[i].dns2;
    }

    JsonObject mq      = out_doc["mqtt"].to<JsonObject>();
    mq["enable"]       = cfg.mqtt.enable;
    mq["broker"]       = cfg.mqtt.broker;
    mq["port"]         = cfg.mqtt.port;
    mq["topic_root"]   = cfg.mqtt.topic_root;


    JsonObject sys         = out_doc["system"].to<JsonObject>();
    sys["auto_start"]      = cfg.system.auto_start;
    sys["watchdog_ms"]     = cfg.system.watchdog_ms;
    sys["button_pin"]      = cfg.system.button_pin;
}

void CL_T20_ConfigJson::buildJsonString(const ST_T20_Config_t& cfg, String& out_str) {
    JsonDocument doc;
    buildJson(cfg, doc);
    serializeJson(doc, out_str);
}
