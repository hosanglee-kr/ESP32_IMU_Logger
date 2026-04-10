/* ============================================================================
 * File: T240_Comm_Service_219.cpp
 * Summary: Network & Web Communication Engine Implementation (v217)
 * ========================================================================== */

#include "T240_Comm_Service_219.h"
#include "T221_Mfcc_Inter_219.h" // ST_Impl 의존성 (엔진간 상호작용 용도)
#include <LittleFS.h>
#include <SD_MMC.h>
#include <time.h> // NTP 처리를 위한 표준 time 라이브러리


CL_T20_CommService::CL_T20_CommService()
    : _server(80), _ws(T20::C10_Web::WS_URI) {}

CL_T20_CommService::~CL_T20_CommService() {
    _ws.closeAll();
    _server.end();
}

bool CL_T20_CommService::begin(const ST_T20_ConfigWiFi_t& w_cfg) {
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
    // 1. 상태 및 모니터링 API
    // ========================================================================
    _server.on("/api/t20/status", HTTP_GET, [this, p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["running"] = p->running;
        doc["hash"] = calcStatusHash(0, p->storage.getRecordCount(), p->running);
        doc["sensor_status"] = p->sensor.getStatusText();
        doc["storage_open"] = p->storage.isOpen();
        _sendJson(request, doc);
    });

    // ========================================================================
    // 2. 레코더 및 파일 스트리밍 API
    // ========================================================================
    _server.on("/api/t20/recorder_begin", HTTP_POST, [p](AsyncWebServerRequest* request) {
        p->measurement_active = true; // 플래그만 세팅하면 T230의 run()이 알아서 세션을 엽니다.
        request->send(200, "application/json", "{\"ok\":true}");
    });

    _server.on("/api/t20/recorder_end", HTTP_POST, [p](AsyncWebServerRequest* request) {
        p->measurement_active = false;
        request->send(200, "application/json", "{\"ok\":true}");
    });

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
    // 3. 센서 제어 및 진단 API
    // ========================================================================
    _server.on("/api/t20/calibrate", HTTP_POST, [p, this](AsyncWebServerRequest* request) {
        bool ok = p->sensor.runCalibration();
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    _server.on("/api/t20/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"ok\":true}");
        delay(500);
        ESP.restart();
    });

    _server.on("/api/t20/noise_learn", HTTP_POST, [p, this](AsyncWebServerRequest* request) {
        if (request->hasParam("active")) {
            bool active = (request->getParam("active")->value() == "true");
            p->dsp.setNoiseLearning(active);
            request->send(200, "application/json", "{\"ok\":true}");
        } else {
            request->send(400, "application/json", "{\"ok\":false}");
        }
    });

    // ========================================================================
    // 4. 설정 로드/저장 API
    // ========================================================================
        _server.on("/api/t20/runtime_config", HTTP_GET, [this](AsyncWebServerRequest* request) {
        // T210의 상수를 이용해 파일 오픈
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

			// 방어 코드: 연결 끊김 시 메모리 릭 방지
            if (index == 0) {
                request->_tempObject = malloc(total + 1);
                request->onDisconnect([request]() {
                    if (request->_tempObject) {
                        free(request->_tempObject);
                        request->_tempObject = NULL;
                    }
                });
            }

            uint8_t* buffer = (uint8_t*)request->_tempObject;
            // 예외 방어: 할당 실패 시 즉시 종료
            if (!buffer) {
                request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom\"}");
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
						// 응답 후 0.5초 뒤 ESP 재시작 (설정 즉시 적용)
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






