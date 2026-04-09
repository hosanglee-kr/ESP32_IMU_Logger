/* ============================================================================
 * File: T219_Config_Json_218.cpp
 * Summary: System Configuration JSON Serialization Implementation (v218)
 * ========================================================================== */
#include "T219_Config_Json_218.h"

bool CL_T20_ConfigJson::parseFromJson(const char* json_str, ST_T20_Config_t& out_cfg) {
	if (!json_str) return false;

	JsonDocument		 doc;
	DeserializationError error = deserializeJson(doc, json_str);
	if (error) return false;

	return parseFromJson(doc, out_cfg);
}

bool CL_T20_ConfigJson::parseFromJson(const JsonDocument& doc, ST_T20_Config_t& out_cfg) {
	// const JsonDocument에서 값을 읽을 때는 반드시 JsonObjectConst를 사용해야 합니다.

	// 1. Sensor
	JsonObjectConst s = doc["sensor"];
	if (s) {
		out_cfg.sensor.axis		   = (EM_T20_SensorAxis_t)(s["axis"] | out_cfg.sensor.axis);
		out_cfg.sensor.accel_range = (EM_T20_AccelRange_t)(s["accel_range"] | out_cfg.sensor.accel_range);
		out_cfg.sensor.gyro_range  = (EM_T20_GyroRange_t)(s["gyro_range"] | out_cfg.sensor.gyro_range);
	}

	// 2. DSP (Preprocess)
	JsonObjectConst d = doc["dsp"];
	if (d) {
		out_cfg.preprocess.remove_dc = d["remove_dc"] | out_cfg.preprocess.remove_dc;

		JsonObjectConst flt			 = d["filter"];
		if (flt) {
			out_cfg.preprocess.filter.enable	  = flt["enable"] | out_cfg.preprocess.filter.enable;
			out_cfg.preprocess.filter.type		  = (EM_T20_FilterType_t)(flt["type"] | out_cfg.preprocess.filter.type);
			out_cfg.preprocess.filter.cutoff_hz_1 = flt["cutoff_hz_1"] | out_cfg.preprocess.filter.cutoff_hz_1;
			out_cfg.preprocess.filter.q_factor	  = flt["q_factor"] | out_cfg.preprocess.filter.q_factor;
		}

		JsonObjectConst nz = d["noise"];
		if (nz) {
			out_cfg.preprocess.noise.enable_gate				= nz["enable_gate"] | out_cfg.preprocess.noise.enable_gate;
			out_cfg.preprocess.noise.gate_threshold_abs			= nz["gate_threshold_abs"] | out_cfg.preprocess.noise.gate_threshold_abs;
			out_cfg.preprocess.noise.mode						= (EM_T20_NoiseMode_t)(nz["mode"] | out_cfg.preprocess.noise.mode);
			out_cfg.preprocess.noise.spectral_subtract_strength = nz["spectral_subtract_strength"] | out_cfg.preprocess.noise.spectral_subtract_strength;
		}
	}

	// 3. Output & Feature
	JsonObjectConst o = doc["output"];
	if (o) {
		out_cfg.output.enabled		   = o["enabled"] | out_cfg.output.enabled;
		out_cfg.output.output_sequence = o["output_sequence"] | out_cfg.output.output_sequence;
		out_cfg.output.sequence_frames = o["sequence_frames"] | out_cfg.output.sequence_frames;
	}

	JsonObjectConst f = doc["feature"];
	if (f) {
		out_cfg.feature.hop_size	= f["hop_size"] | out_cfg.feature.hop_size;
		out_cfg.feature.mfcc_coeffs = f["mfcc_coeffs"] | out_cfg.feature.mfcc_coeffs;
	}

	// 4. Storage
	JsonObjectConst st = doc["storage"];
	if (st) {
		out_cfg.storage.rotation_mb	 = st["rotation_mb"] | out_cfg.storage.rotation_mb;
		out_cfg.storage.rotation_min = st["rotation_min"] | out_cfg.storage.rotation_min;
		out_cfg.storage.save_raw	 = st["save_raw"] | out_cfg.storage.save_raw;
	}

	// 5. Trigger
	JsonObjectConst tr = doc["trigger"];
	if (tr) {
		out_cfg.trigger.use_threshold	  = tr["use_threshold"] | out_cfg.trigger.use_threshold;
		out_cfg.trigger.threshold_rms	  = tr["threshold_rms"] | out_cfg.trigger.threshold_rms;
		out_cfg.trigger.use_deep_sleep	  = tr["use_deep_sleep"] | out_cfg.trigger.use_deep_sleep;
		out_cfg.trigger.sleep_timeout_sec = tr["sleep_timeout_sec"] | out_cfg.trigger.sleep_timeout_sec;
	}

	// 6. System
	JsonObjectConst sys = doc["system"];
	if (sys) {
		out_cfg.system.auto_start = sys["auto_start"] | out_cfg.system.auto_start;
	}

	return true;
}

void CL_T20_ConfigJson::buildJson(const ST_T20_Config_t& cfg, JsonDocument& out_doc) {
	out_doc.clear();

	// 쓰기 작업이므로 JsonObject를 그대로 사용합니다.
	JsonObject s					 = out_doc["sensor"].to<JsonObject>();
	s["axis"]						 = (int)cfg.sensor.axis;
	s["accel_range"]				 = (int)cfg.sensor.accel_range;
	s["gyro_range"]					 = (int)cfg.sensor.gyro_range;

	JsonObject d					 = out_doc["dsp"].to<JsonObject>();
	d["remove_dc"]					 = cfg.preprocess.remove_dc;

	JsonObject flt					 = d["filter"].to<JsonObject>();
	flt["enable"]					 = cfg.preprocess.filter.enable;
	flt["type"]						 = (int)cfg.preprocess.filter.type;
	flt["cutoff_hz_1"]				 = cfg.preprocess.filter.cutoff_hz_1;
	flt["q_factor"]					 = cfg.preprocess.filter.q_factor;

	JsonObject nz					 = d["noise"].to<JsonObject>();
	nz["enable_gate"]				 = cfg.preprocess.noise.enable_gate;
	nz["gate_threshold_abs"]		 = cfg.preprocess.noise.gate_threshold_abs;
	nz["mode"]						 = (int)cfg.preprocess.noise.mode;
	nz["spectral_subtract_strength"] = cfg.preprocess.noise.spectral_subtract_strength;

	JsonObject f					 = out_doc["feature"].to<JsonObject>();
	f["hop_size"]					 = cfg.feature.hop_size;
	f["mfcc_coeffs"]				 = cfg.feature.mfcc_coeffs;

	JsonObject o					 = out_doc["output"].to<JsonObject>();
	o["enabled"]					 = cfg.output.enabled;
	o["output_sequence"]			 = cfg.output.output_sequence;
	o["sequence_frames"]			 = cfg.output.sequence_frames;

	JsonObject st					 = out_doc["storage"].to<JsonObject>();
	st["rotation_mb"]				 = cfg.storage.rotation_mb;
	st["rotation_min"]				 = cfg.storage.rotation_min;
	st["save_raw"]					 = cfg.storage.save_raw;

	JsonObject tr					 = out_doc["trigger"].to<JsonObject>();
	tr["use_threshold"]				 = cfg.trigger.use_threshold;
	tr["threshold_rms"]				 = cfg.trigger.threshold_rms;
	tr["use_deep_sleep"]			 = cfg.trigger.use_deep_sleep;
	tr["sleep_timeout_sec"]			 = cfg.trigger.sleep_timeout_sec;

	JsonObject sys					 = out_doc["system"].to<JsonObject>();
	sys["auto_start"]				 = cfg.system.auto_start;
}

void CL_T20_ConfigJson::buildJsonString(const ST_T20_Config_t& cfg, String& out_str) {
	JsonDocument doc;
	buildJson(cfg, doc);
	serializeJson(doc, out_str);
}
