/* ============================================================================
 * File: T219_Config_Json_218.h
 * Summary: System Configuration JSON Serialization (v218)
 * Dependencies: ArduinoJson V7.4.x
 * ========================================================================== */
#pragma once

#include <ArduinoJson.h>

#include "T214_Def_Rec_218.h"

class CL_T20_ConfigJson {
   public:
	// JSON 문자열을 구조체로 파싱 (기존 설정값을 덮어씀)
	static bool parseFromJson(const char* json_str, ST_T20_Config_t& out_cfg);
	static bool parseFromJson(const JsonDocument& doc, ST_T20_Config_t& out_cfg);

	// 구조체를 JSON 문자열로 변환
	static void buildJson(const ST_T20_Config_t& cfg, JsonDocument& out_doc);
	static void buildJsonString(const ST_T20_Config_t& cfg, String& out_str);
};
