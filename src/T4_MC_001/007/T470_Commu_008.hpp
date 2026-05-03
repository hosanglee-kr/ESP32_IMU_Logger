/* ============================================================================
 * File: T470_Commu_008.hpp
 * Summary: Network, Web, MQTT & OTA Communication Engine (FSM Synchronized)
 * * [AI 메모: 제공 기능 요약]
 * 1. WiFi 연결 관리 (AP 단독, STA, Auto-Fallback 다중 AP 순회 접속).
 * 2. NTP 시간 동기화 (KST 타임존 적용).
 * 3. ESPAsyncWebServer 기반 비동기 REST API (설정, 제어, 파일 스트리밍).
 * 4. AsyncWebSocket 기반 실시간 파형/스펙트럼/MFCC 바이너리 브로드캐스트.
 * 5. MQTT 발행(Publish) 및 자동 재접속 유지보수.
 * 6. OTA(Over-The-Air) 무선 펌웨어 업데이트 지원.
 * ========================================================================== */
#pragma once

#include "T410_Def_009.hpp"
#include "T420_Types_010.hpp"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

class T470_Communicator {
private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;
    WiFiClient     _wifiClient;
    PubSubClient   _mqttClient;

    uint32_t _lastMqttRetryMs = 0;
    uint32_t _lastWifiRetryMs = 0; 
    
    struct MqttCredentials {
        char broker[SmeaConfig::NetworkLimit::MAX_BROKER_LEN_CONST];
        bool enable;
        uint16_t port;
    } _mqttCreds;

    bool _isOtaRunning = false; 

public:
    T470_Communicator();
    ~T470_Communicator();

    bool init(const char* p_ssid = "", const char* p_pw = "", const char* p_mqttBroker = "");
    void runNetwork();
    void broadcastBinary(const void* p_buffer, size_t p_bytes);
    bool publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result);
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

private:
    void _initWebHandlers();
    void _setCorsHeaders(AsyncWebServerResponse* p_response);
    void _sendJsonResponse(AsyncWebServerRequest* p_request, const JsonDocument& p_doc);
    void _reconnectMqtt();
};

