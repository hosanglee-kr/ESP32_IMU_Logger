/* ============================================================================
 * File: T240_Comm_Service_217.cpp
 * Summary: Network & Web Communication Engine Implementation (v217)
 * ========================================================================== */

#include "T240_Comm_Service_217.h"
#include "T221_Mfcc_Inter_217.h" // ST_Impl 의존성 (엔진간 상호작용 용도)
#include <LittleFS.h>
#include <SD_MMC.h>

CL_T20_CommService::CL_T20_CommService() 
    : _server(80), _ws(T20::C10_Web::WS_URI) {}

CL_T20_CommService::~CL_T20_CommService() {
    _ws.closeAll();
    _server.end();
}

bool CL_T20_CommService::begin(const ST_T20_ConfigWiFi_t& w_cfg) {
    WiFi.mode(WIFI_OFF);
    delay(50);

    // [1] AP 모드 및 Fallback 설정
    if (w_cfg.mode == EN_T20_WIFI_AP_ONLY || w_cfg.mode == EN_T20_WIFI_AP_STA || w_cfg.mode == EN_T20_WIFI_AUTO_FALLBACK) {
        WiFi.softAP(w_cfg.ap_ssid, w_cfg.ap_password);
    }

    // [2] STA 모드 및 고정 IP 설정
    if (w_cfg.mode != EN_T20_WIFI_AP_ONLY) {
        WiFi.mode(w_cfg.mode == EN_T20_WIFI_AP_STA ? WIFI_AP_STA : WIFI_STA);
        
        if (w_cfg.use_static_ip) {
            IPAddress ip, gw, sn, d1, d2;
            if (ip.fromString(w_cfg.local_ip) && gw.fromString(w_cfg.gateway) && sn.fromString(w_cfg.subnet)) {
                if (w_cfg.dns1[0] != '\0') d1.fromString(w_cfg.dns1);
                if (w_cfg.dns2[0] != '\0') d2.fromString(w_cfg.dns2);
                WiFi.config(ip, gw, sn, d1, d2);
            }
        }

        // 다중 AP 등록 (WiFi Multi)
        for (int i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
            if (w_cfg.multi_ap[i].ssid[0] != '\0') {
                _wifi_multi.addAP(w_cfg.multi_ap[i].ssid, w_cfg.multi_ap[i].password);
            }
        }

        // 접속 시도
        uint32_t start_ms = millis();
        while (_wifi_multi.run() != WL_CONNECTED && (millis() - start_ms < 5000)) {
            delay(200);
        }
    }

    // [3] WebSocket 바인딩
    _ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t, void* a, uint8_t* d, size_t l) {
        // WS 연결 이벤트 핸들링 (v216과 동일)
    });
    _server.addHandler(&_ws);

    // [4] CORS Preflight 전역 허용 (v216 누락분 복원)
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
    // 2. 레코더 및 파일 스트리밍 API (v216 누락분 복원)
    // ========================================================================
    _server.on("/api/t20/recorder_begin", HTTP_POST, [p](AsyncWebServerRequest* request) {
        ST_T20_RecorderBinaryHeader_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = T20::C10_Rec::BINARY_MAGIC;
        hdr.sample_rate_hz = (uint32_t)T20::C10_DSP::SAMPLE_RATE_HZ;
        hdr.fft_size = T20::C10_DSP::FFT_SIZE;
        hdr.mfcc_dim = p->cfg.feature.mfcc_coeffs;

        bool ok = p->storage.openSession(hdr);
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    _server.on("/api/t20/recorder_end", HTTP_POST, [p](AsyncWebServerRequest* request) {
        p->storage.closeSession("end_by_api");
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
    // 3. 센서 제어 및 진단 API (v216 누락분 복원)
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
        if (LittleFS.exists("/sys/runtime_cfg.json")) {
            AsyncWebServerResponse* response = request->beginResponse(LittleFS, "/sys/runtime_cfg.json", "application/json");
            _setCorsHeaders(response);
            request->send(response);
        } else {
            request->send(404, "application/json", "{}");
        }
    });

    _server.on("/api/t20/runtime_config", HTTP_POST, 
        [](AsyncWebServerRequest *request) {
            // 본문 처리가 완료되지 않았을 때의 Fallback 응답
            request->send(400, "application/json", "{\"ok\":false,\"msg\":\"no_body\"}");
        },
        NULL, // Upload 핸들러 사용 안 함
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            // ArduinoJson V7 호환성을 위해 AsyncJson 우회 및 직접 파싱
            if (index == 0) { 
                request->_tempObject = malloc(total + 1); // 메모리 할당
            }
            
            uint8_t* buffer = (uint8_t*)request->_tempObject;
            if (buffer) {
                memcpy(buffer + index, data, len); // 청크 조립
                
                // 데이터 수신 완료 시
                if (index + len == total) {
                    buffer[total] = '\0'; // 문자열 끝(Null) 처리
                    
                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, buffer);
                    
                    if (!err) {
                        File f = LittleFS.open("/sys/runtime_cfg.json", "w");
                        if (f) {
                            serializeJson(doc, f);
                            f.close();
                            request->send(200, "application/json", "{\"ok\":true}");
                        } else {
                            request->send(500, "application/json", "{\"ok\":false,\"msg\":\"fs_error\"}");
                        }
                    } else {
                        request->send(400, "application/json", "{\"ok\":false,\"msg\":\"json_error\"}");
                    }
                    free(buffer); // 메모리 해제
                    request->_tempObject = NULL;
                }
            } else {
                request->send(500, "application/json", "{\"ok\":false,\"msg\":\"oom\"}");
            }
        }
    );


    // ========================================================================
    // 5. 프론트엔드 정적 파일 서빙
    // ========================================================================
    _server.serveStatic("/", LittleFS, "/www").setDefaultFile("index_214_003.html");
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
