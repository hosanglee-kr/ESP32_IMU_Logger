#include "T20_Mfcc_Inter_054.h"

/*
===============================================================================
소스명: T20_Mfcc_Web_054.cpp
버전: v054

[기능 스펙]
- AsyncWeb 기반 T20 웹 API 등록
- 상태/설정/뷰어/레코더/CSV/Sync 관련 JSON API 라우팅
- 이번 단계에서는 중괄호/스코프 붕괴를 줄이기 위해
  비교적 단순하고 정적인 라우팅 구조로 정리
===============================================================================
*/

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return 0;
    uint32_t v_hash = 2166136261UL;
    v_hash ^= p->recorder_record_count; v_hash *= 16777619UL;
    v_hash ^= p->dropped_frames;        v_hash *= 16777619UL;
    v_hash ^= p->viewer_last_frame_id;  v_hash *= 16777619UL;
    return v_hash;
}

bool T20_rotateListDeleteFile(const char* p_path)
{
    if (p_path == nullptr || p_path[0] == 0) return false;
    if (LittleFS.exists(p_path)) {
        return LittleFS.remove(p_path);
    }
    return false;
}

void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p)
{
    (void)p;
    /* TODO: 실제 rotate prune 정책 고도화 */
}

bool T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len)
{
    if (request == nullptr || p_name == nullptr || p_out_buf == nullptr || p_len == 0) return false;
    if (!request->hasParam(p_name)) return false;
    String v = request->getParam(p_name)->value();
    strlcpy(p_out_buf, v.c_str(), p_len);
    return true;
}

void T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default)
{
    if (p_out_buf == nullptr || p_len == 0) return;
    if (p_default == nullptr) p_default = "";
    strlcpy(p_out_buf, p_default, p_len);
    if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
        String v = request->getParam(p_name)->value();
        strlcpy(p_out_buf, v.c_str(), p_len);
    }
}

uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max)
{
    uint32_t v = p_default;
    if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
        v = (uint32_t)request->getParam(p_name)->value().toInt();
    }
    if (v < p_min) v = p_min;
    if (v > p_max) v = p_max;
    return v;
}

static void T20_sendJsonText(AsyncWebServerRequest* request, bool p_ok, const char* p_json_ok)
{
    if (request == nullptr) return;
    if (p_ok && p_json_ok != nullptr) {
        request->send(200, "application/json; charset=utf-8", p_json_ok);
    } else {
        request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
    }
}

void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path)
{
    if (p == nullptr || v_server == nullptr) {
        return;
    }

    const char* v_base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    String s_status                  = String(v_base) + "/status";
    String s_config                  = String(v_base) + "/config";
    String s_config_schema           = String(v_base) + "/config_schema";
    String s_viewer_waveform         = String(v_base) + "/viewer/waveform";
    String s_viewer_spectrum         = String(v_base) + "/viewer/spectrum";
    String s_viewer_data             = String(v_base) + "/viewer/data";
    String s_viewer_events           = String(v_base) + "/viewer/events";
    String s_viewer_sequence         = String(v_base) + "/viewer/sequence";
    String s_viewer_overview         = String(v_base) + "/viewer/overview";
    String s_viewer_multi_frame      = String(v_base) + "/viewer/multi_frame";
    String s_viewer_chart_bundle     = String(v_base) + "/viewer/chart_bundle";
    String s_rec_manifest            = String(v_base) + "/recorder/manifest";
    String s_rec_index               = String(v_base) + "/recorder/index";
    String s_rec_preview             = String(v_base) + "/recorder/preview";
    String s_rec_preview_parsed      = String(v_base) + "/recorder/preview_parsed";
    String s_rec_range               = String(v_base) + "/recorder/file_range";
    String s_rec_range_json          = String(v_base) + "/recorder/file_range_json";
    String s_rec_bin_header          = String(v_base) + "/recorder/binary_header";
    String s_rec_csv_table           = String(v_base) + "/recorder/file_csv_table";
    String s_rec_csv_schema          = String(v_base) + "/recorder/file_csv_schema";
    String s_rec_csv_type_meta       = String(v_base) + "/recorder/file_csv_type_meta";
    String s_rec_csv_table_advanced  = String(v_base) + "/recorder/file_csv_table_advanced";
    String s_rec_bin_records         = String(v_base) + "/recorder/binary_records";
    String s_rec_bin_payload_schema  = String(v_base) + "/recorder/binary_payload_schema";
    String s_render_selection_sync   = String(v_base) + "/render_selection_sync";
    String s_type_meta_preview_link  = String(v_base) + "/type_meta_preview_link";

    v_server->on(s_status.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument v_doc;
        v_doc["ok"] = true;
        v_doc["running"] = p->running;
        v_doc["initialized"] = p->initialized;
        v_doc["status_hash"] = T20_calcStatusHash(p);
        v_doc["record_count"] = p->recorder_record_count;
        v_doc["dropped_frames"] = p->dropped_frames;
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(v_doc, v_json, sizeof(v_json));
        request->send(200, "application/json; charset=utf-8", v_json);
    });

    v_server->on(s_config.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildConfigJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_config_schema.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildConfigSchemaJsonText(v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_waveform.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerWaveformJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_spectrum.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerSpectrumJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_data.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerDataJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_events.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerEventsJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_sequence.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerSequenceJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_overview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerOverviewJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_multi_frame.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerMultiFrameJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_viewer_chart_bundle.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildViewerChartBundleJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_manifest.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderManifestJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_index.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderIndexJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_preview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderPreviewJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_preview_parsed.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderParsedPreviewJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_range_json.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderRangeJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_bin_header.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderBinaryHeaderJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_csv_table.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderCsvTableJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_csv_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderCsvSchemaJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_csv_type_meta.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", v_path, sizeof(v_path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t v_bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderCsvTypeMetaJsonText(p, v_json, sizeof(v_json), v_path, v_bytes);
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_csv_table_advanced.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", v_path, sizeof(v_path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        char v_filter[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE] = {0};
        char v_col_filters_csv[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE] = {0};
        T20_getQueryParamText(request, "filter", v_filter, sizeof(v_filter), "");
        T20_getQueryParamText(request, "col_filters", v_col_filters_csv, sizeof(v_col_filters_csv), "");
        uint32_t v_bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        uint16_t v_sort_col = (uint16_t)T20_getQueryParamUint32(request, "sort_col", 0, 0, 64);
        uint16_t v_sort_dir = (uint16_t)T20_getQueryParamUint32(request, "sort_dir", G_T20_CSV_SORT_ASC, 0, 1);
        uint16_t v_page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 999);
        uint16_t v_page_size = (uint16_t)T20_getQueryParamUint32(request, "page_size", G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT, 1, G_T20_CSV_TABLE_PAGE_SIZE_MAX);
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderCsvTableAdvancedJsonText(p, v_json, sizeof(v_json), v_path, v_bytes, v_filter, v_col_filters_csv, v_sort_col, v_sort_dir, v_page, v_page_size);
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_bin_records.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderBinaryRecordsJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_rec_bin_payload_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRecorderBinaryPayloadSchemaJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_render_selection_sync.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildRenderSelectionSyncJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    v_server->on(s_type_meta_preview_link.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        bool v_ok = T20_buildTypeMetaPreviewLinkJsonText(p, v_json, sizeof(v_json));
        T20_sendJsonText(request, v_ok, v_json);
    });

    /* 파일 range 다운로드 skeleton */
    v_server->on(s_rec_range.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        (void)p;
        char v_path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", v_path, sizeof(v_path))) {
            request->send(400, "text/plain; charset=utf-8", "path required");
            return;
        }
        if (!LittleFS.exists(v_path)) {
            request->send(404, "text/plain; charset=utf-8", "not found");
            return;
        }

        File v_file = LittleFS.open(v_path, "r");
        if (!v_file) {
            request->send(500, "text/plain; charset=utf-8", "open failed");
            return;
        }

        uint32_t v_size = (uint32_t)v_file.size();
        uint32_t v_offset = 0;
        uint32_t v_length = v_size;

        if (request->hasHeader("Range")) {
            AsyncWebHeader* v_header = request->getHeader("Range");
            if (v_header != nullptr) {
                String v_range = v_header->value();
                uint32_t v_o = 0;
                uint32_t v_l = 0;
                if (T20_parseHttpRangeHeader(v_range, v_size, &v_o, &v_l)) {
                    v_offset = v_o;
                    v_length = v_l;
                }
            }
        }

        v_file.seek(v_offset);
        std::unique_ptr<uint8_t[]> v_buf(new uint8_t[v_length]);
        if (!v_buf) {
            v_file.close();
            request->send(500, "text/plain; charset=utf-8", "alloc failed");
            return;
        }

        size_t v_read = v_file.read(v_buf.get(), v_length);
        v_file.close();

        AsyncWebServerResponse* v_resp = request->beginResponse(200, "application/octet-stream", (const char*)v_buf.get(), v_read);
        if (v_resp != nullptr) {
            v_resp->addHeader("Accept-Ranges", "bytes");
            request->send(v_resp);
        } else {
            request->send(500, "text/plain; charset=utf-8", "response failed");
        }
    });
}
