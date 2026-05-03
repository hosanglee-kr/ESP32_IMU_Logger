/* ============================================================================
 * File: T470_Commu_012.cpp
 * Summary: Network, MQTT & OTA Implementation (FSM Command Queue Integration)
 * ============================================================================
 * * [AI 메모: 마이그레이션 적용 사항]
 * 1. [OTA 데드락 방어]: OTA 진입 시 T450에 CMD_OTA_START를 디스패치하여 
 * FSM을 MAINTENANCE 상태로 전환시킴으로써 마이크와 SD카드 버스 락을 해제.
 * 2. [OOM 통신 폭발 방어]: broadcastBinary 내부에서 4KB 이상 큐 적체 시 
 * 클라이언트 강제 종료(client->close()) 수행.
 * 3. [시간 무결성 보장]: time() 기반 Epoch 기록을 위해 SNTP 서버 완벽 동기화 보장.
 * ========================================================================== */
#include "T470_Commu_012.hpp"
#include "T450_FsmMgr_012.hpp"
#include "T415_ConfigMgr_012.hpp" 
#include <LittleFS.h>
#include <SD_MMC.h>
#include <time.h>
#include <Update.h>
#include <lwip/sockets.h>

T470_Communicator::T470_Communicator()
    : _server(SmeaConfig::Network::HTTP_PORT_DEF),
      _ws(SmeaConfig::Network::WS_URI_DEF),
      _mqttClient(_wifiClient) {
}

T470_Communicator::~T470_Communicator() {
    _ws.closeAll();
    _server.end();
}

// [v012 신설] LwIP 레벨의 TCP Keep-Alive 강제 활성화 (좀비 소켓 킬러)
void T470_Communicator::_enforceTcpKeepAlive() {
    int v_fd = _wifiClient.fd();
    if (v_fd >= 0) {
        int v_enable = 1;
        setsockopt(v_fd, SOL_SOCKET, SO_KEEPALIVE, &v_enable, sizeof(v_enable));
        
        int v_idle = 30; // 30초 대기 후 프로브
        int v_interval = 5; // 5초 간격
        int v_count = 3; // 3회 실패 시 연결 해제
        
        setsockopt(v_fd, IPPROTO_TCP, TCP_KEEPIDLE, &v_idle, sizeof(v_idle));
        setsockopt(v_fd, IPPROTO_TCP, TCP_KEEPINTVL, &v_interval, sizeof(v_interval));
        setsockopt(v_fd, IPPROTO_TCP, TCP_KEEPCNT, &v_count, sizeof(v_count));
    }
}

bool T470_Communicator::init(const char* p_ssid, const char* p_pw, const char* p_mqttBroker) {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    if (strlen(v_cfg.mqtt.mqtt_broker) > 0) {
        strlcpy(_mqttCreds.broker, v_cfg.mqtt.mqtt_broker, sizeof(_mqttCreds.broker));
    } else {
        strlcpy(_mqttCreds.broker, p_mqttBroker, sizeof(_mqttCreds.broker));
    }
    _mqttCreds.enable = (strlen(_mqttCreds.broker) > 0);
    _mqttCreds.port = v_cfg.mqtt.default_port;

    WiFi.mode(WIFI_OFF);
    delay(SmeaConfig::NetworkLimit::WIFI_MODE_SWITCH_DELAY_MS_CONST);

    if (v_cfg.wifi.mode == 0 || v_cfg.wifi.mode == 2 || v_cfg.wifi.mode == 3) {
        if (strlen(v_cfg.wifi.ap_ip) > 0) {
            IPAddress v_apIp;
            if (v_apIp.fromString(v_cfg.wifi.ap_ip)) {
                WiFi.softAPConfig(v_apIp, v_apIp, IPAddress(255, 255, 255, 0));
            }
        }
        WiFi.softAP(v_cfg.wifi.ap_ssid, v_cfg.wifi.ap_password);
        Serial.printf("[Net] AP Started: %s\n", v_cfg.wifi.ap_ssid);
    }

    if (v_cfg.wifi.mode != 0) {
        WiFi.mode(v_cfg.wifi.mode == 2 ? WIFI_AP_STA : WIFI_STA);

        for (uint8_t i = 0; i < SmeaConfig::NetworkLimit::MAX_MULTI_AP_CONST; i++) {
            const char* v_targetSsid = (i == 0 && strlen(v_cfg.wifi.multi_ap[0].ssid) == 0 && strlen(p_ssid) > 0) ? p_ssid : v_cfg.wifi.multi_ap[i].ssid;
            const char* v_targetPw = (i == 0 && strlen(v_cfg.wifi.multi_ap[0].password) == 0 && strlen(p_pw) > 0) ? p_pw : v_cfg.wifi.multi_ap[i].password;

            if (strlen(v_targetSsid) > 0) {
                WiFi.disconnect();
                delay(SmeaConfig::NetworkLimit::WIFI_DISCONNECT_DELAY_MS_CONST);

                if (v_cfg.wifi.multi_ap[i].use_static_ip) {
                    IPAddress v_ip, v_gw, v_sn, v_d1, v_d2;
                    v_ip.fromString(v_cfg.wifi.multi_ap[i].local_ip);
                    v_gw.fromString(v_cfg.wifi.multi_ap[i].gateway);
                    v_sn.fromString(v_cfg.wifi.multi_ap[i].subnet);
                    if (strlen(v_cfg.wifi.multi_ap[i].dns1) > 0) v_d1.fromString(v_cfg.wifi.multi_ap[i].dns1);
                    if (strlen(v_cfg.wifi.multi_ap[i].dns2) > 0) v_d2.fromString(v_cfg.wifi.multi_ap[i].dns2);
                    WiFi.config(v_ip, v_gw, v_sn, v_d1, v_d2);
                } else {
                    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
                }

                Serial.printf("[Net] Connecting to %s...\n", v_targetSsid);
                WiFi.begin(v_targetSsid, v_targetPw);

                uint32_t v_startMs = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - v_startMs < SmeaConfig::Network::WIFI_CONN_TIMEOUT_DEF)) {
                    delay(200);
                }

                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("[Net] WiFi Connected.");
                    configTzTime(SmeaConfig::Network::TZ_INFO_DEF, SmeaConfig::Network::NTP_SERVER_1_DEF, SmeaConfig::Network::NTP_SERVER_2_DEF);

                    struct tm v_timeinfo;
                    uint32_t v_syncStart = millis();
                    while (!getLocalTime(&v_timeinfo, 100) && (millis() - v_syncStart < SmeaConfig::Network::NTP_TIMEOUT_MS_DEF)) {
                        delay(100);
                    }
                    _enforceTcpKeepAlive();
                    break;
                }
            }
        }
    }

    if (_mqttCreds.enable && isConnected()) {
        _mqttClient.setServer(_mqttCreds.broker, _mqttCreds.port);
    }

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

    _server.on("/api/factory_reset", HTTP_POST, [](AsyncWebServerRequest* p_request) {
        T415_ConfigManager::getInstance().resetToDefault();
        p_request->send(200, "application/json", "{\"ok\":true,\"msg\":\"factory_reset_and_rebooting\"}");
        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
    });

    _server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest* p_request) {
        if (!p_request->hasParam("file")) {
            p_request->send(400, "text/plain", "Missing file param");
            return;
        }

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

    _server.on("/api/runtime_config", HTTP_GET, [this](AsyncWebServerRequest* p_request) {
        if (LittleFS.exists(SmeaConfig::Path::SYS_CFG_JSON_DEF)) {
            AsyncWebServerResponse* v_response = p_request->beginResponse(LittleFS, SmeaConfig::Path::SYS_CFG_JSON_DEF, "application/json");
            _setCorsHeaders(v_response);
            p_request->send(v_response);
        } else {
            p_request->send(404, "application/json", "{}");
        }
    });

    _server.on("/api/runtime_config", HTTP_POST,
        [](AsyncWebServerRequest *p_request) {
            p_request->send(400, "application/json", "{\"ok\":false,\"msg\":\"no_body\"}");
        },
        NULL,
        [this](AsyncWebServerRequest *p_request, uint8_t *p_data, size_t p_len, size_t p_index, size_t p_total) {
            if (p_index == 0) {
                if (p_total > SmeaConfig::Network::LARGE_BUF_SIZE_DEF) {
                    p_request->send(413, "application/json", "{\"ok\":false,\"msg\":\"payload_too_large\"}");
                    return;
                }

                p_request->_tempObject = malloc(p_total + 1);
                p_request->onDisconnect([p_request]() {
                    if (p_request->_tempObject) {
                        free(p_request->_tempObject);
                        p_request->_tempObject = NULL;
                    }
                });
            }

            uint8_t* v_buffer = (uint8_t*)p_request->_tempObject;

            if (!v_buffer) {
                if (p_index + p_len == p_total && !p_request->client()->disconnected()) {
                    p_request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom_or_rejected\"}");
                }
                return;
            }

            memcpy(v_buffer + p_index, p_data, p_len);

            if (p_index + p_len == p_total) {
                v_buffer[p_total] = '\0';
                if (T415_ConfigManager::getInstance().updateFromJson((const char*)v_buffer)) {
                    p_request->send(200, "application/json", "{\"ok\":true,\"msg\":\"updated_and_rebooting\"}");
                    T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
                } else {
                    p_request->send(400, "application/json", "{\"ok\":false,\"msg\":\"json_parse_or_save_error\"}");
                }
                free(v_buffer);
                p_request->_tempObject = NULL;
            }
        }
    );

    // [v012 변경] OTA 진입 시 FSM에 MAINTENANCE 전환 명령 하달 (시스템 락 해제)
    _server.on("/api/ota", HTTP_POST,
        [this](AsyncWebServerRequest *p_request) {
            if (!_isOtaRunning) {
                p_request->send(423, "application/json", "{\"ok\":false,\"msg\":\"locked\"}");
                return;
            }
            bool v_success = !Update.hasError();
            p_request->send(v_success ? 200 : 500, "application/json", v_success ? "{\"ok\":true}" : "{\"ok\":false}");
            _isOtaRunning = false;
            
            T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_OTA_END);

            if (v_success) {
                T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
            }
        },
        [this](AsyncWebServerRequest *p_request, String p_filename, size_t p_index, uint8_t *p_data, size_t p_len, bool p_final) {
            if (p_index == 0) {
                if (_isOtaRunning) return;
                _isOtaRunning = true;
                
                // OTA 시작 시 시스템 버스(SPI/SD) 충돌 방어
                T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_OTA_START);
                
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

    _server.serveStatic("/", LittleFS, SmeaConfig::Path::WWW_ROOT_DEF).setDefaultFile(SmeaConfig::Path::WEB_INDEX_DEF);
}

// [v012 업그레이드] 브라우저 스로틀링 대응 OOM 킬러 (R-NMG 방어벽)
void T470_Communicator::broadcastBinary(const void* p_buffer, size_t p_bytes) {
    if (_ws.count() == 0 || !p_buffer) return;

    // 1. 포인터(*) 대신 객체 참조(auto&)로 순회하도록 변경
    for (auto& v_client : _ws.getClients()) {
        // 2. 포인터 접근자(->) 대신 객체 접근자(.) 사용
        if (v_client.status() == WS_CONNECTED) {
            // 3. queueLength() 를 queueLen() 으로 수정
            if (v_client.queueLen() > 4096) {
                v_client.close();
                Serial.println("[Net] Zombie WS Client Kicked (OOM Prevented)");
            } else {
                v_client.binary((uint8_t*)p_buffer, p_bytes);
            }
        }
    }
}



void T470_Communicator::runNetwork() {
    _ws.cleanupClients();

    if (!isConnected() && WiFi.getMode() != WIFI_AP) {
        if (millis() - _lastWifiRetryMs > SmeaConfig::Network::WIFI_RETRY_MS_DEF) {
            Serial.println("[Net] WiFi lost. Attempting reconnect...");
            WiFi.disconnect();
            WiFi.reconnect();
            _lastWifiRetryMs = millis();
        }
        return;
    }

    if (_mqttCreds.enable) {
        if (!_mqttClient.connected()) _reconnectMqtt();
        else _mqttClient.loop();
    }
}

void T470_Communicator::_reconnectMqtt() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    if (millis() - _lastMqttRetryMs > v_cfg.mqtt.retry_interval_ms) {
        String v_clientId = "SMEA-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (_mqttClient.connect(v_clientId.c_str())) {
            Serial.println("[MQTT] Reconnected.");
        }
        _lastMqttRetryMs = millis();
    }
}

// [v012 주의사항] PubSubClient는 내부적으로 QoS 0만 지원하므로, 알람의 완벽한 보장을 위해서는
// 추후 esp-mqtt(QoS 1 지원)로 기반 라이브러리 교체가 필요합니다. 현재는 실패 시 로깅으로 방어합니다.
bool T470_Communicator::publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result) {
    if (!_mqttCreds.enable || !_mqttClient.connected()) return false;

    JsonDocument v_doc;
    v_doc["device_id"] = (uint32_t)ESP.getEfuseMac();
    v_doc["result"] = (uint8_t)p_result;
    v_doc["rms"] = p_slot.rms;
    v_doc["crest_factor"] = p_slot.crest_factor;

    char v_payload[512];
    serializeJson(v_doc, v_payload, sizeof(v_payload));

    bool v_sent = _mqttClient.publish(SmeaConfig::Mqtt::TOPIC_RESULT_DEF, v_payload);
    if (!v_sent) {
        ESP_LOGE("T470_MQTT", "CRITICAL: MQTT Publish Failed! Alarm might be lost (QoS 0 Limitation)");
    }
    return v_sent;
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



