/* ============================================================================
 * File: T240_Comm_Service_231.cpp
 * Summary: Network, MQTT & OTA Implementation (FSM Command Queue Integration)
 * Description: HTTP 요청을 받아 메인 FSM(유한 상태 머신)으로 비동기 명령(Command)을 
 * 하달하고, 현재 시스템의 정확한 런타임 상태를 웹 UI로 응답하는 통신 인터페이스입니다.
 * ========================================================================== */

#include "T240_Comm_Service_231.h"
#include "T221_Mfcc_Inter_231.h" // ST_Impl 의존성 (엔진간 상호작용 용도)
#include <LittleFS.h>
#include <SD_MMC.h>
#include <time.h> // NTP 처리를 위한 표준 time 라이브러리
#include <Update.h> 

CL_T20_CommService::CL_T20_CommService()
    : _server(80), _ws(T20::C10_Web::WS_URI), _mqtt_client(_wifi_client) {
    _last_mqtt_retry_ms = 0;
}

CL_T20_CommService::~CL_T20_CommService() {
    _ws.closeAll();
    _server.end();
}


bool CL_T20_CommService::begin(const ST_T20_Config_t& cfg) {

    _mqtt_cfg = cfg.mqtt;
    const ST_T20_ConfigWiFi_t& w_cfg = cfg.wifi;
    
    WiFi.mode(WIFI_OFF);
    delay(50);

    // [1] AP 모드 및 커스텀 IP 설정
    if (w_cfg.mode == EN_T20_WIFI_AP_ONLY || w_cfg.mode == EN_T20_WIFI_AP_STA || w_cfg.mode == EN_T20_WIFI_AUTO_FALLBACK) {
        // 사용자가 AP IP를 지정했다면 적용
        if (w_cfg.ap_ip[0] != '\0') {
            IPAddress apIP;
            if (apIP.fromString(w_cfg.ap_ip)) {
                // 서브넷은 255.255.255.0으로 고정
                WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
            }
        }
        WiFi.softAP(w_cfg.ap_ssid, w_cfg.ap_password);
    }

    // [2] STA 모드 (개별 라우터별 고정IP/DHCP 적용 및 순차 접속)
    if (w_cfg.mode != EN_T20_WIFI_AP_ONLY) {
        WiFi.mode(w_cfg.mode == EN_T20_WIFI_AP_STA ? WIFI_AP_STA : WIFI_STA);

        for (int i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
            if (w_cfg.multi_ap[i].ssid[0] != '\0') {

                WiFi.disconnect(); // 이전 접속 시도 초기화
                delay(100);

                // 라우터별 고정 IP / DHCP 스위칭
                if (w_cfg.multi_ap[i].use_static_ip) {
                    IPAddress ip, gw, sn, d1, d2;
                    ip.fromString(w_cfg.multi_ap[i].local_ip);
                    gw.fromString(w_cfg.multi_ap[i].gateway);
                    sn.fromString(w_cfg.multi_ap[i].subnet);
                    if (w_cfg.multi_ap[i].dns1[0] != '\0') d1.fromString(w_cfg.multi_ap[i].dns1);
                    if (w_cfg.multi_ap[i].dns2[0] != '\0') d2.fromString(w_cfg.multi_ap[i].dns2);
                    WiFi.config(ip, gw, sn, d1, d2);
                } else {
                    // DHCP 사용 시 이전 config 초기화
                    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
                }

                WiFi.begin(w_cfg.multi_ap[i].ssid, w_cfg.multi_ap[i].password);

                uint32_t start_ms = millis();
                // 타임아웃 4초 대기
                while (WiFi.status() != WL_CONNECTED && (millis() - start_ms < 4000)) {
                    delay(200);
                }

                // 연결 성공 시 더 이상 다른 공유기를 찾지 않고 반복문 종료
                if (WiFi.status() == WL_CONNECTED) {
                    // [NTP 동기화 추가]
                    configTzTime(T20::C10_Time::TZ_INFO, T20::C10_Time::NTP_SERVER_1, T20::C10_Time::NTP_SERVER_2);

                    // 동기화 대기 (최대 5초)
                    struct tm timeinfo;
                    uint32_t start_time = millis();
                    while (!getLocalTime(&timeinfo, 100) && (millis() - start_time < T20::C10_Time::SYNC_TIMEOUT_MS)) {
                        delay(100);
                    }
                    break;
                }
            }
        }
    }
    
    // MQTT 서버 설정
    if (_mqtt_cfg.enable && isConnected()) {
        _mqtt_client.setServer(_mqtt_cfg.broker, _mqtt_cfg.port);
    }

    // [3] WebSocket 바인딩
    _ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t, void* a, uint8_t* d, size_t l) {
        // WS 연결 이벤트 핸들링
    });
    _server.addHandler(&_ws);

    // [4] CORS Preflight 전역 허용
    _server.onNotFound([this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            AsyncWebServerResponse *response = request->beginResponse(200);
            _setCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    });

    _server.begin();
    return true;
}



void CL_T20_CommService::initHandlers(void* p_master_impl) {
    CL_T20_Mfcc::ST_Impl* p = (CL_T20_Mfcc::ST_Impl*)p_master_impl;
    if (!p) return;

    // ========================================================================
    // 1. 상태 및 모니터링 API (FSM 상태 동기화)
    // ========================================================================
    _server.on("/api/t20/status", HTTP_GET, [this, p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["running"] = p->running;
        
        // [FSM 개선] 현재 시스템의 명시적 상태값을 UI로 전달 (0:INIT, 1:READY, 2:MONITORING, 3:RECORDING, 4:LEARNING)
        doc["sys_state"] = (int)p->current_state; 
        
        // 해시 계산용 플래그: MONITORING 이상의 상태면 true로 간주
        bool is_active = (p->current_state >= EN_T20_STATE_MONITORING);
        doc["hash"] = calcStatusHash(p->sample_counter, p->storage.getRecordCount(), is_active); 
        
        doc["sensor_status"] = p->sensor.getStatusText();
        doc["storage_open"] = p->storage.isOpen();
        _sendJson(request, doc);
    });

    // ========================================================================
    // 2. 레코더 및 커맨드 제어 API (FSM Command Queue 하달)
    // ========================================================================
    _server.on("/api/t20/recorder_begin", HTTP_POST, [](AsyncWebServerRequest* req) {
     	// [FSM 개선] 변수 직접 조작 및 스토리지 접근을 제거하고, 메인 FSM 큐로 명령을 위임합니다.
        if (g_t20) g_t20->postCommand(EN_T20_CMD_START);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/t20/recorder_end", HTTP_POST, [](AsyncWebServerRequest* req) {
        // [FSM 개선] 메인 FSM 큐로 정지(대기) 명령을 위임합니다.
        if (g_t20) g_t20->postCommand(EN_T20_CMD_STOP);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/t20/noise_learn", HTTP_POST, [](AsyncWebServerRequest* req) {
        // [FSM 개선] 파라미터 유무와 상관없이 토글(Toggle) 커맨드로 전달
        if (g_t20) g_t20->postCommand(EN_T20_CMD_LEARN_NOISE);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/t20/calibrate", HTTP_POST, [](AsyncWebServerRequest* req) {
        // 센서 캘리브레이션은 즉각적인 I2C/SPI 제어가 필요하므로 Command Queue로 전달합니다.
        if (g_t20) g_t20->postCommand(EN_T20_CMD_CALIBRATE);
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // ========================================================================
    // 3. 파일 스트리밍 및 다운로드 API
    // ========================================================================
    // 인덱스 파일 조회
    _server.on("/api/t20/recorder_index", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!LittleFS.exists("/sys/recorder_index.json")) {
            request->send(404, "application/json", "{\"error\":\"not_found\"}");
            return;
        }
        AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/sys/recorder_index.json", "application/json");
        _setCorsHeaders(response);
        request->send(response);
    });

    // 파일 다운로드 (Streaming)
    _server.on("/api/t20/recorder/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!request->hasParam("path")) {
            request->send(400, "text/plain", "path_required");
            return;
        }
        String path = request->getParam("path")->value();

        // SD_MMC 최우선, 없으면 LittleFS 확인
        if (SD_MMC.exists(path)) {
            AsyncWebServerResponse *response = request->beginResponse(SD_MMC, path, "application/octet-stream");
            _setCorsHeaders(response);
            request->send(response);
        } else if (LittleFS.exists(path)) {
            AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, "application/octet-stream");
            _setCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "text/plain", "file_not_found");
        }
    });

    // ========================================================================
    // 4. 시스템 제어 및 OTA
    // ========================================================================
    _server.on("/api/t20/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    _server.on("/api/t20/runtime_config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (LittleFS.exists(T20::C10_Path::FILE_CFG_JSON)) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, T20::C10_Path::FILE_CFG_JSON, "application/json");
            _setCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "application/json", "{}");
        }
    });
    
    _server.on("/api/t20/runtime_config", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(400, "application/json", "{\"ok\":false,\"msg\":\"no_body\"}");
        },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {

            if (index == 0) {
                // 악의적이거나 비정상적인 대용량 Content-Length로 인한 OOM 패닉 원천 방어
                if (total > T20::C10_Web::LARGE_JSON_BUF_SIZE) {
                    request->send(413, "application/json", "{\"ok\":false,\"msg\":\"payload_too_large\"}");
                    return;
                }
                
                request->_tempObject = malloc(total + 1);
                request->onDisconnect([request]() {
                    if (request->_tempObject) {
                        free(request->_tempObject);
                        request->_tempObject = NULL;
                    }
                });
            }
            
            uint8_t* buffer = (uint8_t*)request->_tempObject;
            if (!buffer) {
                if (!request->client()->disconnected()) {
                    request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom_or_rejected\"}");
                }
                return;
            }

            memcpy(buffer + index, data, len);

			if (index + len == total) {
				buffer[total] = '\0';

				JsonDocument doc;
				DeserializationError err = deserializeJson(doc, buffer);

				if (!err) {
					File f = LittleFS.open(T20::C10_Path::FILE_CFG_JSON, "w");
					if (f) {
						serializeJson(doc, f);
						f.close();
						request->send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting\"}");
						delay(500);
						ESP.restart();
					} else {
						request->send(500, "application/json", "{\"ok\":false,\"msg\":\"fs_error\"}");
					}
				} else {
					request->send(400, "application/json", "{\"ok\":false,\"msg\":\"json_error\"}");
				}
				free(buffer);
				request->_tempObject = NULL;
            } else {
                request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom\"}");
            }
        }
    );
    
    _server.on("/api/t20/ota_update", HTTP_POST, 
        [this](AsyncWebServerRequest *request) {
            bool success = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(success ? 200 : 500, "application/json", success ? "{\"ok\":true}" : "{\"ok\":false}");
            _setCorsHeaders(response);
            request->send(response);
            if (success) {
                delay(1000);
                ESP.restart(); 
            }
        },
        [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA] Update Start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError()) {
                if (Update.write(data, len) != len) {
                    Update.printError(Serial);
                }
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Update Success: %u Bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    // ========================================================================
    // 5. 프론트엔드 정적 파일 서빙
    // ========================================================================
    _server.serveStatic("/", LittleFS, "/www").setDefaultFile(T20::C10_Path::WEB_INDEX);
}

void CL_T20_CommService::broadcastBinary(const float* p_buffer, size_t len) {
    if (_ws.count() == 0 || !p_buffer) return;

    if (_ws.availableForWriteAll()) {
        _ws.binaryAll((uint8_t*)p_buffer, len * sizeof(float));
    }
}

uint32_t CL_T20_CommService::calcStatusHash(uint32_t frame_id, uint32_t rec_count, bool measuring) {
    uint32_t h = 2166136261UL;
    h ^= frame_id;  h *= 16777619UL;
    h ^= rec_count; h *= 16777619UL;
    h ^= (measuring ? 0x80000000UL : 0UL);
    return h;
}

void CL_T20_CommService::_setCorsHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void CL_T20_CommService::_sendJson(AsyncWebServerRequest* request, const JsonDocument& doc) {
    AsyncResponseStream *stream = request->beginResponseStream("application/json");
    _setCorsHeaders(stream);
    serializeJson(doc, *stream);
    request->send(stream);
}

// ========================================================================
// MQTT 운영 로직
// ========================================================================
void CL_T20_CommService::runMqtt() {
    if (!_mqtt_cfg.enable || !isConnected()) return;

    if (!_mqtt_client.connected()) {
        _reconnectMqtt();
    } else {
        _mqtt_client.loop(); 
    }
}

void CL_T20_CommService::_reconnectMqtt() {
    if (millis() - _last_mqtt_retry_ms > 5000) {
        Serial.print(F("[MQTT] Attempting connection..."));
        
        bool connected = false;
        if (strlen(_mqtt_cfg.password) > 0) {
            connected = _mqtt_client.connect(_mqtt_cfg.id, _mqtt_cfg.id, _mqtt_cfg.password);
        } else {
            connected = _mqtt_client.connect(_mqtt_cfg.id);
        }

        if (connected) {
            Serial.println(F(" connected"));
        } else {
            Serial.printf(" failed, rc=%d\n", _mqtt_client.state());
        }
        _last_mqtt_retry_ms = millis();
    }
}

bool CL_T20_CommService::publishMqtt(const char* sub_topic, const JsonDocument& doc) {
    if (!_mqtt_cfg.enable || !_mqtt_client.connected()) return false;

    char full_topic[128];
    snprintf(full_topic, sizeof(full_topic), "%s/%s", _mqtt_cfg.topic_root, sub_topic);

    char payload[512];
    serializeJson(doc, payload, sizeof(payload));

    return _mqtt_client.publish(full_topic, payload);
}
