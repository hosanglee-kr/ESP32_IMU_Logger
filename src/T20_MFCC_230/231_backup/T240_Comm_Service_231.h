/* ============================================================================
 * File: T240_Comm_Service_231.h
 * Summary: Network, Web, MQTT & OTA Communication Engine (FSM Synchronized)
 * * [AI 메모: 제공 기능 요약]
 * 1. WiFi 연결 관리 (AP 단독, STA, Auto-Fallback 다중 AP 순회 접속).
 * 2. NTP 시간 동기화 (KST 타임존 적용).
 * 3. ESPAsyncWebServer 기반 비동기 REST API (설정, 제어, 파일 스트리밍).
 * 4. AsyncWebSocket 기반 실시간 파형/스펙트럼/MFCC 바이너리 브로드캐스트.
 * 5. MQTT 발행(Publish) 및 자동 재접속 유지보수.
 * 6. OTA(Over-The-Air) 무선 펌웨어 업데이트 지원.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. ESPAsyncWebServer의 콜백(Lambda) 함수들은 메인 루프가 아닌 별도의 
 * 비동기 태스크(AsyncEvent) 컨텍스트에서 실행됩니다. 따라서 내부 구현체(ST_Impl)를 
 * 직접 제어하지 않고, 반드시 g_t20->postCommand()를 통해 FSM 큐로 명령을 하달해야 
 * Race Condition과 논리적 데드락을 원천 차단할 수 있습니다.
 * 2. broadcastBinary() 호출 시 전송 버퍼의 크기가 너무 크면 내부 큐가 가득 차 
 * 전송이 누락될 수 있으므로 availableForWriteAll() 검사를 반드시 유지하세요.
 * ========================================================================== 
 */
 
#pragma once

#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

#include "T210_Def_231.h"

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

