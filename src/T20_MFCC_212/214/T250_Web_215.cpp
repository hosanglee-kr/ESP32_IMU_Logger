/* ============================================================================
 * File: T250_Web_215.cpp
 * Summary: Web API 엔드포인트 및 핸들러 구현 (WebSocket 스트리밍 통합 완성본)
 * * [v214 최종 구현 사항]
 * 1. v210의 파편화된 상태값을 v212 그룹 구조체(bmi_state, rec_state)에 매핑
 * 2. Recorder 세션 제어 및 Finalize 단계별 모니터링 엔드포인트 활성화
 * 3. /live_debug 응답의 중복 키 제거 및 FPS 연산 안전성 확보
 * 4. AsyncWebSocket을 이용한 고속 바이너리 스트리밍(개선안 B) 통합
 * 5. 누락되었던 핸들러(파일 다운로드, 노이즈 제어) 등록부 연결 완료
 ============================================================================ */

// TODO
// - 기능 점검 및 개선
// - T250_Web_215.cpp 소스 상의 고정 상수 성격 별도 상수변수로 정의 등 리팩토링
// - 상수 namespace 이름은 T250_Web
// - gnu++17 환경이야
// - 성능 최적화 


#include "T250_Web_214.h"

// [WebSocket] 글로벌 인스턴스 선언 (바이너리 스트리밍 용도)
AsyncWebSocket ws("/api/t20/ws");

// --- 전방 선언 (Forward Declarations) ---
void T20_registerControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerSensorDiagHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerDataHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerFileStreamingHandler(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);
void T20_registerNoiseControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base);


/* ----------------------------------------------------------------------------
 * 공통 헬퍼 함수
 * ---------------------------------------------------------------------------- */
static void T20_sendJsonText(AsyncWebServerRequest* request, bool ok, const char* json_ok) {
    if (request == nullptr) return;
    if (ok && json_ok != nullptr)
        request->send(200, "application/json; charset=utf-8", json_ok);
    else
        request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
}

/* ----------------------------------------------------------------------------
 * WebSocket 이벤트 핸들러 및 브로드캐스트 (개선안 B)
 * ---------------------------------------------------------------------------- */
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WS Client[%u] connected\n", client->id());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WS Client[%u] disconnected\n", client->id());
    }
}

// 이 함수는 T20_processTask 등 데이터 연산이 끝나는 시점에서 주기적으로 호출해주면 됩니다.
void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p) {
    if (ws.count() == 0 || p == nullptr || !p->latest_vector_valid) return;

    // 패킷 구조: Waveform(256) + Spectrum(129) + MFCC(39) = 424 floats (1696 Bytes)
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
        request->send(200, "application/json", json);
    });

    v_server->on((base + "/bmi270_verify_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["verify_state"] = T20_StateToString(p->bmi_state.init);
        doc["chip_id"] = p->bmi270_chip_id;
        doc["spi_ok"] = p->bmi270_spi_ok;
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, "application/json", json);
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
        T20_sendJsonText(request, ok, "{\"ok\":true}");
    });

    v_server->on((base + "/recorder/file_csv_table_advanced").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        uint16_t page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 1000);

        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool ok = T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, 4096, nullptr, nullptr, 0, 0, page, 20);
        T20_sendJsonText(request, ok, json);
    });
    
    // 노이즈 프로파일 조회 (Front-end 차트용)
    v_server->on((base + "/dsp/noise").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["is_learning"] = p->noise_learning_active;
        doc["learned_frames"] = p->noise_learned_frames;
        JsonArray spectrum = doc["spectrum"].to<JsonArray>();
        for(int i=0; i < 129; i++) spectrum.add(p->noise_spectrum[i]);
        
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE];
        serializeJson(doc, json, sizeof(json));
        request->send(200, "application/json", json);
    });

    // 진단 정보 보강 (중복 키 제거 및 계산 최적화 완료)
    v_server->on((base + "/live_debug").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
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
        request->send(200, "application/json", json);
    });
}

/* ----------------------------------------------------------------------------
 * 4. 파일 스트리밍 엔드포인트
 * ---------------------------------------------------------------------------- */
void T20_registerFileStreamingHandler(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    v_server->on((base + "/recorder/download").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128];
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "text/plain", "path_required"); return;
        }

        File file = (p->recorder_storage_backend == EN_T20_STORAGE_SDMMC) ? SD_MMC.open(path, "r") : LittleFS.open(path, "r");
        
        if (!file) {
            request->send(404, "text/plain", "file_not_found"); return;
        }

        // 모호성(Ambiguity) 에러를 방지하기 위해 String 클래스로 명시적 캐스팅
        AsyncWebServerResponse *response = request->beginResponse(
            file, 
            String(path), 
            String("application/octet-stream")
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
            T20_sendJsonText(request, true, "{\"ok\":true}");
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
    
    // FNV-1a Hash 알고리즘 응용
    uint32_t h = 2166136261UL;
    h ^= p->viewer_last_frame_id;      h *= 16777619UL; 
    h ^= p->recorder_record_count;     h *= 16777619UL; 
    h ^= (uint32_t)(p->bmi270_last_axis_values[2] * 1000); 
    h ^= (p->measurement_active ? 0x80000000 : 0); // 측정 상태 변경 시 즉각 반영
    return h;
}





/* ----------------------------------------------------------------------------
 * 6. 프론트엔드 정적 파일 서빙 핸들러 (LittleFS 연동)
 * ---------------------------------------------------------------------------- */
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server) {
    if (v_server == nullptr) return;

    // [옵션 1] 명시적 라우트 매핑 (가장 확실한 방법)
    // 루트("/") 접속 시 강제로 index_214_003.html을 반환합니다.
    v_server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, "/index_214_003.html", "text/html");
    });

    // [옵션 2] 폴더 전체 서빙 (css, js 등 에셋 자동 로드)
    // 브라우저 캐시(Cache-Control)를 적용하여 ESP32의 디스크 I/O 부하를 줄입니다.
    v_server->serveStatic("/", LittleFS, "/")
            .setDefaultFile("index_214_003.html")
            .setCacheControl("max-age=3600"); // 1시간 동안 브라우저 캐시 허용

    // 404 Not Found 처리 (선택 사항이나 디버깅에 매우 유용함)
    v_server->onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "404: Not Found in LittleFS");
        }
    });
}



/* ----------------------------------------------------------------------------
 * 메인 등록 함수 (모든 핸들러 취합)
 * ---------------------------------------------------------------------------- */
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path) {
    if (p == nullptr || v_server == nullptr) return;
    String base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    // 기존 데이터 송출 핸들러 (T20_buildViewerDataJsonText 등을 사용하는 라우트)
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
        request->send(200, "application/json", json);
    });

    // 보조/추가 핸들러군 등록
    T20_registerControlHandlers(p, v_server, base);
    T20_registerSensorDiagHandlers(p, v_server, base);
    T20_registerDataHandlers(p, v_server, base);
    
    // [중요 수정] 선언은 되었으나 호출되지 않았던 파일/노이즈 핸들러 최종 등록
    T20_registerFileStreamingHandler(p, v_server, base);
    T20_registerNoiseControlHandlers(p, v_server, base);

    // WebSocket 핸들러 바인딩
    ws.onEvent(onWsEvent);
    v_server->addHandler(&ws);

    // Build Sanity 체커 등록
    v_server->on((base + "/build_sanity").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildBuildSanityJsonText(p, json, sizeof(json)), json);
    });
    
    // [최종 추가] 백엔드 API 등록이 모두 끝난 후 정적 프론트엔드 라우트 등록
    T20_registerStaticFrontendHandlers(v_server);
}

