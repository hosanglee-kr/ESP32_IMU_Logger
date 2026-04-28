/* ============================================================================
 * File: T470_Commu_005.cpp
 * Summary: Network, MQTT & OTA Implementation (FSM Command Queue Integration)
 * Description: HTTP 요청을 받아 FSM으로 비동기 명령을 하달하고 방어 로직을 적용한 통신부.
 * ========================================================================== 
 */
#include "T470_Commu_005.hpp"
#include "T450_FsmMgr_005.hpp" // FSM 상태 확인 및 명령 하달용
#include "T415_ConfigMgr_005.hpp" // 동적 설정 매니저 추가
#include <LittleFS.h>
#include <SD_MMC.h>
#include <time.h> 
#include <Update.h> 

T470_Communicator::T470_Communicator() 
    : _server(SmeaConfig::Network::HTTP_PORT_DEF), 
      _ws(SmeaConfig::Network::WS_URI_DEF), 
      _mqttClient(_wifiClient) {
}

T470_Communicator::~T470_Communicator() {
    _ws.closeAll();
    _server.end();
}

bool T470_Communicator::init(const char* p_ssid, const char* p_pw, const char* p_mqttBroker) {
    // 런타임 동적 설정 가져오기
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    strlcpy(_creds.ssid, p_ssid, sizeof(_creds.ssid));
    strlcpy(_creds.password, p_pw, sizeof(_creds.password));
    
    // JSON 동적 설정의 MQTT 브로커가 존재하면 우선 적용, 없으면 초기화 파라미터 적용
    if (strlen(v_cfg.mqtt.mqtt_broker) > 0) {
        strlcpy(_creds.mqtt_broker, v_cfg.mqtt.mqtt_broker, sizeof(_creds.mqtt_broker));
    } else {
        strlcpy(_creds.mqtt_broker, p_mqttBroker, sizeof(_creds.mqtt_broker));
    }
    _creds.use_mqtt = (strlen(_creds.mqtt_broker) > 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.printf("[Net] Connecting to %s...\n", _creds.ssid);
    WiFi.begin(_creds.ssid, _creds.password);

    uint32_t v_startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - v_startMs < SmeaConfig::Network::WIFI_CONN_TIMEOUT_DEF)) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[Net] WiFi Connected.");
        
        // NTP 동기화
        configTzTime(SmeaConfig::Network::TZ_INFO_DEF, SmeaConfig::Network::NTP_SERVER_1_DEF, SmeaConfig::Network::NTP_SERVER_2_DEF);
        struct tm v_timeinfo;
        v_startMs = millis();
        while (!getLocalTime(&v_timeinfo, 100) && (millis() - v_startMs < 5000)) { // 5초 타임아웃
            delay(100);
        }
    } else {
        Serial.println("[Net] WiFi Connect Failed. Will retry in background.");
    }

    if (_creds.use_mqtt && isConnected()) {
        // 포트는 런타임 설정의 기본 포트 사용
        _mqttClient.setServer(_creds.mqtt_broker, v_cfg.mqtt.default_port);
    }

    // CORS Preflight 전역 처리
    _server.onNotFound([this](AsyncWebServerRequest *p_request) {
        if (p_request->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse *v_response = p_request->beginResponse(200);
            _setCorsHeaders(v_response);
            p_request->send(v_response);
        } else {
            p_request->send(404, "text/plain", "Not Found");
        }
    });

    _initWebHandlers();
    
    _server.addHandler(&_ws);
    _server.begin();
    
    return true;
}

void T470_Communicator::_initWebHandlers() {
    
    // [1] 시스템 상태 모니터링 API
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* p_request) {
        JsonDocument v_doc;
        v_doc["sys_state"] = (uint8_t)T450_FsmManager::getInstance().getCurrentState();
        v_doc["heap_free"] = ESP.getFreeHeap();
        v_doc["psram_free"] = ESP.getFreePsram();
        _sendJsonResponse(p_request, v_doc);
    });
    
    _server.on("/api/recorder_begin", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_MANUAL_RECORD_START);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/recorder_end", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_MANUAL_RECORD_STOP);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/noise_learn", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_LEARN_NOISE);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });
    
    _server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        p_request->send(200, "application/json", "{\"ok\":true}");
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
    });

    // [2] 데이터 다운로드 API (SD 카드 병목 방어 적용)
    _server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest* p_request) {
        if (!p_request->hasParam("file")) {
            p_request->send(400, "text/plain", "Missing file param");
            return;
        }

        // [방어 3: SD 병목 I/O 마비 방어]
        if (T450_FsmManager::getInstance().getCurrentState() == SystemState::RECORDING) {
            p_request->send(423, "application/json", "{\"error\":\"locked_due_to_recording\"}");
            return;
        }

        String v_path = p_request->getParam("file")->value();
        if (SD_MMC.exists(v_path)) {
            p_request->send(SD_MMC, v_path, "application/octet-stream");
        } else {
            p_request->send(404, "text/plain", "File not found");
        }
    });

    _server.on("/api/ota", HTTP_POST, 
        [this](AsyncWebServerRequest *p_request) {
            if (!_isOtaRunning) {
                p_request->send(423, "application/json", "{\"ok\":false,\"msg\":\"locked\"}");
                return;
            }
            bool v_success = !Update.hasError();
            p_request->send(v_success ? 200 : 500, "application/json", v_success ? "{\"ok\":true}" : "{\"ok\":false}");
            _isOtaRunning = false; 
            
            if (v_success) {
                // [방어 2: 콜백 내 ESP.restart() 지양]
                // 추후 FSM 커맨드 큐를 통해 재부팅 스케줄링
                delay(500);
                ESP.restart();
            }
        },
        [this](AsyncWebServerRequest *p_request, String p_filename, size_t p_index, uint8_t *p_data, size_t p_len, bool p_final) {
            if (p_index == 0) {
                if (_isOtaRunning) return; // [방어 3: 파티션 락]
                _isOtaRunning = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    _isOtaRunning = false;
                }
            }
            if (_isOtaRunning && !Update.hasError()) {
                Update.write(p_data, p_len);
            }
            if (_isOtaRunning && p_final) {
                Update.end(true);
            }
        }
    );
    
    // [4] 프론트엔드 정적 웹 서빙
    _server.serveStatic("/", LittleFS, SmeaConfig::Path::WWW_ROOT_DEF).setDefaultFile(SmeaConfig::Path::WEB_INDEX_DEF);
}

// [방어 1: 네트워크 페이로드 절단 방어]
void T470_Communicator::broadcastBinary(const void* p_buffer, size_t p_bytes) {
    if (_ws.count() == 0 || !p_buffer) return;
    
    if (_ws.availableForWriteAll()) {
        _ws.binaryAll((uint8_t*)p_buffer, p_bytes); // 이중 sizeof(float) 금지 준수
    }
}

// [방어 1: 좀비 소켓 및 연결 복구 메인 루프]
void T470_Communicator::runNetwork() {
    _ws.cleanupClients(); // TCP OOM 방어

    if (!isConnected()) {
        if (millis() - _lastWifiRetryMs > SmeaConfig::Network::WIFI_RETRY_MS_DEF) { 
            WiFi.disconnect();
            WiFi.reconnect();
            _lastWifiRetryMs = millis();
        }
        return; 
    }

    if (_creds.use_mqtt) {
        if (!_mqttClient.connected()) _reconnectMqtt();
        else _mqttClient.loop(); 
    }
}

void T470_Communicator::_reconnectMqtt() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // 런타임 동적 설정(재시도 간격) 참조
    if (millis() - _lastMqttRetryMs > v_cfg.mqtt.retry_interval_ms) {
        String v_clientId = "SMEA-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (_mqttClient.connect(v_clientId.c_str())) {
            Serial.println("[MQTT] Reconnected.");
        }
        _lastMqttRetryMs = millis();
    }
}

bool T470_Communicator::publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result) {
    if (!_creds.use_mqtt || !_mqttClient.connected()) return false;

    JsonDocument v_doc;
    v_doc["device_id"] = (uint32_t)ESP.getEfuseMac();
    v_doc["result"] = (uint8_t)p_result;
    v_doc["rms"] = p_slot.rms;
    v_doc["crest_factor"] = p_slot.crest_factor;

    char v_payload[512];
    serializeJson(v_doc, v_payload, sizeof(v_payload));

    return _mqttClient.publish(SmeaConfig::Mqtt::TOPIC_RESULT_DEF, v_payload);
}

void T470_Communicator::_setCorsHeaders(AsyncWebServerResponse* p_response) {
    p_response->addHeader("Access-Control-Allow-Origin", "*");
    p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    p_response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void T470_Communicator::_sendJsonResponse(AsyncWebServerRequest* p_request, const JsonDocument& p_doc) {
    AsyncResponseStream *v_stream = p_request->beginResponseStream("application/json");
    _setCorsHeaders(v_stream);
    serializeJson(p_doc, *v_stream);
    p_request->send(v_stream);
}
