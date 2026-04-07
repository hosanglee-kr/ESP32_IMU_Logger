/* ============================================================================
 * File: T240_Comm_Service_217.h
 * Summary: Network & Web Communication Engine (v217 Full Version)
 * Description: v216의 모든 REST API, 파일 스트리밍, CORS, WebSocket 완벽 복원
 * ========================================================================== */
#pragma once

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "T210_Def_Com_217.h"
#include "T214_Def_Rec_217.h" 

class CL_T20_CommService {
public:
    CL_T20_CommService();
    ~CL_T20_CommService();

    // 네트워크 초기화 및 서버 바인딩
    bool begin(const ST_T20_ConfigWiFi_t& w_cfg);
    
    // REST API 및 WebSocket 핸들러 라우팅 (v216 누락분 100% 복원)
    void initHandlers(void* p_master_impl);

    // 실시간 바이너리 데이터 브로드캐스트 (WS, OOM 방어 적용)
    void broadcastBinary(const float* p_buffer, size_t len);

    // 상태 확인 유틸리티
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    uint32_t calcStatusHash(uint32_t frame_id, uint32_t rec_count, bool measuring);

private:
    // CORS 및 JSON 응답 헬퍼
    void _setCorsHeaders(AsyncWebServerResponse* response);
    void _sendJson(AsyncWebServerRequest* request, const JsonDocument& doc);

private:
    WiFiMulti        _wifi_multi;
    AsyncWebServer   _server;
    AsyncWebSocket   _ws;
};
