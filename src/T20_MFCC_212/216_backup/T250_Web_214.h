/* File: T250_Web_214.h */

#pragma once

#include <AsyncJson.h>
#include "T221_Mfcc_Inter_215.h"

/* ----------------------------------------------------------------------------
 * 전용 상수 정의 (C++17 inline constexpr 활용)
 * ---------------------------------------------------------------------------- */
namespace T250_Web {
    // 배열 크기 상수
    inline constexpr size_t WAVEFORM_LEN   = 256U;
    inline constexpr size_t SPECTRUM_LEN   = 129U;
    inline constexpr size_t MFCC_LEN       = 39U;
    inline constexpr size_t BINARY_BUF_LEN = WAVEFORM_LEN + SPECTRUM_LEN + MFCC_LEN; // 424U

    // 해시 연산 상수 (FNV-1a 알고리즘 기반)
    inline constexpr uint32_t HASH_OFFSET_BASIS = 2166136261UL;
    inline constexpr uint32_t HASH_PRIME        = 16777619UL;
    inline constexpr uint32_t MEASURE_FLAG_BIT  = 0x80000000UL;

    // HTTP MIME 및 공통 텍스트
    inline constexpr const char* MIME_JSON  = "application/json; charset=utf-8";
    inline constexpr const char* MIME_OCTET = "application/octet-stream";
    inline constexpr const char* MIME_HTML  = "text/html";
    inline constexpr const char* MIME_TEXT  = "text/plain";
    
    inline constexpr const char* JSON_OK    = "{\"ok\":true}";
    inline constexpr const char* JSON_FAIL  = "{\"ok\":false}";
    
    // 정적 파일 서빙 설정
    inline constexpr const char* INDEX_FILE = "index_214_003.html";
    inline constexpr const char* CACHE_CTRL = "max-age=3600";
    inline constexpr const char* WS_URI     = "/api/t20/ws";
}



// API 핸들러 등록 함수
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

// 프론트엔드 정적 파일 서빙 전용 핸들러
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server);

void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);

