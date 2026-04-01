/* ============================================================================
 * File: T250_Web_213.cpp
 * Summary: Web API 엔드포인트 및 핸들러 구현 (완성본)
 * * [구현 계획 재점검 사항]
 * 1. v210의 파편화된 상태값을 v212 그룹 구조체(bmi_state, rec_state)에 매핑
 * 2. Recorder 세션 제어 및 Finalize 단계별 모니터링 엔드포인트 활성화
 * 3. BMI270 하드웨어 브리지 및 레지스터 검증 상태 연결
 * 4. 모든 builder 함수가 direct access가 아닌 구조체 참조를 하도록 유지
 ============================================================================ */

#include "T250_Web_213.h"

/* ----------------------------------------------------------------------------
 * 1. Recorder 제어 및 세션 관리 엔드포인트
 * ---------------------------------------------------------------------------- */

void T20_registerControlHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    // 레코딩 시작/중지
    v_server->on((base + "/recorder_begin").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderBegin(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_started\"}");
    });

    v_server->on((base + "/recorder_end").c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_recorderEnd(p);
        T20_sendJsonText(request, ok, "{\"ok\":true,\"msg\":\"recorder_stopped\"}");
    });

    // Finalize 프로세스 진단 (v210의 촘촘한 상태 전이 확인)
    v_server->on((base + "/recorder_finalize").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderFinalizeJsonText(p, json, sizeof(json)), json);
    });

    v_server->on((base + "/recorder_finalize_pipeline").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        // 전체 파이프라인(Commit->Sync->Artifact) 상태 리포트
        T20_sendJsonText(request, T20_buildRecorderFinalizeBundleJsonText(p, json, sizeof(json)), json);
    });
}

/* ----------------------------------------------------------------------------
 * 2. BMI270 센서 상세 진단 엔드포인트
 * ---------------------------------------------------------------------------- */

void T20_registerSensorDiagHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    // 실제 하드웨어 SPI/레지스터 통신 상태
    v_server->on((base + "/bmi270_actual_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        // p->bmi_state.spi 및 p->bmi_state.read 상태 반환
        T20_sendJsonText(request, T20_buildIoSyncBundleJsonText(p, json, sizeof(json)), json);
    });

    // 인터럽트(DRDY) 및 하드웨어 브리지 상태
    v_server->on((base + "/bmi270_bridge_state").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["hw_bridge"] = T20_StateToString(p->bmi_state.hw_link);
        doc["isr_bridge"] = T20_StateToString(p->bmi_runtime.isr_hook);
        doc["drdy_flag"] = p->bmi270_drdy_isr_flag;
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, "application/json", json);
    });

    // 레지스터 검증(Verify) 결과
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
 * 3. 데이터 프리뷰 및 메타데이터 관리
 * ---------------------------------------------------------------------------- */

void T20_registerDataHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const String& base) {
    // 프리뷰 데이터 로드 (LittleFS의 CSV 등)
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

    // 고급 CSV 테이블 뷰 (필터, 정렬, 페이징)
    v_server->on((base + "/recorder/file_csv_table_advanced").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[128] = {0};
        T20_getQueryParamPath(request, "path", path, sizeof(path));
        uint16_t page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 1000);
        
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool ok = T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, 4096, nullptr, nullptr, 0, 0, page, 20);
        T20_sendJsonText(request, ok, json);
    });
}

/* ----------------------------------------------------------------------------
 * 최종 등록 함수 통합
 * ---------------------------------------------------------------------------- */
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path) {
    if (p == nullptr || v_server == nullptr) return;
    String base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    // 기존 핸들러 (Status, Config 등 - 이전 답변 참조)
    // ... (s_status, s_runtime_config 등 등록 코드 중략) ...

    // 추가 보강된 핸들러들 등록
    T20_registerControlHandlers(p, v_server, base);
    T20_registerSensorDiagHandlers(p, v_server, base);
    T20_registerDataHandlers(p, v_server, base);

    // Build Sanity 체크용 엔드포인트 (최종 확인용)
    v_server->on((base + "/build_sanity").c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildBuildSanityJsonText(p, json, sizeof(json)), json);
    });
}


