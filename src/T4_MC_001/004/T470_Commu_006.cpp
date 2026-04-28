/* ============================================================================
 * File: T470_Commu_006.cpp
 * Summary: Network, MQTT & OTA Implementation (FSM Command Queue Integration)
 * Description: HTTP 요청을 받아 FSM으로 비동기 명령을 하달하고 방어 로직을 적용한 통신부.
 * ========================================================================== */
#include "T470_Commu_006.hpp"
#include "T450_FsmMgr_005.hpp" // FSM 상태 확인 및 명령 하달용
#include "T415_ConfigMgr_006.hpp" // 동적 설정 매니저 추가
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

    // JSON 동적 설정의 MQTT 브로커가 존재하면 우선 적용, 없으면 초기화 파라미터 적용
    if (strlen(v_cfg.mqtt.mqtt_broker) > 0) {
        strlcpy(_mqttCreds.broker, v_cfg.mqtt.mqtt_broker, sizeof(_mqttCreds.broker));
    } else {
        strlcpy(_mqttCreds.broker, p_mqttBroker, sizeof(_mqttCreds.broker));
    }
    _mqttCreds.enable = (strlen(_mqttCreds.broker) > 0);
    _mqttCreds.port = v_cfg.mqtt.default_port;

    WiFi.mode(WIFI_OFF);
    delay(50);

    // [1] AP 모드 및 커스텀 IP 설정 (모드 0: AP_ONLY, 2: AP_STA, 3: AUTO_FALLBACK)
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

    // [2] STA 모드 (개별 라우터별 고정IP/DHCP 적용 및 순차 접속)
    if (v_cfg.wifi.mode != 0) {
        WiFi.mode(v_cfg.wifi.mode == 2 ? WIFI_AP_STA : WIFI_STA);

        for (int i = 0; i < 3; i++) {
            // 호환성을 위해 p_ssid가 있으면 임시로 첫 번째에 삽입하거나 JSON 설정 우선 사용
            const char* v_targetSsid = (i == 0 && strlen(v_cfg.wifi.multi_ap[0].ssid) == 0 && strlen(p_ssid) > 0) ? p_ssid : v_cfg.wifi.multi_ap[i].ssid;
            const char* v_targetPw = (i == 0 && strlen(v_cfg.wifi.multi_ap[0].password) == 0 && strlen(p_pw) > 0) ? p_pw : v_cfg.wifi.multi_ap[i].password;

            if (strlen(v_targetSsid) > 0) {
                WiFi.disconnect(); 
                delay(100);

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
                    break; // 접속 성공 시 순회 중지
                }
            }
        }
    }

    if (_mqttCreds.enable && isConnected()) {
        _mqttClient.setServer(_mqttCreds.broker, _mqttCreds.port);
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

    // [3] 시스템 설정 JSON API (다운로드 & 업로드)
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
            
            // [방어 2 상태 붕괴 차단] OOM 발생 시 매 청크마다 send(500) 호출 방지
            if (!v_buffer) {
                if (p_index + p_len == p_total && !p_request->client()->disconnected()) { 
                    p_request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom_or_rejected\"}");
                }
                return;
            }

            memcpy(v_buffer + p_index, p_data, p_len);

            if (p_index + p_len == p_total) {
                v_buffer[p_total] = '\0';

                JsonDocument v_doc;
                DeserializationError v_err = deserializeJson(v_doc, v_buffer);

                if (!v_err) {
                    File v_f = LittleFS.open(SmeaConfig::Path::SYS_CFG_JSON_DEF, "w");
                    if (v_f) {
                        serializeJson(v_doc, v_f);
                        v_f.close();
                        p_request->send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting\"}");
                        
                        // [방어 2] FSM 커맨드를 통한 안전한 재부팅
                        T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT); 
                    } else {
                        p_request->send(500, "application/json", "{\"ok\":false,\"msg\":\"fs_error\"}");
                    }
                } else {
                    p_request->send(400, "application/json", "{\"ok\":false,\"msg\":\"json_error\"}");
                }
                free(v_buffer);
                p_request->_tempObject = NULL;
            }
        }
    );

    // [4] OTA 무선 업데이트
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
                // [방어 2] FSM 커맨드 큐를 통해 재부팅 스케줄링
                T450_FsmManager::getInstance().dispatchCommand(SystemCommand::CMD_REBOOT);
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
    
    // [5] 프론트엔드 정적 웹 서빙
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

    // WiFi Auto-Reconnect (논블로킹 백오프 10초 적용)
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

bool T470_Communicator::publishResultMqtt(const SmeaType::FeatureSlot& p_slot, DetectionResult p_result) {
    if (!_mqttCreds.enable || !_mqttClient.connected()) return false;

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