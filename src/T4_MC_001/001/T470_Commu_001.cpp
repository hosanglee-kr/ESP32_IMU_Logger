/* ============================================================================
 * File: T470_Commu_001.cpp
 * Summary: Network, MQTT & OTA Implementation (FSM Command Queue Integration)
 * Description: HTTP 요청을 받아 FSM으로 비동기 명령을 하달하고 방어 로직을 적용한 통신부.
 * ========================================================================== */
#include "T470_Commu_001.hpp"
#include "T450_FsmMgr_001.hpp" // FSM 상태 확인 및 명령 하달용
#include <LittleFS.h>
#include <SD_MMC.h>
#include <time.h> 
#include <Update.h> 

T470_Communicator::T470_Communicator() 
    : v_server(SmeaConfig::Network::HTTP_PORT), 
      v_ws(SmeaConfig::Network::WS_URI), 
      v_mqttClient(v_wifiClient) {
}

T470_Communicator::~T470_Communicator() {
    v_ws.closeAll();
    v_server.end();
}

bool T470_Communicator::init(const char* p_ssid, const char* p_pw, const char* p_mqttBroker) {
    strncpy(v_creds.ssid, p_ssid, sizeof(v_creds.ssid));
    strncpy(v_creds.password, p_pw, sizeof(v_creds.password));
    strncpy(v_creds.mqtt_broker, p_mqttBroker, sizeof(v_creds.mqtt_broker));
    v_creds.use_mqtt = (strlen(p_mqttBroker) > 0);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.printf("[Net] Connecting to %s...\n", v_creds.ssid);
    WiFi.begin(v_creds.ssid, v_creds.password);

    uint32_t v_startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - v_startMs < SmeaConfig::Network::WIFI_CONN_TIMEOUT)) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[Net] WiFi Connected.");
        
        // NTP 동기화
        configTzTime(SmeaConfig::Network::TZ_INFO, SmeaConfig::Network::NTP_SERVER_1, SmeaConfig::Network::NTP_SERVER_2);
        struct tm v_timeinfo;
        v_startMs = millis();
        while (!getLocalTime(&v_timeinfo, 100) && (millis() - v_startMs < SmeaConfig::Network::NTP_TIMEOUT_MS)) {
            delay(100);
        }
    } else {
        Serial.println("[Net] WiFi Connect Failed. Will retry in background.");
    }

    if (v_creds.use_mqtt && isConnected()) {
        v_mqttClient.setServer(v_creds.mqtt_broker, SmeaConfig::Mqtt::DEFAULT_PORT);
    }

    // CORS Preflight 전역 처리
    v_server.onNotFound([this](AsyncWebServerRequest *p_request) {
        if (p_request->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse *v_response = p_request->beginResponse(200);
            setCorsHeaders(v_response);
            p_request->send(v_response);
        } else {
            p_request->send(404, "text/plain", "Not Found");
        }
    });

    initWebHandlers();
    
    v_server.addHandler(&v_ws);
    v_server.begin();
    
    return true;
}

void T470_Communicator::initWebHandlers() {
    
    // [1] 시스템 상태 모니터링 API
    v_server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* p_request) {
        JsonDocument v_doc;
        v_doc["sys_state"] = (uint8_t)T450_FsmManager::getInstance().getCurrentState();
        v_doc["heap_free"] = ESP.getFreeHeap();
        v_doc["psram_free"] = ESP.getFreePsram();
        sendJsonResponse(p_request, v_doc);
    });
    
    v_server.on("/api/recorder_begin", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_MANUAL_RECORD_START);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });

    v_server.on("/api/recorder_end", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_MANUAL_RECORD_STOP);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });

    v_server.on("/api/noise_learn", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_LEARN_NOISE);
        p_request->send(200, "application/json", "{\"ok\":true}");
    });
    
    v_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        p_request->send(200, "application/json", "{\"ok\":true}");
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
    });


    // [2] 데이터 다운로드 API (SD 카드 병목 방어 적용)
    v_server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest* p_request) {
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

    v_server.on("/api/ota", HTTP_POST, 
        [this](AsyncWebServerRequest *p_request) {
            if (!v_isOtaRunning) {
                p_request->send(423, "application/json", "{\"ok\":false,\"msg\":\"locked\"}");
                return;
            }
            bool v_success = !Update.hasError();
            p_request->send(v_success ? 200 : 500, "application/json", v_success ? "{\"ok\":true}" : "{\"ok\":false}");
            v_isOtaRunning = false; 
            
            if (v_success) {
                // [방어 2: 콜백 내 ESP.restart() 지양]
                // 추후 FSM 커맨드 큐를 통해 재부팅 스케줄링
                delay(500);
                ESP.restart();
            }
        },
        [this](AsyncWebServerRequest *p_request, String p_filename, size_t p_index, uint8_t *p_data, size_t p_len, bool p_final) {
            if (p_index == 0) {
                if (v_isOtaRunning) return; // [방어 3: 파티션 락]
                v_isOtaRunning = true;
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    v_isOtaRunning = false;
                }
            }
            if (v_isOtaRunning && !Update.hasError()) {
                Update.write(p_data, p_len);
            }
            if (v_isOtaRunning && p_final) {
                Update.end(true);
            }
        }
    );
    
    

    // [4] 프론트엔드 정적 웹 서빙
    v_server.serveStatic("/", LittleFS, SmeaConfig::Path::WWW_ROOT).setDefaultFile(SmeaConfig::Path::WEB_INDEX);
}

// [방어 1: 네트워크 페이로드 절단 방어]
void T470_Communicator::broadcastBinary(const void* p_buffer, size_t p_bytes) {
    if (v_ws.count() == 0 || !p_buffer) return;
    
    if (v_ws.availableForWriteAll()) {
        v_ws.binaryAll((uint8_t*)p_buffer, p_bytes); // 이중 sizeof(float) 금지 준수
    }
}

// [방어 1: 좀비 소켓 및 연결 복구 메인 루프]
void T470_Communicator::runNetwork() {
    v_ws.cleanupClients(); // TCP OOM 방어

    if (!isConnected()) {
        if (millis() - v_lastWifiRetryMs > SmeaConfig::Network::WIFI_RETRY_MS) { 
            WiFi.disconnect();
            WiFi.reconnect();
            v_lastWifiRetryMs = millis();
        }
        return; 
    }

    if (v_creds.use_mqtt) {
        if (!v_mqttClient.connected()) reconnectMqtt();
        else v_mqttClient.loop(); 
    }
}

void T470_Communicator::reconnectMqtt() {
    if (millis() - v_lastMqttRetryMs > SmeaConfig::Mqtt::RETRY_INTERVAL_MS) {
        String v_clientId = "SMEA-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (v_mqttClient.connect(v_clientId.c_str())) {
            Serial.println("[MQTT] Reconnected.");
        }
        v_lastMqttRetryMs = millis();
    }
}

bool T470_Communicator::publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result) {
    if (!v_creds.use_mqtt || !v_mqttClient.connected()) return false;

    JsonDocument v_doc;
    v_doc["device_id"] = (uint32_t)ESP.getEfuseMac();
    v_doc["result"] = (uint8_t)p_result;
    v_doc["rms"] = p_slot.rms;
    v_doc["crest_factor"] = p_slot.crest_factor;

    char v_payload[512];
    serializeJson(v_doc, v_payload, sizeof(v_payload));

    return v_mqttClient.publish(SmeaConfig::Mqtt::TOPIC_RESULT, v_payload);

}

void T470_Communicator::setCorsHeaders(AsyncWebServerResponse* p_response) {
    p_response->addHeader("Access-Control-Allow-Origin", "*");
    p_response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    p_response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void T470_Communicator::sendJsonResponse(AsyncWebServerRequest* p_request, const JsonDocument& p_doc) {
    AsyncResponseStream *v_stream = p_request->beginResponseStream("application/json");
    setCorsHeaders(v_stream);
    serializeJson(p_doc, *v_stream);
    p_request->send(v_stream);
}

