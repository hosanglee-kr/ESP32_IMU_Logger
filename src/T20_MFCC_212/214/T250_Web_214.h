/* File: T250_Web_214.h */

#pragma once

#include "T221_Mfcc_Inter_214.h"

/*
namespace T20_WebCfg {
    // --- 1. 네트워크 및 경로 관련 ---
    const char* const BASE_API_PATH = "/api/t20";
    const char* const WS_ENDPOINT   = "/api/t20/ws";
    const char* const DEFAULT_INDEX = "/index_214_003.html";

    // --- 2. WebSocket 데이터 구조 (바이너리 패킷) ---
    const size_t WS_WAVEFORM_COUNT = 256;
    const size_t WS_SPECTRUM_COUNT = 129;
    const size_t WS_MFCC_COUNT     = 39;
    const size_t WS_TOTAL_FLOATS   = WS_WAVEFORM_COUNT + WS_SPECTRUM_COUNT + WS_MFCC_COUNT; // 424
    const size_t WS_BUFFER_SIZE    = WS_TOTAL_FLOATS * sizeof(float); // 1696 Bytes

    // --- 3. HTTP 응답 및 타임아웃 ---
    const uint32_t CACHE_CONTROL_MAX_AGE = 3600; // 1시간
    const uint16_t DEFAULT_PAGE_SIZE     = 20;
    const uint16_t MAX_PAGE_LIMIT        = 1000;

    // --- 4. JSON 응답 메시지 ---
    const char* const MSG_REC_STARTED    = "{\"ok\":true,\"msg\":\"recorder_started\"}";
    const char* const MSG_REC_STOPPED    = "{\"ok\":true,\"msg\":\"recorder_stopped\"}";
    const char* const MSG_ERR_PATH_REQ   = "path_required";
    const char* const MSG_ERR_NOT_FOUND  = "file_not_found";
}


*/

// API 핸들러 등록 함수
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

// 프론트엔드 정적 파일 서빙 전용 핸들러
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server);

void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);

