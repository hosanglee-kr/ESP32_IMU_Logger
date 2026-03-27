#include "T20_Mfcc_Inter_019.h"

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

/*
===============================================================================
소스명: T20_Mfcc_Web_019.cpp
버전: v019

[기능 스펙]
- AsyncWeb 기반 제어/다운로드 엔드포인트 제공
- LittleFS 정적 파일(html/css/js) 제공
- 상태 조회 / 설정 조회 / 설정 적용 / 최신 특징 조회 / live waveform / live sequence 제공
- 측정 시작 / 측정 종료 / 파일 다운로드 제공

[이번 단계 반영]
1. /config GET
2. /config POST
3. /live/status
4. /live/latest
5. /live/wave
6. /live/sequence
7. LittleFS 정적 파일 제공

[향후 단계 TODO]
- websocket / SSE 기반 실시간 push
- live waveform decimation/aggregation 최적화
- 인증 / 접근 제어
- 고급 JSON 파서 기반 전체 설정 반영
===============================================================================
*/

static void T20_webWriteConfigJson(Print& p_out, const ST_T20_Config_t& p_cfg)
{
    p_out.print("{");
    p_out.printf("\"version\":\"%s\",", G_T20_VERSION_STR);
    p_out.printf("\"frame_size\":%u,", p_cfg.feature.frame_size);
    p_out.printf("\"hop_size\":%u,", p_cfg.feature.hop_size);
    p_out.printf("\"sample_rate_hz\":%.1f,", p_cfg.feature.sample_rate_hz);
    p_out.printf("\"mel_filters\":%u,", p_cfg.feature.mel_filters);
    p_out.printf("\"mfcc_coeffs\":%u,", p_cfg.feature.mfcc_coeffs);
    p_out.printf("\"delta_window\":%u,", p_cfg.feature.delta_window);
    p_out.printf("\"sequence_frames\":%u,", p_cfg.output.sequence_frames);
    p_out.printf("\"sequence_flatten\":%s,", p_cfg.output.sequence_flatten ? "true" : "false");
    p_out.printf("\"remove_dc\":%s,", p_cfg.preprocess.remove_dc ? "true" : "false");
    p_out.printf("\"preemphasis_enable\":%s,", p_cfg.preprocess.preemphasis.enable ? "true" : "false");
    p_out.printf("\"preemphasis_alpha\":%.4f,", p_cfg.preprocess.preemphasis.alpha);
    p_out.printf("\"filter_enable\":%s,", p_cfg.preprocess.filter.enable ? "true" : "false");
    p_out.printf("\"filter_type\":%u,", (unsigned)p_cfg.preprocess.filter.type);
    p_out.printf("\"cutoff_hz_1\":%.3f,", p_cfg.preprocess.filter.cutoff_hz_1);
    p_out.printf("\"cutoff_hz_2\":%.3f,", p_cfg.preprocess.filter.cutoff_hz_2);
    p_out.printf("\"q_factor\":%.4f,", p_cfg.preprocess.filter.q_factor);
    p_out.printf("\"noise_gate_enable\":%s,", p_cfg.preprocess.noise.enable_gate ? "true" : "false");
    p_out.printf("\"gate_threshold_abs\":%.6f,", p_cfg.preprocess.noise.gate_threshold_abs);
    p_out.printf("\"spectral_subtract_enable\":%s,", p_cfg.preprocess.noise.enable_spectral_subtract ? "true" : "false");
    p_out.printf("\"spectral_subtract_strength\":%.4f,", p_cfg.preprocess.noise.spectral_subtract_strength);
    p_out.printf("\"noise_learn_frames\":%u", p_cfg.preprocess.noise.noise_learn_frames);
    p_out.print("}");
}

static void T20_webWriteStatusJson(Print& p_out, CL_T20_Mfcc::ST_Impl* p)
{
    p_out.print("{");
    p_out.printf("\"initialized\":%s,", p->initialized ? "true" : "false");
    p_out.printf("\"running\":%s,", p->running ? "true" : "false");
    p_out.printf("\"session_state\":%u,", (unsigned)p->session_state);
    p_out.printf("\"dropped_frames\":%lu,", (unsigned long)p->dropped_frames);
    p_out.printf("\"queued_records\":%lu,", (unsigned long)p->recorder.queued_record_count);
    p_out.printf("\"dropped_records\":%lu,", (unsigned long)p->recorder.dropped_record_count);
    p_out.printf("\"written_records\":%lu,", (unsigned long)p->recorder.written_record_count);
    p_out.printf("\"web_attached\":%s", p->web_attached ? "true" : "false");
    p_out.print("}");
}

static void T20_webWriteLatestJson(Print& p_out, const ST_T20_FeatureVector_t& p_feat)
{
    p_out.print("{");
    p_out.printf("\"log_mel_len\":%u,", p_feat.log_mel_len);
    p_out.printf("\"mfcc_len\":%u,", p_feat.mfcc_len);
    p_out.printf("\"delta_len\":%u,", p_feat.delta_len);
    p_out.printf("\"delta2_len\":%u,", p_feat.delta2_len);
    p_out.printf("\"vector_len\":%u,", p_feat.vector_len);

    p_out.print("\"mfcc\":[");
    for (uint16_t i = 0; i < p_feat.mfcc_len; ++i) {
        if (i != 0) p_out.print(",");
        p_out.print(p_feat.mfcc[i], 6);
    }
    p_out.print("],");

    p_out.print("\"delta\":[");
    for (uint16_t i = 0; i < p_feat.delta_len; ++i) {
        if (i != 0) p_out.print(",");
        p_out.print(p_feat.delta[i], 6);
    }
    p_out.print("],");

    p_out.print("\"delta2\":[");
    for (uint16_t i = 0; i < p_feat.delta2_len; ++i) {
        if (i != 0) p_out.print(",");
        p_out.print(p_feat.delta2[i], 6);
    }
    p_out.print("]");

    p_out.print("}");
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

    const char* v_base = (p_base_path != nullptr && p_base_path[0] != '\0') ? p_base_path : G_T20_WEB_DEFAULT_BASE_PATH;
    strncpy(p->web_base_path, v_base, sizeof(p->web_base_path) - 1);
    p->web_base_path[sizeof(p->web_base_path) - 1] = '\0';

    String s_status     = String(v_base) + "/status";
    String s_liveStatus = String(v_base) + "/live/status";
    String s_config     = String(v_base) + "/config";
    String s_latest     = String(v_base) + "/latest";
    String s_liveLatest = String(v_base) + "/live/latest";
    String s_liveWave   = String(v_base) + "/live/wave";
    String s_liveSeq    = String(v_base) + "/live/sequence";
    String s_start      = String(v_base) + "/measurement/start";
    String s_stop       = String(v_base) + "/measurement/stop";
    String s_files      = String(v_base) + "/files";
    String s_raw        = String(v_base) + "/download/raw";
    String s_cfg        = String(v_base) + "/download/config";
    String s_meta       = String(v_base) + "/download/meta";
    String s_evt        = String(v_base) + "/download/event";
    String s_feat       = String(v_base) + "/download/feature";

    auto handle_status = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            T20_webWriteStatusJson(*res, p);
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_config_get = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            T20_webWriteConfigJson(*res, p->cfg);
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
                T20_webWriteLatestJson(*res, p->latest_feature);
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
            res->print("{\"frame_len\":");
            res->print((unsigned)G_T20_FFT_SIZE);
            res->print(",\"samples\":[");
            const uint16_t v_emit = (G_T20_FFT_SIZE > G_T20_WEB_LIVE_WAVE_MAX_SAMPLES) ? G_T20_WEB_LIVE_WAVE_MAX_SAMPLES : G_T20_FFT_SIZE;
            for (uint16_t i = 0; i < v_emit; ++i) {
                if (i != 0) res->print(",");
                res->print(p->latest_wave_frame[i], 6);
            }
            res->print("]}");
            xSemaphoreGive(p->mutex);
        } else {
            res->print("{\"ok\":false,\"error\":\"mutex_timeout\"}");
        }
        request->send(res);
    };

    auto handle_sequence = [p](AsyncWebServerRequest* request) {
        AsyncResponseStream* res = request->beginResponseStream("application/json; charset=utf-8");
        if (p->mutex != nullptr && xSemaphoreTake(p->mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            const uint16_t v_need = p->seq_rb.frames * p->seq_rb.feature_dim;
            res->print("{");
            res->printf("\"ready\":%s,", p->latest_sequence_valid ? "true" : "false");
            res->printf("\"frames\":%u,", p->seq_rb.frames);
            res->printf("\"feature_dim\":%u,", p->seq_rb.feature_dim);
            res->print("\"data\":[");
            const uint16_t v_emit = (v_need > G_T20_WEB_LIVE_SEQ_MAX_VALUES) ? G_T20_WEB_LIVE_SEQ_MAX_VALUES : v_need;
            for (uint16_t i = 0; i < v_emit; ++i) {
                if (i != 0) res->print(",");
                res->print(p->latest_sequence_flat[i], 6);
            }
            res->print("]}");
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
                bool v_ok = T20_applyConfigJson(p, s_body.c_str());
                request->send(v_ok ? 200 : 400,
                              "application/json; charset=utf-8",
                              v_ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"config_apply_failed\"}");
            }
        }
    );

    v_server->on(s_start.c_str(), HTTP_POST, [](AsyncWebServerRequest* request) {
        bool v_ok = (g_t20_instance != nullptr) ? g_t20_instance->measurementStart() : false;
        request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    v_server->on(s_stop.c_str(), HTTP_POST, [](AsyncWebServerRequest* request) {
        bool v_ok = (g_t20_instance != nullptr) ? g_t20_instance->measurementStop() : false;
        request->send(v_ok ? 200 : 500, "application/json; charset=utf-8", v_ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

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
    memset(p->web_base_path, 0, sizeof(p->web_base_path));
}
