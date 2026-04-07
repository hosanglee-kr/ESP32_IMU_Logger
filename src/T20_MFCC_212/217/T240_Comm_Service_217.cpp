/* ============================================================================
 * File: T240_Comm_Service_217.cpp
 * Summary: Network & Web Communication Engine Implementation (v217)
 * Compiler: gnu++17 / Async Processing
 * ========================================================================== */

#include "T240_Comm_Service_217.h"
#include "T221_Mfcc_Inter_217.h" // ST_Impl 접근용

CL_T20_CommService::CL_T20_CommService() 
    : _server(80), _ws(T20::C10_Web::WS_URI) {}

CL_T20_CommService::~CL_T20_CommService() {
    _ws.closeAll();
    _server.end();
}

bool CL_T20_CommService::begin(const ST_T20_ConfigWiFi_t& w_cfg) {
    WiFi.mode(WIFI_OFF);
    delay(50);

    // [1] AP 모드 설정 (AP_STA 또는 AP_ONLY)
    if (w_cfg.mode == EN_T20_WIFI_AP_ONLY || w_cfg.mode == EN_T20_WIFI_AP_STA || w_cfg.mode == EN_T20_WIFI_AUTO_FALLBACK) {
        WiFi.softAP(w_cfg.ap_ssid, w_cfg.ap_password);
    }

    // [2] STA 모드 설정 및 Multi-AP 등록
    if (w_cfg.mode != EN_T20_WIFI_AP_ONLY) {
        WiFi.mode(w_cfg.mode == EN_T20_WIFI_AP_STA ? WIFI_AP_STA : WIFI_STA);
        
        // 고정 IP 설정
        if (w_cfg.use_static_ip) {
            IPAddress ip, gw, sn;
            if (ip.fromString(w_cfg.local_ip) && gw.fromString(w_cfg.gateway) && sn.fromString(w_cfg.subnet)) {
                WiFi.config(ip, gw, sn);
            }
        }

        for (int i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
            if (w_cfg.multi_ap[i].ssid[0] != '\0') {
                _wifi_multi.addAP(w_cfg.multi_ap[i].ssid, w_cfg.multi_ap[i].password);
            }
        }

        // 연결 시도 (비블로킹 권장이나 초기 부팅 시에는 일정 시간 대기)
        uint32_t start_ms = millis();
        while (_wifi_multi.run() != WL_CONNECTED && (millis() - start_ms < 5000)) {
            delay(200);
        }
    }

    // [3] WebSocket 및 라우팅 활성화
    _ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t, void* a, uint8_t* d, size_t l) {
        if (t == WS_EVT_CONNECT) Serial.printf("[WS] Client %u connected\n", c->id());
    });
    _server.addHandler(&_ws);

    _server.begin();
    return true;
}

void CL_T20_CommService::initHandlers(void* p_master_impl) {
    CL_T20_Mfcc::ST_Impl* p = (CL_T20_Mfcc::ST_Impl*)p_master_impl;
    if (!p) return;

    // --- 기본 상태 API ---
    _server.on("/api/t20/status", HTTP_GET, [this, p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["running"] = p->running;
        doc["hash"] = calcStatusHash(0, p->storage.getRecordCount(), 0.0f, p->running);
        _sendJson(request, doc);
    });

    // --- 레코더 제어 API ---
    _server.on("/api/t20/recorder_begin", HTTP_POST, [p](AsyncWebServerRequest* request) {
        ST_T20_RecorderBinaryHeader_t hdr;
        hdr.magic = T20::C10_Rec::BINARY_MAGIC;
        hdr.sample_rate_hz = (uint32_t)T20::C10_DSP::SAMPLE_RATE_HZ;
        bool ok = p->storage.openSession(hdr);
        request->send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    _server.on("/api/t20/recorder_end", HTTP_POST, [p](AsyncWebServerRequest* request) {
        p->storage.closeSession();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    // --- 설정 로드/저장 API ---
    _server.on("/api/t20/runtime_config", HTTP_GET, [p, this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["hop_size"] = p->cfg.feature.hop_size;
        doc["mfcc_coeffs"] = p->cfg.feature.mfcc_coeffs;
        doc["recorder_enabled"] = p->cfg.recorder.enabled;
        _sendJson(request, doc);
    });

    // --- 정적 파일 서빙 (LittleFS) ---
    _server.serveStatic("/", LittleFS, T20::C10_Path::DIR_WEB).setDefaultFile(T20::C10_Path::WEB_INDEX);
}

void CL_T20_CommService::broadcastBinary(const float* p_buffer, size_t len) {
    if (_ws.count() == 0 || !p_buffer) return;
    
    // WebSocket 큐 오버플로 방지
    if (_ws.availableForWriteAll()) {
        _ws.binaryAll((uint8_t*)p_buffer, len * sizeof(float));
    }
}

uint32_t CL_T20_CommService::calcStatusHash(uint32_t frame_id, uint32_t rec_count, float z_axis, bool measuring) {
    uint32_t h = 2166136261UL;
    h ^= frame_id; h *= 16777619UL;
    h ^= rec_count; h *= 16777619UL;
    h ^= (measuring ? 0x80000000UL : 0UL);
    return h;
}

void CL_T20_CommService::_setCorsHeaders(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

void CL_T20_CommService::_sendJson(AsyncWebServerRequest* request, const JsonDocument& doc) {
    AsyncResponseStream *stream = request->beginResponseStream("application/json");
    _setCorsHeaders(stream);
    serializeJson(doc, *stream);
    request->send(stream);
}
