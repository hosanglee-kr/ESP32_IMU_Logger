/* File: T250_Web_214.h */

#pragma once

#include "T221_Mfcc_Inter_214.h"

// ----------------------------------------------------------------------------
// [v215] 고정 상수 정의 (gnu++17 constexpr 활용)
// ----------------------------------------------------------------------------
namespace T250_Web {
    // Mime Types
    constexpr const char* MIME_JSON   = "application/json; charset=utf-8";
    constexpr const char* MIME_HTML   = "text/html";
    constexpr const char* MIME_PLAIN  = "text/plain";
    constexpr const char* MIME_OCTET  = "application/octet-stream";

    // Frontend Static Assets
    constexpr const char* FILE_INDEX_HTML = "/index_214_003.html";
    constexpr const char* CACHE_CONTROL   = "max-age=3600";

    // WebSocket Path
    constexpr const char* ROUTE_WS = "/api/t20/ws";

    // API Routes (Suffixes)
    constexpr const char* ROUTE_REC_BEGIN         = "/recorder_begin";
    constexpr const char* ROUTE_REC_END           = "/recorder_end";
    constexpr const char* ROUTE_REC_FINALIZE      = "/recorder_finalize";
    constexpr const char* ROUTE_REC_FIN_PIPE      = "/recorder_finalize_pipeline";
    constexpr const char* ROUTE_BMI_ACTUAL        = "/bmi270_actual_state";
    constexpr const char* ROUTE_BMI_BRIDGE        = "/bmi270_bridge_state";
    constexpr const char* ROUTE_BMI_VERIFY        = "/bmi270_verify_state";
    constexpr const char* ROUTE_TYPE_PREV_LOAD    = "/type_preview_load";
    constexpr const char* ROUTE_REC_CSV_ADV       = "/recorder/file_csv_table_advanced";
    constexpr const char* ROUTE_DSP_NOISE         = "/dsp/noise";
    constexpr const char* ROUTE_LIVE_DEBUG        = "/live_debug";
    constexpr const char* ROUTE_REC_DOWNLOAD      = "/recorder/download";
    constexpr const char* ROUTE_NOISE_LEARN       = "/noise_learn";
    constexpr const char* ROUTE_VIEWER_DATA       = "/viewer_data";
    constexpr const char* ROUTE_STATUS            = "/status";
    constexpr const char* ROUTE_BUILD_SANITY      = "/build_sanity";

    // Common JSON Responses
    constexpr const char* JSON_RES_FAIL = "{\"ok\":false}";
    constexpr const char* JSON_RES_OK   = "{\"ok\":true}";
}



// API 핸들러 등록 함수
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

// 프론트엔드 정적 파일 서빙 전용 핸들러
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server);

void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);

