/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. [방어] Generic int 남용 금지: 임베디드 환경의 부호 확장 오버헤드와 
 * 메모리 파편화를 막기 위해 모든 크기/인덱스는 <cstdint> 고정 길이 정수형을 사용한다.
 * 2. [비동기 보호]: AsyncWebServer 및 AsyncWebSocket의 콜백(Callback) 내부에서 
 * 하드웨어 제어 블로킹 함수를 직접 호출하지 않고, 반드시 T450_FsmManager의 
 * dispatchCommand() 큐로 위임하여 Watchdog 패닉을 방어한다.
 * 3. [v012 고도화]: R-NMG (Resilient Network & Memory Guard)
 * - WebSocket 큐 사이즈 검사 및 강제 킥(Kick)을 통한 OOM 방어 적용.
 * - SO_KEEPALIVE 옵션을 통한 Half-open 좀비 소켓 회수 방어벽 추가.
 * ============================================================================
 * File: T470_Commu_012.hpp
 * Summary: Network, Web, MQTT & OTA Communication Engine (FSM Synchronized)
 * ========================================================================== */
#pragma once

#include "T410_Def_012.hpp"
#include "T420_Types_012.hpp"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <cstdint>

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
    void _enforceTcpKeepAlive(); // [v012 신설] 좀비 소켓 회수
};
