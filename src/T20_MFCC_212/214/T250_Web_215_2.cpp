/* ============================================================================
 * File: T250_Web_215.cpp
 * Summary: Web API 엔드포인트 및 핸들러 구현 (WebSocket 스트리밍 통합 완성본)
 * * [v215 리팩토링 사항]
 * 1. T250_Web namespace를 통한 하드코딩 매직 넘버 및 문자열 상수화 (C++17 constexpr)
 * 2. WebSocket 바이너리 버퍼 병합 연산의 메모리 오프셋 최적화
 * 3. HTTP MIME 타입 및 공통 응답 문자열의 재사용을 통한 스택/힙 오버헤드 감소
 * 4. FNV-1a 상태 해시 연산 인자의 타입 명확화
 ============================================================================ */

#include "T250_Web_214.h"


// [WebSocket] 글로벌 인스턴스 선언
AsyncWebSocket ws(T250_Web::WS_URI);

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
        request->send(500, T250_Web::MIME_JSON, T250_Web::JSON_FAIL);
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

    // 패킷 구조: Waveform(256) + Spectrum(129) + MFCC(39) = 424 floats (1696 Bytes)
    static float ws_buffer[T250_Web::BINARY_BUF_LEN];
    
    size_t offset = 0;
    memcpy(&ws_buffer[offset], p->viewer_last_waveform, T250_Web::WAVEFORM_LEN * sizeof(float));
    offset += T250_Web::WAVEFORM_LEN;

    memcpy(&ws_buffer[offset], p->viewer_last_spectrum, T250_Web::SPECTRUM_LEN * sizeof(float));
    offset += T250_Web::SPECTRUM_LEN;

    memcpy(&ws_buffer[offset], p->latest_feature.vector, T250_Web::MFCC_LEN * sizeof(float));

    ws.binaryAll((uint8_t*)ws_buffer, sizeof(ws_buffer));
}

/* ----------------------------------------------------------------------------
 * 1. Recorder 제어 및 세션 관리 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + "/recorder_begin").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderBegin(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_started\"}");
    });

    v_server->on((base + "/recorder_end").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderEnd(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_stopped\"}");
    });

    v_server->on((base + "/recorder_finalize").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderFinalizeJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + "/recorder_finalize_pipeline").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderFinalizeBundleJsonText(p, json, sizeof(json)), json);
    });
}

/* ----------------------------------------------------------------------------
 * 2. BMI270 센서 상세 진단 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerSensorDiagHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + "/bmi270_actual_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildIoSyncBundleJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + "/bmi270_bridge_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["hw_bridge"] = T20_StateToString(p->bmi_state.hw_link);
        doc["isr_bridge"] = T20_StateToString(p->bmi_runtime.isr_hook);
        doc["drdy_flag"] = p->bmi270_drdy_isr_flag;
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });

    v_server->on((base + "/bmi270_verify_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
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
    v_server->on((base + "/type_preview_load").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        bool ok = T20_loadTypePreviewText(p, path);
        if(ok) {
            T20_updateTypePreviewSchemaGuess(p);
            T20_updateTypePreviewSamples(p);
        }
        T20_sendJsonText(request, ok, T250_Web::JSON_OK);
    });

    v_server->on((base + "/recorder/file_csv_table_advanced").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        uint16_t page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 1000);

        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool ok = T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, 4096, nullptr, nullptr, 0, 0, page, 20);
        T20_sendJsonText(request, ok, json);
    });
    
    v_server->on((base + "/dsp/noise").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["is_learning"] = p->noise_learning_active;
        doc["learned_frames"] = p->noise_learned_frames;
        JsonArray spectrum = doc["spectrum"].to<JsonArray>();
        for(size_t i = 0; i < T250_Web::SPECTRUM_LEN; i++) spectrum.add(p->noise_spectrum[i]);
        
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE];
        serializeJson(doc, json, sizeof(json));
        request->send(200, T250_Web::MIME_JSON, json);
    });

    v_server->on((base + "/live_debug").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        
        float hz = (p->viewer_last_frame_id > 0) ? 
                   (1000.0f / (float)(millis() - p->last_frame_process_ms + 1)) : 0.0f;
                   
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
    v_server->on((base + "/recorder/download").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128];
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, T250_Web::MIME_TEXT, "path_required"); 
            return;
        }

        File file = (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(path, "r") : LittleFS.open(path, "r");
        
        if (!file) {
            request->send(404, T250_Web::MIME_TEXT, "file_not_found"); 
            return;
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
    v_server->on((base + "/noise_learn").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        if (request->hasParam("active")) {
            p->noise_learning_active = (request->getParam("active")->value() == "true");
            T20_sendJsonText(request, true, T250_Web::JSON_OK);
        } else {
            T20_sendJsonText(request, false, nullptr);
        }
    });
}

/* ----------------------------------------------------------------------------
 * 6. 프론트엔드 정적 파일 서빙 핸들러 (LittleFS 연동)
 * ---------------------------------------------------------------------------- */
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server) {
    if (v_server == nullptr) return;

    v_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String path = String("/") + String(T250_Web::INDEX_FILE);
        request->send(LittleFS, path, T250_Web::MIME_HTML);
    });

    v_server->serveStatic("/", LittleFS, "/")
            .setDefaultFile(T250_Web::INDEX_FILE)
            .setCacheControl(T250_Web::CACHE_CTRL);

    v_server->onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, T250_Web::MIME_TEXT, "404: Not Found in LittleFS");
        }
    });
}

/* ----------------------------------------------------------------------------
 * 상태 해시 연산
 * ---------------------------------------------------------------------------- */
uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return 0;
    
    uint32_t h = T250_Web::HASH_OFFSET_BASIS;
    h ^= p->viewer_last_frame_id;      h *= T250_Web::HASH_PRIME; 
    h ^= p->recorder_record_count;     h *= T250_Web::HASH_PRIME; 
    h ^= (uint32_t)(p->bmi270_last_axis_values[2] * 1000.0f); 
    h ^= (p->measurement_active ? T250_Web::MEASURE_FLAG_BIT : 0UL); 
    return h;
}

/* ----------------------------------------------------------------------------
 * 메인 등록 함수 (모든 핸들러 취합)
 * ---------------------------------------------------------------------------- */
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path) {
    if (p == nullptr || v_server == nullptr) return;
    String base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    v_server->on((base + "/viewer_data").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerDataJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + "/status").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
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

    v_server->on((base + "/build_sanity").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildBuildSanityJsonText(p, json, sizeof(json)), json);
    });
    
    // 정적 프론트엔드 라우트는 다른 API 엔드포인트에 영향을 주지 않도록 가장 마지막에 등록
    T20_registerStaticFrontendHandlers(v_server);
}
