#include "T20_Mfcc_Web_062.h"

uint32_t T20_calcStatusHash(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return 0;
    uint32_t h = 2166136261UL;
    h ^= p->recorder_record_count; h *= 16777619UL;
    h ^= p->dropped_frames;        h *= 16777619UL;
    h ^= p->viewer_last_frame_id;  h *= 16777619UL;
    return h;
}

bool T20_rotateListDeleteFile(const char* p_path)
{
    if (p_path == nullptr || p_path[0] == 0) return false;
    if (LittleFS.exists(p_path)) return LittleFS.remove(p_path);
    return false;
}

void T20_rotateListPrune(CL_T20_Mfcc::ST_Impl* p)
{
    (void)p;
}

bool T20_getQueryParamPath(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len)
{
    if (request == nullptr || p_name == nullptr || p_out_buf == nullptr || p_len == 0) return false;
    if (!request->hasParam(p_name)) return false;
    const AsyncWebParameter* param = request->getParam(p_name);
    if (param == nullptr) return false;
    strlcpy(p_out_buf, param->value().c_str(), p_len);
    return true;
}

void T20_getQueryParamText(AsyncWebServerRequest* request, const char* p_name, char* p_out_buf, uint16_t p_len, const char* p_default)
{
    if (p_out_buf == nullptr || p_len == 0) return;
    if (p_default == nullptr) p_default = "";
    strlcpy(p_out_buf, p_default, p_len);
    if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
        const AsyncWebParameter* param = request->getParam(p_name);
        if (param != nullptr) strlcpy(p_out_buf, param->value().c_str(), p_len);
    }
}

uint32_t T20_getQueryParamUint32(AsyncWebServerRequest* request, const char* p_name, uint32_t p_default, uint32_t p_min, uint32_t p_max)
{
    uint32_t v = p_default;
    if (request != nullptr && p_name != nullptr && request->hasParam(p_name)) {
        const AsyncWebParameter* param = request->getParam(p_name);
        if (param != nullptr) v = (uint32_t)param->value().toInt();
    }
    if (v < p_min) v = p_min;
    if (v > p_max) v = p_max;
    return v;
}

static void T20_sendJsonText(AsyncWebServerRequest* request, bool ok, const char* json_ok)
{
    if (request == nullptr) return;
    if (ok && json_ok != nullptr) request->send(200, "application/json; charset=utf-8", json_ok);
    else request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
}

void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path)
{
    if (p == nullptr || v_server == nullptr) return;
    const char* base = (p_base_path == nullptr || p_base_path[0] == 0) ? "/api/t20" : p_base_path;

    String s_status                 = String(base) + "/status";
    String s_config                 = String(base) + "/config";
    String s_runtime_config         = String(base) + "/runtime_config";
    String s_runtime_config_apply   = String(base) + "/runtime_config_apply";
    String s_sdmmc_profile_apply    = String(base) + "/sdmmc/profile_apply";
    String s_config_schema          = String(base) + "/config_schema";
    String s_viewer_data            = String(base) + "/viewer/data";
    String s_viewer_waveform        = String(base) + "/viewer/waveform";
    String s_viewer_spectrum        = String(base) + "/viewer/spectrum";
    String s_viewer_events          = String(base) + "/viewer/events";
    String s_viewer_sequence        = String(base) + "/viewer/sequence";
    String s_viewer_overview        = String(base) + "/viewer/overview";
    String s_viewer_multi_frame     = String(base) + "/viewer/multi_frame";
    String s_viewer_chart_bundle    = String(base) + "/viewer/chart_bundle";
    String s_rec_manifest           = String(base) + "/recorder/manifest";
    String s_rec_index              = String(base) + "/recorder/index";
    String s_rec_preview            = String(base) + "/recorder/preview";
    String s_rec_preview_parsed     = String(base) + "/recorder/preview_parsed";
    String s_rec_range              = String(base) + "/recorder/file_range";
    String s_rec_range_json         = String(base) + "/recorder/file_range_json";
    String s_rec_bin_header         = String(base) + "/recorder/binary_header";
    String s_rec_csv_table          = String(base) + "/recorder/file_csv_table";
    String s_rec_csv_schema         = String(base) + "/recorder/file_csv_schema";
    String s_rec_csv_type_meta      = String(base) + "/recorder/file_csv_type_meta";
    String s_rec_csv_table_adv      = String(base) + "/recorder/file_csv_table_advanced";
    String s_rec_bin_records        = String(base) + "/recorder/binary_records";
    String s_rec_bin_payload_schema = String(base) + "/recorder/binary_payload_schema";
    String s_render_selection_sync  = String(base) + "/render_selection_sync";
    String s_type_meta_preview_link = String(base) + "/type_meta_preview_link";

    v_server->on(s_status.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["ok"] = true;
        doc["running"] = p->running;
        doc["initialized"] = p->initialized;
        doc["status_hash"] = T20_calcStatusHash(p);
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        serializeJson(doc, json, sizeof(json));
        request->send(200, "application/json; charset=utf-8", json);
    });

    v_server->on(s_config.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildConfigJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_config_schema.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildConfigSchemaJsonText(json, sizeof(json)), json);
    });

    v_server->on(s_runtime_config.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRuntimeConfigJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_runtime_config_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
        T20_getQueryParamText(request, "json", json, sizeof(json), "{}");
        bool ok = T20_applyRuntimeConfigJsonText(p, json);
        if (!ok) {
            request->send(400, "application/json; charset=utf-8", "{"ok":false,"message":"invalid runtime config"}");
            return;
        }
        T20_saveRuntimeConfigFile(p);
        char out_json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
        T20_buildRuntimeConfigJsonText(p, out_json, sizeof(out_json));
        request->send(200, "application/json; charset=utf-8", out_json);
    });

    v_server->on(s_sdmmc_profile_apply.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        char name[64] = {0};
        T20_getQueryParamText(request, "name", name, sizeof(name), "default-1bit");
        bool ok = T20_applySdmmcProfileByName(p, name);
        if (!ok) {
            request->send(400, "application/json; charset=utf-8", "{"ok":false,"message":"invalid profile"}");
            return;
        }
        char json[G_T20_RUNTIME_CFG_JSON_BUF_SIZE] = {0};
        T20_buildRuntimeConfigJsonText(p, json, sizeof(json));
        request->send(200, "application/json; charset=utf-8", json);
    });

    v_server->on(s_viewer_data.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerDataJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_waveform.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerWaveformJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_spectrum.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerSpectrumJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_events.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerEventsJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_sequence.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerSequenceJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_overview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerOverviewJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_multi_frame.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerMultiFrameJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_viewer_chart_bundle.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        uint16_t points = (uint16_t)T20_getQueryParamUint32(request, "points", 128, 8, 2048);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildViewerChartBundleJsonText(p, json, sizeof(json), points), json);
    });

    v_server->on(s_rec_manifest.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderManifestJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_rec_index.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderIndexJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_rec_preview.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderPreviewJsonText(p, json, sizeof(json), path, bytes), json);
    });

    v_server->on(s_rec_preview_parsed.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderParsedPreviewJsonText(p, json, sizeof(json), path, bytes), json);
    });

    v_server->on(s_rec_range_json.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t offset = T20_getQueryParamUint32(request, "offset", 0, 0, 0xFFFFFFFFUL);
        uint32_t length = T20_getQueryParamUint32(request, "length", 1024, 1, 0xFFFFFFFFUL);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderRangeJsonText(p, json, sizeof(json), path, offset, length), json);
    });

    v_server->on(s_rec_bin_header.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        T20_getQueryParamText(request, "path", path, sizeof(path), "");
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderBinaryHeaderJsonText(p, json, sizeof(json), path), json);
    });

    v_server->on(s_rec_csv_table.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderCsvTableJsonText(p, json, sizeof(json), path, bytes), json);
    });

    v_server->on(s_rec_csv_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderCsvSchemaJsonText(p, json, sizeof(json), path, bytes), json);
    });

    v_server->on(s_rec_csv_type_meta.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderCsvTypeMetaJsonText(p, json, sizeof(json), path, bytes), json);
    });

    v_server->on(s_rec_csv_table_adv.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        char filter[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE] = {0};
        char col_filters_csv[G_T20_TEXT_PREVIEW_LINE_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"message\":\"path required\"}");
            return;
        }
        T20_getQueryParamText(request, "filter", filter, sizeof(filter), "");
        T20_getQueryParamText(request, "col_filters", col_filters_csv, sizeof(col_filters_csv), "");
        uint32_t bytes = T20_getQueryParamUint32(request, "bytes", G_T20_PREVIEW_TEXT_BYTES_DEFAULT, 1, G_T20_PREVIEW_TEXT_BYTES_MAX);
        uint16_t sort_col = (uint16_t)T20_getQueryParamUint32(request, "sort_col", 0, 0, 64);
        uint16_t sort_dir = (uint16_t)T20_getQueryParamUint32(request, "sort_dir", G_T20_CSV_SORT_ASC, G_T20_CSV_SORT_ASC, G_T20_CSV_SORT_DESC);
        uint16_t page = (uint16_t)T20_getQueryParamUint32(request, "page", 0, 0, 999);
        uint16_t page_size = (uint16_t)T20_getQueryParamUint32(request, "page_size", G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT, 1, G_T20_CSV_TABLE_PAGE_SIZE_MAX);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderCsvTableAdvancedJsonText(p, json, sizeof(json), path, bytes, filter, col_filters_csv, sort_col, sort_dir, page, page_size), json);
    });

    v_server->on(s_rec_bin_records.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        T20_getQueryParamText(request, "path", path, sizeof(path), "");
        uint32_t offset = T20_getQueryParamUint32(request, "offset", 0, 0, 0xFFFFFFFFUL);
        uint32_t limit = T20_getQueryParamUint32(request, "limit", 32, 1, 4096);
        char json[G_T20_WEB_LARGE_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderBinaryRecordsJsonText(p, json, sizeof(json), path, offset, limit), json);
    });

    v_server->on(s_rec_bin_payload_schema.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        T20_getQueryParamText(request, "path", path, sizeof(path), "");
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRecorderBinaryPayloadSchemaJsonText(p, json, sizeof(json), path), json);
    });

    v_server->on(s_render_selection_sync.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildRenderSelectionSyncJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_type_meta_preview_link.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char json[G_T20_WEB_JSON_BUF_SIZE] = {0};
        T20_sendJsonText(request, T20_buildTypeMetaPreviewLinkJsonText(p, json, sizeof(json)), json);
    });

    v_server->on(s_rec_range.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
        char path[G_T20_WEB_PATH_BUF_SIZE] = {0};
        if (!T20_getQueryParamPath(request, "path", path, sizeof(path))) {
            request->send(400, "text/plain; charset=utf-8", "path required");
            return;
        }
        if (!LittleFS.exists(path)) {
            request->send(404, "text/plain; charset=utf-8", "not found");
            return;
        }

        File file = LittleFS.open(path, "r");
        if (!file) {
            request->send(500, "text/plain; charset=utf-8", "open failed");
            return;
        }

        uint32_t size = (uint32_t)file.size();
        uint32_t offset = 0;
        uint32_t length = size;

        if (request->hasHeader("Range")) {
            const AsyncWebHeader* header = request->getHeader("Range");
            if (header != nullptr) {
                uint32_t o = 0;
                uint32_t l = 0;
                if (T20_parseHttpRangeHeader(header->value(), size, &o, &l)) {
                    offset = o;
                    length = l;
                }
            }
        }

        file.seek(offset);
        std::unique_ptr<uint8_t[]> buf(new uint8_t[length]);
        if (!buf) {
            file.close();
            request->send(500, "text/plain; charset=utf-8", "alloc failed");
            return;
        }

        size_t read_len = file.read(buf.get(), length);
        file.close();

        AsyncWebServerResponse* resp = request->beginResponse(200, "application/octet-stream", (const uint8_t*)buf.get(), read_len);
        if (resp != nullptr) {
            resp->addHeader("Accept-Ranges", "bytes");
            request->send(resp);
        } else {
            request->send(500, "text/plain; charset=utf-8", "response failed");
        }
    });
}
