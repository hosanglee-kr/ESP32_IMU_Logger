/* ============================================================================
 * File: T250_Web_215.cpp
 * Summary: Web API 엔드포인트 및 핸들러 구현 (WebSocket 스트리밍 통합 및 리팩토링)
 * * [v215 리팩토링 및 최적화 사항]
 * 1. 매직 스트링 제거 및 T250_Web 네임스페이스에 constexpr 상수로 통합
 * 2. gnu++17 환경에 맞춘 컴파일 타임 상수(constexpr) 적용으로 런타임 오버헤드 감소
 * 3. HTTP 헤더 및 Mime-type 상수화로 메모리 효율성 극대화
 * 4. WebSocket 경로와 REST API 기본 경로 간의 동기화 구조 개선
 ============================================================================ */

#include "T250_Web_214.h"


// [WebSocket] 글로벌 인스턴스 선언
AsyncWebSocket ws(T250_Web::ROUTE_WS);

// --- 전방 선언 (Forward Declarations) ---
void T20_registerControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerSensorDiagHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerDataHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerFileStreamingHandler(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerNoiseControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server);


/* ----------------------------------------------------------------------------
 * 공통 헬퍼 함수
 * ---------------------------------------------------------------------------- */
static void T20_sendJsonText(AsyncWebServerRequest* request, bool ok, const char* json_ok) {
    if (request == nullptr) return;
    if (ok && json_ok != nullptr)
        request->send(200, T250_Web::MIME_JSON, json_ok);
    else
        request->send(500, T250_Web::MIME_JSON, T250_Web::JSON_RES_FAIL);
}

/* ----------------------------------------------------------------------------
 * WebSocket 이벤트 핸들러 및 브로드캐스트
 * ---------------------------------------------------------------------------- */
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client[%u] connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client[%u] disconnected\n", client->id());
    }
}

void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p) {
    if (ws.count() == 0 || p == nullptr || !p->latest_vector_valid) return;

    static float ws_buffer[424];
    
    memcpy(&ws_buffer[0], p->viewer_last_waveform, 256 * sizeof(float));
    memcpy(&ws_buffer[256], p->viewer_last_spectrum, 129 * sizeof(float));
    memcpy(&ws_buffer[385], p->latest_feature.vector, 39 * sizeof(float));

    ws.binaryAll((uint8_t*)ws_buffer, sizeof(ws_buffer));
}


/* ----------------------------------------------------------------------------
 * 1. Recorder 제어 및 세션 관리 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + T250_Web::ROUTE_REC_BEGIN).c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderBegin(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_started\"}");
    });

    v_server->on((base + T250_Web::ROUTE_REC_END).c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderEnd(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_stopped\"}");
    });

    v_server->on((base + T250_Web::ROUTE_REC_FINALIZE).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderFinalizeJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + T250_Web::ROUTE_REC_FIN_PIPE).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderFinalizeBundleJsonText(p, json, sizeof(json)), json);
    });
}

/* ----------------------------------------------------------------------------
 * 2. BMI270 센서 상세 진단 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerSensorDiagHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + T250_Web::ROUTE_BMI_ACTUAL).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildIoSyncBundleJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + T250_Web::ROUTE_BMI_BRIDGE).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["hw_bridge"] = T20_StateToString(p->bmi_state.hw_link);
        doc["isr_bridge"] = T20_StateToString(p->bmi_runtime.isr_hook);
        doc["drdy_flag"] = p->bmi270_drdy_isr_flag;
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });

    v_server->on((base + T250_Web::ROUTE_BMI_VERIFY).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["verify_state"] = T20_StateToString(p->bmi_state.init);
        doc["chip_id"] = p->bmi270_chip_id;
        doc["spi_ok"] = p->bmi270_spi_ok;
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });
}

/* ----------------------------------------------------------------------------
 * 3. 데이터 프리뷰, 모니터링 및 메타데이터 관리
 * ---------------------------------------------------------------------------- */
void T20_registerDataHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + T250_Web::ROUTE_TYPE_PREV_LOAD).c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        bool ok = T20_loadTypePreviewText(p, path);
        if(ok) {
            T20_updateTypePreviewSchemaGuess(p);
            T20_updateTypePreviewSamples(p);
        }
        T20_sendJsonText(request, ok, T250_Web::JSON_RES_OK);
    });

    v_server->on((base + T250_Web::ROUTE_REC_CSV_ADV).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        uint16_t page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 1000);

        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool ok = T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, 4096, nullptr, nullptr, 0, 0, page, 20);
        T20_sendJsonText(request, ok, json);
    });
    
    v_server->on((base + T250_Web::ROUTE_DSP_NOISE).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["is_learning"] = p->noise_learning_active;
        doc["learned_frames"] = p->noise_learned_frames;
        JsonArray spectrum = doc["spectrum"].to<JsonArray>();
        for(int i=0; i < 129; i++) spectrum.add(p->noise_spectrum[i]);
        
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE];
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });

    v_server->on((base + T250_Web::ROUTE_LIVE_DEBUG).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        float hz = (p->viewer_last_frame_id > 0) ? 
                   (1000.0f / (millis() - p->last_frame_process_ms + 1)) : 0.0f;
                   
        doc["process_hz"] = hz;
        doc["integrity"]["dropped_frames"] = p->dropped_frames;
        doc["integrity"]["dma_overflows"] = p->recorder_last_error[0] == 'd' ? 1 : 0;
        doc["sample_count"] = p->bmi270_sample_counter;
        doc["frame_id"] = p->viewer_last_frame_id;
        doc["hop_size"] = p->cfg.feature.hop_size;
        doc["fft_size"] = G_T20_FFT_SIZE;
        
        char json[G_T20_WEB_JSON_BUF_SIZE];
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });
}

/* ----------------------------------------------------------------------------
 * 4. 파일 스트리밍 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerFileStreamingHandler(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + T250_Web::ROUTE_REC_DOWNLOAD).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128];
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, T250_Web::MIME_PLAIN, "path_required"); return;
        }

        File file = (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(path, "r") : LittleFS.open(path, "r");
        
        if (!file) {
            request->send(404, T250_Web::MIME_PLAIN, "file_not_found"); return;
        }

        AsyncWebServerResponse *response = request->beginResponse(
            file, 
            String(path), 
            String(T250_Web::MIME_OCTET)
        );
        
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });
}

/* ----------------------------------------------------------------------------
 * 5. 노이즈 학습 컨트롤 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerNoiseControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + T250_Web::ROUTE_NOISE_LEARN).c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        if (request->hasParam("active")) {
            p->noise_learning_active = (request->getParam("active")->value() == "true");
            T20_sendJsonText(request, true, T250_Web::JSON_RES_OK);
        } else {
            T20_sendJsonText(request, false, nullptr);
        }
    });
}

/* ----------------------------------------------------------------------------
 * 상태 해시 연산
 * ---------------------------------------------------------------------------- */
uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return 0;
    
    uint32_t h = 2166136261UL;
    h ^= p->viewer_last_frame_id;      h *= 16777619UL; 
    h ^= p->recorder_record_count;     h *= 16777619UL; 
    h ^= (uint32_t)(p->bmi270_last_axis_values[2] * 1000); 
    h ^= (p->measurement_active ? 0x80000000 : 0); 
    return h;
}

/* ----------------------------------------------------------------------------
 * 6. 프론트엔드 정적 파일 서빙 핸들러 (LittleFS 연동)
 * ---------------------------------------------------------------------------- */
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server) {
    if (v_server == nullptr) return;

    v_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, T250_Web::FILE_INDEX_HTML, T250_Web::MIME_HTML);
    });

    v_server->serveStatic("/", LittleFS, "/")
            .setDefaultFile(T250_Web::FILE_INDEX_HTML + 1) // 앞에 붙은 '/' 제거용
            .setCacheControl(T250_Web::CACHE_CONTROL);

    v_server->onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, T250_Web::MIME_PLAIN, "404: Not Found in LittleFS");
        }
    });
}

/* ----------------------------------------------------------------------------
 * 메인 등록 함수 (모든 핸들러 취합)
 * ---------------------------------------------------------------------------- */
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path) {
    if (p == nullptr || v_server == nullptr) return;
    String base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    v_server->on((base + T250_Web::ROUTE_VIEWER_DATA).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerDataJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + T250_Web::ROUTE_STATUS).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["hash"] = T20_calcStatusHash(p);
        doc["running"] = p->running;
        doc["measuring"] = p->measurement_active;
        char json[256];
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });

    T20_registerControlHandlers(p, v_server, base);
    T20_registerSensorDiagHandlers(p, v_server, base);
    T20_registerDataHandlers(p, v_server, base);
    T20_registerFileStreamingHandler(p, v_server, base);
    T20_registerNoiseControlHandlers(p, v_server, base);

    ws.onEvent(onWsEvent);
    v_server->addHandler(&ws);

    v_server->on((base + T250_Web::ROUTE_BUILD_SANITY).c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildBuildSanityJsonText(p, json, sizeof(json)), json);
    });
    
    // [최종 추가] 백엔드 API 등록이 모두 끝난 후 정적 프론트엔드 라우트 등록
    T20_registerStaticFrontendHandlers(v_server);
}
