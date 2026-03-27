#include "T20_Mfcc_Inter_025.h"

#if __has_include(<ESPAsyncWebServer.h>)
#include <ESPAsyncWebServer.h>
#define T20_HAS_ASYNCWEB 1
#else
#define T20_HAS_ASYNCWEB 0
#endif

#if __has_include(<LittleFS.h>)
#include <LittleFS.h>
#define T20_HAS_LITTLEFS 1
#else
#define T20_HAS_LITTLEFS 0
#endif

#include <SD_MMC.h>
#include <string.h>
#include <stdio.h>

/*
===============================================================================
소스명: T20_Mfcc_Web_025.cpp
버전: v025

[기능 스펙]
- AsyncWeb 기반 제어/다운로드 엔드포인트 제공
- LittleFS 정적 파일(html/css/js) 제공
- polling 기반 live API + WebSocket/SSE 실시간 push 통합
- 설정 JSON 적용 로직 보강
- recorder rotate 결과 다운로드 경로 유지

[이번 단계 반영]
1. SSE 실시간 push 실제 연결
2. WebSocket 실시간 push 실제 연결
3. rotate 목록/삭제 API 추가
4. push decimation / force push 정책 추가
5. recorder rotate index 기록 유지
6. 설정 JSON 적용 보강 유지

[향후 단계 TODO]
- websocket 인증 / 권한 제어
- SSE reconnect / last-event-id 대응
- zero-copy / DMA / cache aligned 최적화
- 전체 JSON 스키마 정식 파서 전환
- 다중 프로파일 저장
===============================================================================
*/

#if T20_HAS_ASYNCWEB
static AsyncEventSource* s_t20_sse = nullptr;
static AsyncWebSocket*   s_t20_ws  = nullptr;
static char s_t20_sse_path[64] = {0};
static char s_t20_ws_path[64]  = {0};
#endif

static bool T20_jsonFindBool(const char* p_json, const char* p_key, bool* p_out)
{
    if (p_json == nullptr || p_key == nullptr || p_out == nullptr) {
        return false;
    }

    const char* pos = strstr(p_json, p_key);
    if (pos == nullptr) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    ++pos;

    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        ++pos;
    }

    if (strncmp(pos, "true", 4) == 0) {
        *p_out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *p_out = false;
        return true;
    }
    return false;
}

static bool T20_jsonFindUint(const char* p_json, const char* p_key, uint32_t* p_out)
{
    if (p_json == nullptr || p_key == nullptr || p_out == nullptr) {
        return false;
    }

    const char* pos = strstr(p_json, p_key);
    if (pos == nullptr) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    ++pos;

    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"') {
        ++pos;
    }

    *p_out = (uint32_t)strtoul(pos, nullptr, 10);
    return true;
}

static bool T20_jsonFindFloat(const char* p_json, const char* p_key, float* p_out)
{
    if (p_json == nullptr || p_key == nullptr || p_out == nullptr) {
        return false;
    }

    const char* pos = strstr(p_json, p_key);
    if (pos == nullptr) {
        return false;
    }

    pos = strchr(pos, ':');
    if (pos == nullptr) {
        return false;
    }
    ++pos;

    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == '"') {
        ++pos;
    }

    *p_out = strtof(pos, nullptr);
    return true;
}

static void T20_webBuildStatusJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    snprintf(
        p_buf, p_len,
        "{\"initialized\":%s,\"running\":%s,\"session_state\":%u,\"measurement_active\":%s,"
        "\"dropped_frames\":%lu,\"queued_records\":%lu,\"dropped_records\":%lu,"
        "\"written_records\":%lu,\"web_attached\":%s}",
        p->initialized ? "true" : "false",
        p->running ? "true" : "false",
        (unsigned)p->session_state,
        p->measurement_active ? "true" : "false",
        (unsigned long)p->dropped_frames,
        (unsigned long)p->recorder.queued_record_count,
        (unsigned long)p->recorder.dropped_record_count,
        (unsigned long)p->recorder.written_record_count,
        p->web_attached ? "true" : "false"
    );
}

static void T20_webBuildLatestJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    int n = snprintf(
        p_buf, p_len,
        "{\"log_mel_len\":%u,\"mfcc_len\":%u,\"delta_len\":%u,\"delta2_len\":%u,\"vector_len\":%u,",
        p->latest_feature.log_mel_len,
        p->latest_feature.mfcc_len,
        p->latest_feature.delta_len,
        p->latest_feature.delta2_len,
        p->latest_feature.vector_len
    );
    if (n < 0 || (size_t)n >= p_len) {
        if (p_len > 0) {
            p_buf[0] = '\0';
        }
        return;
    }

    size_t off = (size_t)n;

    auto append_array = [&](const char* p_key, const float* p_arr, uint16_t p_count, bool p_comma) -> bool
    {
        int m = snprintf(p_buf + off, p_len - off, "\"%s\":[", p_key);
        if (m < 0 || (size_t)m >= p_len - off) {
            return false;
        }
        off += (size_t)m;

        for (uint16_t i = 0; i < p_count; ++i) {
            m = snprintf(p_buf + off, p_len - off, (i == 0) ? "%.6f" : ",%.6f", p_arr[i]);
            if (m < 0 || (size_t)m >= p_len - off) {
                return false;
            }
            off += (size_t)m;
        }

        m = snprintf(p_buf + off, p_len - off, p_comma ? "]," : "]");
        if (m < 0 || (size_t)m >= p_len - off) {
            return false;
        }
        off += (size_t)m;
        return true;
    };

    if (!append_array("mfcc", p->latest_feature.mfcc, p->latest_feature.mfcc_len, true)) return;
    if (!append_array("delta", p->latest_feature.delta, p->latest_feature.delta_len, true)) return;
    if (!append_array("delta2", p->latest_feature.delta2, p->latest_feature.delta2_len, false)) return;

    snprintf(p_buf + off, p_len - off, "}");
}

static void T20_webBuildWaveJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    int n = snprintf(p_buf, p_len, "{\"frame_len\":%u,\"samples\":[", (unsigned)G_T20_FFT_SIZE);
    if (n < 0 || (size_t)n >= p_len) {
        if (p_len > 0) p_buf[0] = '\0';
        return;
    }

    size_t off = (size_t)n;
    const uint16_t v_emit = (G_T20_FFT_SIZE > G_T20_WEB_LIVE_WAVE_MAX_SAMPLES) ? G_T20_WEB_LIVE_WAVE_MAX_SAMPLES : G_T20_FFT_SIZE;

    for (uint16_t i = 0; i < v_emit; ++i) {
        n = snprintf(p_buf + off, p_len - off, (i == 0) ? "%.6f" : ",%.6f", p->latest_wave_frame[i]);
        if (n < 0 || (size_t)n >= p_len - off) return;
        off += (size_t)n;
    }

    snprintf(p_buf + off, p_len - off, "]}");
}

static void T20_webBuildSequenceJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    int n = snprintf(
        p_buf, p_len,
        "{\"ready\":%s,\"frames\":%u,\"feature_dim\":%u,\"data\":[",
        p->latest_sequence_valid ? "true" : "false",
        p->seq_rb.frames,
        p->seq_rb.feature_dim
    );
    if (n < 0 || (size_t)n >= p_len) {
        if (p_len > 0) p_buf[0] = '\0';
        return;
    }

    size_t off = (size_t)n;
    const uint16_t v_need = p->seq_rb.frames * p->seq_rb.feature_dim;
    const uint16_t v_emit = (v_need > G_T20_WEB_LIVE_SEQ_MAX_VALUES) ? G_T20_WEB_LIVE_SEQ_MAX_VALUES : v_need;

    for (uint16_t i = 0; i < v_emit; ++i) {
        n = snprintf(p_buf + off, p_len - off, (i == 0) ? "%.6f" : ",%.6f", p->latest_sequence_flat[i]);
        if (n < 0 || (size_t)n >= p_len - off) return;
        off += (size_t)n;
    }

    snprintf(p_buf + off, p_len - off, "]}");
}

static void T20_webBuildLiveBundleJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    char v_status[768] = {0};
    char v_latest[2048] = {0};
    char v_wave[2048] = {0};
    char v_seq[2048] = {0};

    T20_webBuildStatusJson(p, v_status, sizeof(v_status));
    T20_webBuildLatestJson(p, v_latest, sizeof(v_latest));
    T20_webBuildWaveJson(p, v_wave, sizeof(v_wave));
    T20_webBuildSequenceJson(p, v_seq, sizeof(v_seq));

    snprintf(p_buf, p_len, "{\"status\":%s,\"latest\":%s,\"wave\":%s,\"sequence\":%s}", v_status, v_latest, v_wave, v_seq);
}



static void T20_webBuildConfigJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    snprintf(
        p_buf,
        p_len,
        "{\"sample_rate_hz\":%.1f,\"mfcc_coeffs\":%u,\"mel_filters\":%u,"
        "\"sequence_frames\":%u,\"output_mode\":\"%s\",\"preemphasis_enable\":%s,"
        "\"preemphasis_alpha\":%.3f,\"filter_enable\":%s,\"filter_type\":%d,"
        "\"cutoff_hz_1\":%.3f,\"cutoff_hz_2\":%.3f}",
        p->cfg.feature.sample_rate_hz,
        p->cfg.feature.mfcc_coeffs,
        p->cfg.feature.mel_filters,
        p->cfg.output.sequence_frames,
        (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) ? "vector" : "sequence",
        p->cfg.preprocess.preemphasis.enable ? "true" : "false",
        p->cfg.preprocess.preemphasis.alpha,
        p->cfg.preprocess.filter.enable ? "true" : "false",
        (int)p->cfg.preprocess.filter.type,
        p->cfg.preprocess.filter.cutoff_hz_1,
        p->cfg.preprocess.filter.cutoff_hz_2
    );
}

static bool T20_webSaveConfigToLittleFs(CL_T20_Mfcc::ST_Impl* p)
{
#if T20_HAS_LITTLEFS
    if (p == nullptr) {
        return false;
    }

    File f = LittleFS.open("/t20_cfg_runtime.json", "w");
    if (!f) {
        return false;
    }

    char buf[1024] = {0};
    T20_webBuildConfigJson(p, buf, sizeof(buf));
    f.print(buf);
    f.close();
    return true;
#else
    (void)p;
    return false;
#endif
}

static void T20_webBuildRotateListJson(CL_T20_Mfcc::ST_Impl* p, char* p_buf, size_t p_len)
{
    if (p == nullptr || p_buf == nullptr || p_len == 0) {
        return;
    }

    int n = snprintf(p_buf, p_len, "{\"count\":%u,\"items\":[", p->rotate_item_count);
    if (n < 0 || (size_t)n >= p_len) {
        if (p_len > 0) p_buf[0] = '\0';
        return;
    }

    size_t off = (size_t)n;
    for (uint16_t i = 0; i < p->rotate_item_count; ++i) {
        const ST_T20_RotateItem_t& v_item = p->rotate_items[i];
        n = snprintf(p_buf + off, p_len - off,
                     "%s{\"rotate_id\":%lu,\"raw\":\"%s\",\"meta\":\"%s\",\"event\":\"%s\",\"feature\":\"%s\"}",
                     (i == 0) ? "" : ",",
                     (unsigned long)v_item.rotate_id,
                     v_item.raw_path, v_item.meta_path, v_item.event_path, v_item.feature_path);
        if (n < 0 || (size_t)n >= p_len - off) {
            return;
        }
        off += (size_t)n;
    }

    snprintf(p_buf + off, p_len - off, "]}");
}

static void T20_webSendJsonError(AsyncWebServerRequest* p_request, int p_code, const char* p_error)
{
    String v_body = String("{\"ok\":false,\"error\":\"") + p_error + "\"}";
    p_request->send(p_code, "application/json; charset=utf-8", v_body);
}

static void T20_webSendSdFile(AsyncWebServerRequest* p_request, const char* p_path, const char* p_type)
{
    if (p_path == nullptr || p_path[0] == '\0') {
        T20_webSendJsonError(p_request, 404, "empty_path");
        return;
    }
    if (!SD_MMC.exists(p_path)) {
        T20_webSendJsonError(p_request, 404, "not_found");
        return;
    }
    p_request->send(SD_MMC, p_path, p_type, true);
}

bool T20_webAttachStaticFiles(CL_T20_Mfcc::ST_Impl* p, void* p_server)
{
    (void)p;
#if !T20_HAS_ASYNCWEB || !T20_HAS_LITTLEFS
    (void)p_server;
    return false;
#else
    if (p_server == nullptr) {
        return false;
    }

    AsyncWebServer* v_server = reinterpret_cast<AsyncWebServer*>(p_server);
    LittleFS.begin(true);

    v_server->serveStatic("/t20/", LittleFS, "/t20/").setDefaultFile("index.html");
    v_server->on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (LittleFS.exists(G_T20_WEB_STATIC_INDEX_PATH)) {
            AsyncWebServerResponse* res = request->beginResponse(LittleFS, G_T20_WEB_STATIC_INDEX_PATH, "text/html; charset=utf-8");
            res->addHeader("Cache-Control", G_T20_WEB_STATIC_CACHE_CONTROL);
            request->send(res);
        } else {
            T20_webSendJsonError(request, 404, "index_not_found");
        }
    });
    return true;
#endif
}

bool T20_applyConfigJsonAdvanced(CL_T20_Mfcc::ST_Impl* p, const char* p_json)
{
    if (p == nullptr || p_json == nullptr || g_t20_instance == nullptr) {
        return false;
    }

    if (p->measurement_active) {
        return false;
    }

    ST_T20_Config_t v_cfg = p->cfg;

    uint32_t v_u32 = 0;
    float v_f = 0.0f;
    bool v_b = false;

    if (T20_jsonFindUint(p_json, "\"frame_size\"", &v_u32))            v_cfg.feature.frame_size = (uint16_t)v_u32;
    if (T20_jsonFindUint(p_json, "\"hop_size\"", &v_u32))              v_cfg.feature.hop_size = (uint16_t)v_u32;
    if (T20_jsonFindUint(p_json, "\"mel_filters\"", &v_u32))           v_cfg.feature.mel_filters = (uint16_t)v_u32;
    if (T20_jsonFindUint(p_json, "\"mfcc_coeffs\"", &v_u32))           v_cfg.feature.mfcc_coeffs = (uint16_t)v_u32;
    if (T20_jsonFindUint(p_json, "\"delta_window\"", &v_u32))          v_cfg.feature.delta_window = (uint16_t)v_u32;
    if (T20_jsonFindUint(p_json, "\"sequence_frames\"", &v_u32))       v_cfg.output.sequence_frames = (uint16_t)v_u32;
    if (T20_jsonFindFloat(p_json, "\"sample_rate_hz\"", &v_f))         v_cfg.feature.sample_rate_hz = v_f;
    if (T20_jsonFindBool(p_json, "\"sequence_flatten\"", &v_b))        v_cfg.output.sequence_flatten = v_b;
    if (T20_jsonFindBool(p_json, "\"remove_dc\"", &v_b))               v_cfg.preprocess.remove_dc = v_b;
    if (T20_jsonFindBool(p_json, "\"preemphasis_enable\"", &v_b))      v_cfg.preprocess.preemphasis.enable = v_b;
    if (T20_jsonFindFloat(p_json, "\"preemphasis_alpha\"", &v_f))      v_cfg.preprocess.preemphasis.alpha = v_f;
    if (T20_jsonFindBool(p_json, "\"filter_enable\"", &v_b))           v_cfg.preprocess.filter.enable = v_b;
    if (T20_jsonFindFloat(p_json, "\"cutoff_hz_1\"", &v_f))            v_cfg.preprocess.filter.cutoff_hz_1 = v_f;
    if (T20_jsonFindFloat(p_json, "\"cutoff_hz_2\"", &v_f))            v_cfg.preprocess.filter.cutoff_hz_2 = v_f;
    if (T20_jsonFindFloat(p_json, "\"q_factor\"", &v_f))               v_cfg.preprocess.filter.q_factor = v_f;
    if (T20_jsonFindBool(p_json, "\"noise_gate_enable\"", &v_b))       v_cfg.preprocess.noise.enable_gate = v_b;
    if (T20_jsonFindFloat(p_json, "\"gate_threshold_abs\"", &v_f))     v_cfg.preprocess.noise.gate_threshold_abs = v_f;
    if (T20_jsonFindBool(p_json, "\"spectral_subtract_enable\"", &v_b))v_cfg.preprocess.noise.enable_spectral_subtract = v_b;
    if (T20_jsonFindFloat(p_json, "\"spectral_subtract_strength\"", &v_f)) v_cfg.preprocess.noise.spectral_subtract_strength = v_f;
    if (T20_jsonFindUint(p_json, "\"noise_learn_frames\"", &v_u32))    v_cfg.preprocess.noise.noise_learn_frames = (uint16_t)v_u32;

    EM_T20_OutputMode_t v_out_mode;
    if (T20_parseOutputMode(p_json, &v_out_mode)) {
        v_cfg.output.output_mode = v_out_mode;
    }

    EM_T20_FilterType_t v_filter_type;
    if (T20_parseFilterType(p_json, &v_filter_type)) {
        v_cfg.preprocess.filter.type = v_filter_type;
    }

    return g_t20_instance->setConfig(&v_cfg);
}

bool T20_webPushLiveNow(CL_T20_Mfcc::ST_Impl* p)
{
#if !T20_HAS_ASYNCWEB
    (void)p;
    return false;
#else
    if (p == nullptr || !p->web_attached) {
        return false;
    }

    char v_payload[G_T20_WEB_JSON_BUFFER_SIZE] = {0};

    if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        T20_webBuildLiveBundleJson(p, v_payload, sizeof(v_payload));
        xSemaphoreGive(p->mutex);
    } else {
        snprintf(v_payload, sizeof(v_payload), "{\"ok\":false,\"error\":\"mutex_timeout\"}");
    }

    if (s_t20_sse != nullptr) {
        s_t20_sse->send(v_payload, "live", millis());
    }
    if (s_t20_ws != nullptr) {
        s_t20_ws->textAll(v_payload);
    }

    p->web_last_push_ms = millis();
    return true;
#endif
}

void T20_webPeriodicPush(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr || !p->web_attached) {
        return;
    }

    const uint32_t v_now = millis();
    const uint32_t v_status_hash = T20_calcStatusHash(p);
    const bool v_status_changed = (v_status_hash != p->web_last_status_hash);
    const bool v_live_due = ((v_now - p->web_last_live_change_ms) <= G_T20_WEB_PUSH_MIN_INTERVAL_MS);
    const bool v_force_due = ((v_now - p->web_last_force_push_ms) >= G_T20_WEB_PUSH_FORCE_INTERVAL_MS);

    if (v_status_changed) {
        p->web_last_status_hash = v_status_hash;
        p->web_last_status_change_ms = v_now;
    }

    if (!v_force_due && !v_status_changed && !v_live_due) {
        return;
    }

    if ((v_now - p->web_last_push_ms) < G_T20_WEB_PUSH_MIN_INTERVAL_MS && !v_force_due) {
        return;
    }

    if (T20_webPushLiveNow(p)) {
        p->web_last_force_push_ms = v_now;
    }
}

bool T20_webAttach(CL_T20_Mfcc::ST_Impl* p, void* p_server, const char* p_base_path)
{
    if (p == nullptr || p_server == nullptr) {
        return false;
    }

#if !T20_HAS_ASYNCWEB
    (void)p_base_path;
    return false;
#else
    AsyncWebServer* v_server = reinterpret_cast<AsyncWebServer*>(p_server);

    p->web_server_ptr = p_server;
    p->web_attached = true;
    p->web_last_push_ms = 0;

    const char* v_base = (p_base_path != nullptr && p_base_path[0] != '\0') ? p_base_path : G_T20_WEB_DEFAULT_BASE_PATH;
    strncpy(p->web_base_path, v_base, sizeof(p->web_base_path) - 1);
    p->web_base_path[sizeof(p->web_base_path) - 1] = '\0';

    snprintf(s_t20_sse_path, sizeof(s_t20_sse_path), "%s/live/sse", v_base);
    snprintf(s_t20_ws_path, sizeof(s_t20_ws_path), "%s/live/ws", v_base);

    if (s_t20_sse == nullptr) {
        s_t20_sse = new AsyncEventSource(s_t20_sse_path);
        s_t20_sse->onConnect([](AsyncEventSourceClient* client) {
            if (client != nullptr) {
                client->send("{\"ok\":true,\"type\":\"connected\"}", "hello", millis());
            }
        });
        v_server->addHandler(s_t20_sse);
    }

    if (s_t20_ws == nullptr) {
        s_t20_ws = new AsyncWebSocket(s_t20_ws_path);
        s_t20_ws->onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
            (void)server; (void)arg; (void)data; (void)len;
            if (type == WS_EVT_CONNECT && client != nullptr) {
                client->text("{\"ok\":true,\"type\":\"connected\"}");
            }
        });
        v_server->addHandler(s_t20_ws);
    }

    String s_status     = String(v_base) + "/status";
    String s_liveStatus = String(v_base) + "/live/status";
    String s_config     = String(v_base) + "/config";
    String s_latest     = String(v_base) + "/latest";
    String s_liveLatest = String(v_base) + "/live/latest";
    String s_liveWave   = String(v_base) + "/live/wave";
    String s_liveSeq    = String(v_base) + "/live/sequence";
    String s_livePush   = String(v_base) + "/live/push";
    String s_start      = String(v_base) + "/measurement/start";
    String s_stop       = String(v_base) + "/measurement/stop";
    String s_files      = String(v_base) + "/files";
    String s_rotates    = String(v_base) + "/files/rotates";
    String s_delete     = String(v_base) + "/files/delete";
    String s_cleanup    = String(v_base) + "/files/cleanup";
    String s_cfg_get    = String(v_base) + "/config/runtime";
    String s_cfg_save   = String(v_base) + "/config/runtime/save";
    String s_cfg_export = String(v_base) + "/config/export";
    String s_cfg_import = String(v_base) + "/config/import";
    String s_profile_save = String(v_base) + "/config/profile/save";
    String s_profile_load = String(v_base) + "/config/profile/load";
    String s_raw        = String(v_base) + "/download/raw";
    String s_cfg        = String(v_base) + "/download/config";
    String s_meta       = String(v_base) + "/download/meta";
    String s_evt        = String(v_base) + "/download/event";
    String s_feat       = String(v_base) + "/download/feature";

    auto handle_status = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            char buf[1024] = {0};
            T20_webBuildStatusJson(p, buf, sizeof(buf));
            res->print(buf);
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_latest = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (p->latest_vector_valid) {
                char buf[G_T20_WEB_JSON_BUFFER_SIZE] = {0};
                T20_webBuildLatestJson(p, buf, sizeof(buf));
                res->print(buf);
            } else {
                res->print("{\"ok\":false,\"error\":\"no_latest_feature\"}");
            }
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_wave = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            char buf[G_T20_WEB_JSON_BUFFER_SIZE] = {0};
            T20_webBuildWaveJson(p, buf, sizeof(buf));
            res->print(buf);
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_sequence = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            char buf[G_T20_WEB_JSON_BUFFER_SIZE] = {0};
            T20_webBuildSequenceJson(p, buf, sizeof(buf));
            res->print(buf);
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_config_get = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            res->print("{");
            res->printf("\"version\":\"%s\",", G_T20_VERSION_STR);
            res->printf("\"frame_size\":%u,", p->cfg.feature.frame_size);
            res->printf("\"hop_size\":%u,", p->cfg.feature.hop_size);
            res->printf("\"sample_rate_hz\":%.1f,", p->cfg.feature.sample_rate_hz);
            res->printf("\"mel_filters\":%u,", p->cfg.feature.mel_filters);
            res->printf("\"mfcc_coeffs\":%u,", p->cfg.feature.mfcc_coeffs);
            res->printf("\"delta_window\":%u,", p->cfg.feature.delta_window);
            res->printf("\"sequence_frames\":%u,", p->cfg.output.sequence_frames);
            res->printf("\"sequence_flatten\":%s,", p->cfg.output.sequence_flatten ? "true" : "false");
            res->printf("\"remove_dc\":%s,", p->cfg.preprocess.remove_dc ? "true" : "false");
            res->printf("\"preemphasis_enable\":%s,", p->cfg.preprocess.preemphasis.enable ? "true" : "false");
            res->printf("\"preemphasis_alpha\":%.4f,", p->cfg.preprocess.preemphasis.alpha);
            res->printf("\"filter_enable\":%s,", p->cfg.preprocess.filter.enable ? "true" : "false");
            res->printf("\"filter_type\":%u,", (unsigned)p->cfg.preprocess.filter.type);
            res->printf("\"cutoff_hz_1\":%.3f,", p->cfg.preprocess.filter.cutoff_hz_1);
            res->printf("\"cutoff_hz_2\":%.3f,", p->cfg.preprocess.filter.cutoff_hz_2);
            res->printf("\"q_factor\":%.4f,", p->cfg.preprocess.filter.q_factor);
            res->printf("\"noise_gate_enable\":%s,", p->cfg.preprocess.noise.enable_gate ? "true" : "false");
            res->printf("\"gate_threshold_abs\":%.6f,", p->cfg.preprocess.noise.gate_threshold_abs);
            res->printf("\"spectral_subtract_enable\":%s,", p->cfg.preprocess.noise.enable_spectral_subtract ? "true" : "false");
            res->printf("\"spectral_subtract_strength\":%.4f,", p->cfg.preprocess.noise.spectral_subtract_strength);
            res->printf("\"noise_learn_frames\":%u", p->cfg.preprocess.noise.noise_learn_frames);
            res->print("}");
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    v_server->on(s_status.c_str(), HTTP_GET, handle_status);
    v_server->on(s_liveStatus.c_str(), HTTP_GET, handle_status);
    v_server->on(s_config.c_str(), HTTP_GET, handle_config_get);
    v_server->on(s_latest.c_str(), HTTP_GET, handle_latest);
    v_server->on(s_liveLatest.c_str(), HTTP_GET, handle_latest);
    v_server->on(s_liveWave.c_str(), HTTP_GET, handle_wave);
    v_server->on(s_liveSeq.c_str(), HTTP_GET, handle_sequence);

    v_server->on(s_livePush.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = T20_webPushLiveNow(p);
        request->send(ok ? 200 : 500, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    v_server->on(s_config.c_str(), HTTP_POST, [](AsyncWebServerRequest* request){}, nullptr,
        [p](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String s_body;
            if (index == 0) {
                s_body = "";
                s_body.reserve(total + 8);
            }
            for (size_t i = 0; i < len; ++i) {
                s_body += (char)data[i];
            }
            if (index + len == total) {
                bool ok = T20_applyConfigJsonAdvanced(p, s_body.c_str());
                request->send(ok ? 200 : 400,
                              "application/json; charset=utf-8",
                              ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"config_apply_failed\"}");
            }
        }
    );

    v_server->on(s_start.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = (g_t20_instance != nullptr) ? g_t20_instance->measurementStart() : false;
        if (ok) {
            T20_webPushLiveNow(p);
        }
        request->send(ok ? 200 : 500, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    v_server->on(s_stop.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = (g_t20_instance != nullptr) ? g_t20_instance->measurementStop() : false;
        if (ok) {
            T20_webPushLiveNow(p);
        }
        request->send(ok ? 200 : 500, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });


    v_server->on(s_rotates.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            char buf[4096] = {0};
            T20_webBuildRotateListJson(p, buf, sizeof(buf));
            res->print(buf);
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    });

    v_server->on(s_delete.c_str(), HTTP_POST, [](AsyncWebServerRequest* request){}, nullptr,
        [p](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String s_body;
            if (index == 0) {
                s_body = "";
                s_body.reserve(total + 8);
            }
            for (size_t i = 0; i < len; ++i) {
                s_body += (char)data[i];
            }
            if (index + len == total) {
                const char* key = "\"path\"";
                const char* pos = strstr(s_body.c_str(), key);
                if (pos == nullptr) {
                    request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"path_required\"}");
                    return;
                }
                pos = strchr(pos, ':');
                if (pos == nullptr) {
                    request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"invalid_json\"}");
                    return;
                }
                ++pos;
                while (*pos == ' ' || *pos == '\"') ++pos;
                String path = "";
                while (*pos != '\0' && *pos != '\"' && *pos != '\r' && *pos != '\n' && *pos != '}') {
                    path += *pos++;
                }
                bool ok = T20_rotateListDeleteFile(p, path.c_str());
                request->send(ok ? 200 : 500, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            }
        }
    );


    v_server->on(s_cleanup.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        uint16_t keep = G_T20_RECORDER_ROTATE_KEEP_FILES;
        if (request->hasParam("keep", true)) {
            keep = (uint16_t)request->getParam("keep", true)->value().toInt();
        } else if (request->hasParam("keep")) {
            keep = (uint16_t)request->getParam("keep")->value().toInt();
        }
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            T20_rotateListPrune(p, keep);
            xSemaphoreGive(p->mutex);
        }
        request->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    });

    v_server->on(s_cfg_get.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char buf[1024] = {0};
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            T20_webBuildConfigJson(p, buf, sizeof(buf));
            xSemaphoreGive(p->mutex);
            request->send(200, "application/json; charset=utf-8", buf);
            return;
        }
        request->send(500, "application/json; charset=utf-8", "{\"ok\":false}");
    });

    v_server->on(s_cfg_save.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        bool ok = false;
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ok = T20_webSaveConfigToLittleFs(p);
            xSemaphoreGive(p->mutex);
        }
        request->send(ok ? 200 : 500, "application/json; charset=utf-8", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });



    v_server->on(s_profile_save.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        uint8_t v_idx = G_T20_CFG_PROFILE_INDEX_DEFAULT;
        if (request->hasParam("index", true)) {
            v_idx = (uint8_t)request->getParam("index", true)->value().toInt();
        } else if (request->hasParam("index")) {
            v_idx = (uint8_t)request->getParam("index")->value().toInt();
        }

        bool v_ok = false;
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            v_ok = T20_saveProfileToLittleFs(v_idx, &p->cfg);
            xSemaphoreGive(p->mutex);
        }
        request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    v_server->on(s_profile_load.c_str(), HTTP_POST, [p](AsyncWebServerRequest* request) {
        uint8_t v_idx = G_T20_CFG_PROFILE_INDEX_DEFAULT;
        if (request->hasParam("index", true)) {
            v_idx = (uint8_t)request->getParam("index", true)->value().toInt();
        } else if (request->hasParam("index")) {
            v_idx = (uint8_t)request->getParam("index")->value().toInt();
        }

        ST_T20_Config_t v_cfg;
        if (!T20_loadProfileFromLittleFs(v_idx, &v_cfg)) {
            request->send(404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"profile_not_found\"}");
            return;
        }

        bool v_ok = false;
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            p->cfg = v_cfg;
            v_ok = T20_configureFilter(p);
            T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3));
            if (v_ok) {
                p->current_profile_index = v_idx;
                T20_saveRuntimeConfigToLittleFs(&p->cfg);
            }
            xSemaphoreGive(p->mutex);
        }
        request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    v_server->on(s_cfg_export.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        char v_json[G_T20_CFG_JSON_BUFFER_SIZE] = {0};
        bool v_ok = false;
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            v_ok = T20_buildConfigJsonText(&p->cfg, v_json, sizeof(v_json));
            xSemaphoreGive(p->mutex);
        }
        request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? v_json : "{\"ok\":false}");
    });

    v_server->on(s_cfg_import.c_str(), HTTP_POST, [](AsyncWebServerRequest* request){}, nullptr,
        [p](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            static String s_body;
            if (index == 0) {
                s_body = "";
                s_body.reserve(total + 8);
            }
            for (size_t i = 0; i < len; ++i) {
                s_body += (char)data[i];
            }
            if (index + len == total) {
                ST_T20_Config_t v_cfg;
                if (!T20_parseConfigJsonText(s_body.c_str(), &v_cfg)) {
                    request->send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"invalid_config\"}");
                    return;
                }

                bool v_ok = false;
                if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    p->cfg = v_cfg;
                    v_ok = T20_configureFilter(p);
                    T20_seqInit(&p->seq_rb, p->cfg.output.sequence_frames, (uint16_t)(p->cfg.feature.mfcc_coeffs * 3));
                    if (v_ok) {
                        T20_saveRuntimeConfigToLittleFs(&p->cfg);
                    }
                    xSemaphoreGive(p->mutex);
                }

                request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? "{\"ok\":true}" : "{\"ok\":false}");
            }
        }
    );

    v_server->on(s_files.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            res->print("{");
            res->printf("\"session_open\":%s,", p->recorder.session_open ? "true" : "false");
            res->printf("\"session_dir\":\"%s\",", p->recorder.session_dir);
            res->printf("\"raw\":\"%s\",", p->recorder.raw_path);
            res->printf("\"config\":\"%s\",", p->recorder.cfg_path);
            res->printf("\"meta\":\"%s\",", p->recorder.meta_path);
            res->printf("\"event\":\"%s\",", p->recorder.event_path);
            res->printf("\"feature\":\"%s\"", p->recorder.feature_path);
            res->print("}");
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    });

    v_server->on(s_raw.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) { T20_webSendSdFile(request, p->recorder.raw_path, "application/octet-stream"); });
    v_server->on(s_cfg.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) { T20_webSendSdFile(request, p->recorder.cfg_path, "application/json; charset=utf-8"); });
    v_server->on(s_meta.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) { T20_webSendSdFile(request, p->recorder.meta_path, "text/plain; charset=utf-8"); });
    v_server->on(s_evt.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) { T20_webSendSdFile(request, p->recorder.event_path, "text/plain; charset=utf-8"); });
    v_server->on(s_feat.c_str(), HTTP_GET, [p](AsyncWebServerRequest* request) { T20_webSendSdFile(request, p->recorder.feature_path, "text/csv; charset=utf-8"); });

    return true;
#endif
}

void T20_webDetach(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return;
    }
    p->web_attached = false;
    p->web_server_ptr = nullptr;
    p->web_last_push_ms = 0;
    memset(p->web_base_path, 0, sizeof(p->web_base_path));
}
