/* ============================================================================
 * File: T240_Comm_Service_217.h
 * Summary: Network & Web Communication Engine (v217)
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

#include "T217_Def_Net_217.h"



class CL_T20_CommService {
public:
    CL_T20_CommService();
    ~CL_T20_CommService();

    // 네트워크 초기화 및 서버 시작
    bool begin(const ST_T20_ConfigWiFi_t& w_cfg);
    
    // 마스터 객체 연결 (데이터 수집 및 설정을 위해 ST_Impl 참조)
    // 실제 구현에서는 전역 인스턴스 대신 의존성 주입을 사용
    void initHandlers(void* p_master_impl);

    // 실시간 바이너리 데이터 브로드캐스트 (WS)
    void broadcastBinary(const float* p_buffer, size_t len);

    // 시스템 상태 해시 계산 (v216 로직 유지)
    uint32_t calcStatusHash(uint32_t frame_id, uint32_t rec_count, float z_axis, bool measuring);

private:
    // 내부 WiFi 및 서버 객체
    WiFiMulti        _wifi_multi;
    AsyncWebServer   _server;
    AsyncWebSocket   _ws;

    // CORS 및 공통 응답 헬퍼
    void _setCorsHeaders(AsyncWebServerResponse* response);
    void _sendJson(AsyncWebServerRequest* request, const JsonDocument& doc);
};
