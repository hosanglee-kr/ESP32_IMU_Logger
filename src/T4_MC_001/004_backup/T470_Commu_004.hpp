/* ============================================================================
 * File: T470_Commu_004.hpp
 * Summary: Network, Web, MQTT & OTA Communication Engine (FSM Synchronized)
 * * [AI 메모: 제공 기능 요약]
 * 1. WiFi/NTP 동기화 및 ESPAsyncWebServer 기반 REST API 제공.
 * 2. AsyncWebSocket 기반 실시간 피처(MFCC 등) 바이너리 브로드캐스트.
 * 3. MQTT Publish 및 비동기 OTA 펌웨어 업데이트 지원.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. ESPAsyncWebServer의 콜백은 메인 루프(Core 0/1)가 아닌 비동기 태스크 컨텍스트입니다.
 * 따라서 FSM 상태를 직접 제어하지 말고 큐/이벤트로 명령을 하달해야 합니다.
 * * * * [AI 셀프 회고 및 구현 원칙 - 비동기 통신 및 네트워크 방어] * * *
 * * [방어 1: 네트워크 페이로드 절단 및 좀비 소켓(OOM) 방어]
 * - 원칙: 통신 모듈은 이중 sizeof 곱셈을 피하고 순수 바이트 크기만 취급할 것.
 * - 원칙: _ws.cleanupClients()를 주기적으로 호출하여 닫힌 소켓을 강제 회수할 것.
 * * [방어 2: 비동기 스레드 블로킹(데드락) 및 상태 머신 파괴 차단]
 * - 원칙: 콜백 내 delay()를 금지하고, 재부팅/명령은 FSM에 위임할 것.
 * - 원칙: 대용량 청크 업로드 시 에러가 나면 상태만 마킹하고 응답은 최종에서 1회만 할 것.
 * * [방어 3: OTA 파티션 락(Lock) 및 SD 병목(I/O 마비) 방어]
 * - 원칙: _isOtaRunning 플래그로 동시 다발적인 OTA 접근을 HTTP 423 Locked로 방어할 것.
 * - 원칙: FSM 상태가 RECORDING일 경우 대용량 다운로드 API를 차단하여 SPI 경합을 막을 것.
 * * [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * ========================================================================== */
#pragma once

#include "T410_Config_004.hpp"
#include "T420_Types_004.hpp"
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
    uint32_t _lastWifiRetryMs = 0; // 논블로킹 WiFi 타이머
    
    // 임시 설정 구조체 (추후 Flash/SD에서 로드된 값 매핑용)
    struct NetworkCredentials {
        char ssid[32];
        char password[64];
        char mqtt_broker[64];
        bool use_mqtt;
    } _creds;

    bool _isOtaRunning = false; 

public:
    T470_Communicator();
    ~T470_Communicator();

    /**
     * @brief WiFi, NTP, WebServer, MQTT 초기화 및 라우팅 설정
     */
    bool init(const char* p_ssid, const char* p_pw, const char* p_mqttBroker = "");

    /**
     * @brief 메인 루프에서 주기적으로 호출되어 WiFi/MQTT 재연결 및 소켓 회수 수행
     */
    void runNetwork();

    /**
     * @brief 바이너리 데이터를 WebSocket으로 브로드캐스트 (B8 방어 적용)
     */
    void broadcastBinary(const void* p_buffer, size_t p_bytes);

    /**
     * @brief 추출된 특징량(FeatureSlot) 및 판정 결과를 MQTT로 발행
     */
    bool publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result);

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

private:
    void _initWebHandlers();
    void _setCorsHeaders(AsyncWebServerResponse* p_response);
    void _sendJsonResponse(AsyncWebServerRequest* p_request, const JsonDocument& p_doc);
    void _reconnectMqtt();
};
