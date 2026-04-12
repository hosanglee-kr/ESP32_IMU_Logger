/* ============================================================================
 * File: T240_Comm_Service_219.h
 * Summary: Summary: Network, Web, MQTT & OTA Communication Engine
 * Description: v216의 모든 REST API, 파일 스트리밍, CORS, WebSocket
 * ========================================================================== */
#pragma once

#include <WiFi.h>
// #include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

#include "T210_Def_Com_219.h"
#include "T214_Def_Rec_219.h"

class CL_T20_CommService {
   public:
	CL_T20_CommService();
	~CL_T20_CommService();

	// 네트워크 및 MQTT 초기화
	bool begin(const ST_T20_Config_t& cfg);
	
	// REST API 및 WebSocket 핸들러 라우팅
	void initHandlers(void* p_master_impl);

	// 실시간 바이너리 데이터 브로드캐스트 (WS, OOM 방어 적용)
	void broadcastBinary(const float* p_buffer, size_t len);

	// MQTT 유지보수 및 발행 API
	void runMqtt();
	bool publishMqtt(const char* sub_topic, const JsonDocument& doc);
	
	// 상태 확인 유틸리티
	bool isConnected() const {
		return WiFi.status() == WL_CONNECTED;
	}
	uint32_t calcStatusHash(uint32_t frame_id, uint32_t rec_count, bool measuring);

   private:
    // CORS 및 JSON 응답 헬퍼
	void _setCorsHeaders(AsyncWebServerResponse* response);
	void _sendJson(AsyncWebServerRequest* request, const JsonDocument& doc);
	
	void _reconnectMqtt(); // MQTT 재접속 로직

   private:
	AsyncWebServer _server;
	AsyncWebSocket _ws;

	// MQTT 및 통신 자원
	WiFiClient          _wifi_client;
	PubSubClient        _mqtt_client;
	ST_T20_ConfigMqtt_t _mqtt_cfg;
	uint32_t            _last_mqtt_retry_ms;
};
