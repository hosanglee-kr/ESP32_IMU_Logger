// LOG10_Log_010.h

#pragma once
/*
 * ------------------------------------------------------
 * 소스명 : LOG10_Log_010.h
 * 모듈약어 : LOG
 * 모듈명 : Logger
 * ------------------------------------------------------
 * [구현 규칙]
 * ESPAsyncWebServer 사용
 *  - 항상 소스 시작 주석 부분 체계 유지 및 내용 업데이트
 *  - 소스 시작 주석 부분 구현규칙, 코드네이밍규칙 내용 그대로 유지, 수정금지
 *  - ArduinoJson v7.x.x 사용 (v6 이하 사용 금지)
 *  - JsonDocument 단일 타입만 사용  (DynamicJsonDocument, StaticJsonDocument 사용 금지)
 *  - createNestedArray/Object/containsKey 사용 금지
 *  - memset + strlcpy 기반 안전 초기화
 *  - 주석/필드명은 JSON 구조와 동일하게 유지
 *  - typedef enum 적극 사용
 *  - 변수명은 가능한 해석 가능하게
 * ------------------------------------------------------
 * [코드 네이밍 규칙]
 *   - namespace 명        : LOG_ 접두사
 *   - namespace 내 상수   : LOG 접두사 미사용
 *   - 전역 상수,매크로    : G_LOG_ 접두사
 *   - 전역 변수           : g_LOG_ 접두사
 *   - 전역 함수           : LOG_ 접두사
 *   - type                : LOG_이름_t
 *   - enum 상수           : EN_LOG_ 접두사
 *   - 구조체              : ST_LOG_ 접두사
 *   - 클래스명            : CL_LOG_ 접두사
 *   - 클래스 private 멤버 : _ 접두사
 *   - 클래스 정적 멤버    : s_ 접두사
 *   - 함수 로컬 변수      : v_ 접두사
 *   - 함수 인자           : p_ 접두사
 * ------------------------------------------------------
 * 설명
 *  - Header Only Logger
 *  - Serial 출력 + Ring Buffer + JSON Text Export
 *  - LittleFS 저장 옵션 지원
 *  - Diagnostics(consecFail/spike/lastFailMs) 지원
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include <Stream.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <ArduinoJson.h>

#ifndef G_LOG_THREAD_SAFE
#define G_LOG_THREAD_SAFE 1
#endif

#ifndef G_LOG_ENABLE_LFS
#define G_LOG_ENABLE_LFS 1
#endif

#ifndef G_LOG_BUFFER_SIZE
#define G_LOG_BUFFER_SIZE 256
#endif

#ifndef G_LOG_MESSAGE_SIZE
#define G_LOG_MESSAGE_SIZE 128
#endif

#ifndef G_LOG_JSON_RESERVE_SIZE
#define G_LOG_JSON_RESERVE_SIZE 4608
#endif

#if G_LOG_ENABLE_LFS
#include <LittleFS.h>
#endif

// ------------------------------------------------------
// 로그 레벨 정의
// ------------------------------------------------------
typedef enum : uint8_t
{
    EN_LOG_LEVEL_NONE  = 0,
    EN_LOG_LEVEL_ERROR = 1,
    EN_LOG_LEVEL_WARN  = 2,
    EN_LOG_LEVEL_INFO  = 3,
    EN_LOG_LEVEL_DEBUG = 4
} LOG_LEVEL_t;

// ------------------------------------------------------
// ANSI 색상 코드 상수
// ------------------------------------------------------
#define G_LOG_COLOR_RESET  "\033[0m"
#define G_LOG_COLOR_RED    "\033[31m"
#define G_LOG_COLOR_YELLOW "\033[33m"
#define G_LOG_COLOR_GREEN  "\033[32m"
#define G_LOG_COLOR_CYAN   "\033[36m"
#define G_LOG_COLOR_WHITE  "\033[37m"

// ------------------------------------------------------
// 로그 엔트리 구조체
// ------------------------------------------------------
typedef struct
{
    uint32_t    timestamp;
    LOG_LEVEL_t level;
    char        message[G_LOG_MESSAGE_SIZE];
} ST_LOG_Entry_t;

// ------------------------------------------------------
// 상태 스냅샷 구조체
// JSON key 주석 유지
// ------------------------------------------------------
typedef struct
{
    uint32_t drop;          // drop
    uint32_t lastErrMs;     // lastErrMs
    uint32_t lastWarnMs;    // lastWarnMs

    uint32_t consecFail;    // diag.consecFail
    uint32_t spike;         // diag.spike
    uint32_t lastFailMs;    // diag.lastFailMs

    uint32_t cntErr;        // lvCount.err
    uint32_t cntWarn;       // lvCount.warn
    uint32_t cntInfo;       // lvCount.info
    uint32_t cntDbg;        // lvCount.dbg

    uint16_t buffered;      // buffered
    uint16_t capacity;      // capacity
} ST_LOG_Stats_t;

namespace LOG_
{
    static constexpr uint16_t kBufferSize = G_LOG_BUFFER_SIZE;
    static constexpr uint16_t kMessageSize = G_LOG_MESSAGE_SIZE;
    static constexpr uint16_t kLevelCount = 5;
    static constexpr uint16_t kDefaultJsonMax = 160;

    static inline const char* callerOrUnknown(const char* p_caller)
    {
        return ((p_caller != nullptr) && (p_caller[0] != '\0')) ? p_caller : "unknown";
    }
}

// ------------------------------------------------------
// Logger 클래스
// ------------------------------------------------------
class CL_LOG_Logger
{
  public:
    static constexpr uint16_t BUFFER_SIZE = LOG_::kBufferSize;
    static constexpr uint16_t MESSAGE_SIZE = LOG_::kMessageSize;

  public:
    // --------------------------------------------------
    // 초기화
    // --------------------------------------------------
    static void begin(Stream& p_serial = Serial)
    {
        _serial = &p_serial;

#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        memset(s_buffer, 0, sizeof(s_buffer));
        s_head = 0;
        s_count = 0;
        s_dropCount = 0;

        memset(s_lvCount, 0, sizeof(s_lvCount));
        s_lastErrMs = 0;
        s_lastWarnMs = 0;

        s_consecFail = 0;
        s_spike = 0;
        s_lastFailMs = 0;
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif

        delay(30);
        printBanner();
    }

    // --------------------------------------------------
    // 설정
    // --------------------------------------------------
    static void setLevel(LOG_LEVEL_t p_level)
    {
        _logLevel = p_level;
    }

    static LOG_LEVEL_t getLevel()
    {
        return _logLevel;
    }

    static void enableTimestamp(bool p_enable)
    {
        _showTimestamp = p_enable;
    }

    static void enableMemUsage(bool p_enable)
    {
        _showMemUsage = p_enable;
    }

    // --------------------------------------------------
    // 상태 조회
    // --------------------------------------------------
    static uint32_t getDropCount()
    {
        return s_dropCount;
    }

    static uint32_t getLastErrMs()
    {
        return s_lastErrMs;
    }

    static uint32_t getLastWarnMs()
    {
        return s_lastWarnMs;
    }

    static uint32_t getConsecFail()
    {
        return s_consecFail;
    }

    static uint32_t getSpike()
    {
        return s_spike;
    }

    static uint32_t getLastFailMs()
    {
        return s_lastFailMs;
    }

    static uint32_t getCountByLevel(LOG_LEVEL_t p_level)
    {
        uint8_t v_level = (uint8_t)p_level;
        if (v_level >= LOG_::kLevelCount)
        {
            return 0;
        }
        return s_lvCount[v_level];
    }

    static void getStats(ST_LOG_Stats_t& p_out_stats)
    {
        memset(&p_out_stats, 0, sizeof(p_out_stats));
#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        p_out_stats.drop       = s_dropCount;
        p_out_stats.lastErrMs  = s_lastErrMs;
        p_out_stats.lastWarnMs = s_lastWarnMs;

        p_out_stats.consecFail = s_consecFail;
        p_out_stats.spike      = s_spike;
        p_out_stats.lastFailMs = s_lastFailMs;

        p_out_stats.cntErr     = s_lvCount[(uint8_t)EN_LOG_LEVEL_ERROR];
        p_out_stats.cntWarn    = s_lvCount[(uint8_t)EN_LOG_LEVEL_WARN];
        p_out_stats.cntInfo    = s_lvCount[(uint8_t)EN_LOG_LEVEL_INFO];
        p_out_stats.cntDbg     = s_lvCount[(uint8_t)EN_LOG_LEVEL_DEBUG];

        p_out_stats.buffered   = s_count;
        p_out_stats.capacity   = BUFFER_SIZE;
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif
    }

    // --------------------------------------------------
    // 현장 운용 래퍼
    // --------------------------------------------------
    static void markOk()
    {
#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        s_consecFail = 0;
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif
    }

    static void markSpike(const char* p_tag, int32_t p_value = 0)
    {
#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        s_spike++;
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif
        log(EN_LOG_LEVEL_WARN, "[SPIKE] %s=%ld", ((p_tag != nullptr) ? p_tag : "unknown"), (long)p_value);
    }

    static void markFail(const char* p_fmt, ...)
    {
        if (_serial == nullptr)
        {
            return;
        }

        char v_message[MESSAGE_SIZE];
        memset(v_message, 0, sizeof(v_message));

        va_list v_args;
        va_start(v_args, p_fmt);
        vsnprintf(v_message, sizeof(v_message), (p_fmt != nullptr) ? p_fmt : "", v_args);
        va_end(v_args);

        log(EN_LOG_LEVEL_ERROR, "[FAIL] %s", v_message);
    }

    // --------------------------------------------------
    // 로그 출력
    // --------------------------------------------------
    static void log(LOG_LEVEL_t p_level, const char* p_fmt, ...)
    {
        if ((_serial == nullptr) || (p_level == EN_LOG_LEVEL_NONE))
        {
            return;
        }

        if (p_level > _logLevel)
        {
            return;
        }

        char v_message[MESSAGE_SIZE];
        memset(v_message, 0, sizeof(v_message));

        va_list v_args;
        va_start(v_args, p_fmt);
        vsnprintf(v_message, sizeof(v_message), (p_fmt != nullptr) ? p_fmt : "", v_args);
        va_end(v_args);

        _pushToRing(p_level, v_message);
        _printToSerial(p_level, v_message);
    }

    // --------------------------------------------------
    // RingBuffer clear
    // --------------------------------------------------
    static void clear()
    {
#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        memset(s_buffer, 0, sizeof(s_buffer));
        s_head = 0;
        s_count = 0;
        s_dropCount = 0;
        memset(s_lvCount, 0, sizeof(s_lvCount));
        s_lastErrMs = 0;
        s_lastWarnMs = 0;
        s_consecFail = 0;
        s_spike = 0;
        s_lastFailMs = 0;
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif
    }

    // --------------------------------------------------
    // JSON Text Export
    //  - createNestedArray/Object 없이 문자열로 JSON 생성
    //  - p_max: 최신 p_max개만 출력
    // --------------------------------------------------
    static void getLogsAsJsonText(String& p_out_json, uint16_t p_max = LOG_::kDefaultJsonMax)
    {
        p_out_json.reserve(G_LOG_JSON_RESERVE_SIZE);
        p_out_json = "{\"logs\":[";

        uint16_t v_total = 0;
        uint16_t v_outputCount = 0;
        uint16_t v_startIndex = 0;

        uint32_t v_drop = 0;
        uint32_t v_lastErrMs = 0;
        uint32_t v_lastWarnMs = 0;

        uint32_t v_consecFail = 0;
        uint32_t v_spike = 0;
        uint32_t v_lastFailMs = 0;

        uint32_t v_errCount = 0;
        uint32_t v_warnCount = 0;
        uint32_t v_infoCount = 0;
        uint32_t v_dbgCount = 0;

#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        v_total = s_count;
        v_outputCount = v_total;
        if (p_max < v_outputCount)
        {
            v_outputCount = p_max;
        }

        v_startIndex = s_head;
        if (v_total > v_outputCount)
        {
            v_startIndex = (uint16_t)((s_head + (v_total - v_outputCount)) % BUFFER_SIZE);
        }

        for (uint16_t v_i = 0; v_i < v_outputCount; v_i++)
        {
            uint16_t v_index = (uint16_t)((v_startIndex + v_i) % BUFFER_SIZE);
            const ST_LOG_Entry_t& v_entry = s_buffer[v_index];

            if (v_i > 0)
            {
                p_out_json += ",";
            }

            p_out_json += "{\"ts\":";
            p_out_json += String(v_entry.timestamp);
            p_out_json += ",\"lv\":";
            p_out_json += String((int)v_entry.level);
            p_out_json += ",\"msg\":\"";
            _appendJsonEscaped(p_out_json, v_entry.message);
            p_out_json += "\"}";
        }

        v_drop       = s_dropCount;
        v_lastErrMs  = s_lastErrMs;
        v_lastWarnMs = s_lastWarnMs;

        v_consecFail = s_consecFail;
        v_spike      = s_spike;
        v_lastFailMs = s_lastFailMs;

        v_errCount   = s_lvCount[(uint8_t)EN_LOG_LEVEL_ERROR];
        v_warnCount  = s_lvCount[(uint8_t)EN_LOG_LEVEL_WARN];
        v_infoCount  = s_lvCount[(uint8_t)EN_LOG_LEVEL_INFO];
        v_dbgCount   = s_lvCount[(uint8_t)EN_LOG_LEVEL_DEBUG];
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif

        p_out_json += "],\"drop\":";
        p_out_json += String(v_drop);

        p_out_json += ",\"diag\":{\"consecFail\":";
        p_out_json += String(v_consecFail);
        p_out_json += ",\"spike\":";
        p_out_json += String(v_spike);
        p_out_json += ",\"lastFailMs\":";
        p_out_json += String(v_lastFailMs);
        p_out_json += "}";

        p_out_json += ",\"lvCount\":{\"err\":";
        p_out_json += String(v_errCount);
        p_out_json += ",\"warn\":";
        p_out_json += String(v_warnCount);
        p_out_json += ",\"info\":";
        p_out_json += String(v_infoCount);
        p_out_json += ",\"dbg\":";
        p_out_json += String(v_dbgCount);
        p_out_json += "}";

        p_out_json += ",\"lastErrMs\":";
        p_out_json += String(v_lastErrMs);
        p_out_json += ",\"lastWarnMs\":";
        p_out_json += String(v_lastWarnMs);
        p_out_json += "}";
    }

    // --------------------------------------------------
    // JsonDocument Export
    //  - ArduinoJson v7 JsonDocument 단일 타입만 사용
    //  - createNestedArray/Object/containsKey 사용 금지 정책 준수
    // --------------------------------------------------
    static bool getStatsAsJsonDocument(JsonDocument& p_doc)
    {
        p_doc.clear();

        JsonObject v_root = p_doc.to<JsonObject>();
        v_root["drop"] = s_dropCount;
        v_root["lastErrMs"] = s_lastErrMs;
        v_root["lastWarnMs"] = s_lastWarnMs;

        JsonObject v_diag = v_root["diag"].to<JsonObject>();
        v_diag["consecFail"] = s_consecFail;
        v_diag["spike"] = s_spike;
        v_diag["lastFailMs"] = s_lastFailMs;

        JsonObject v_lvCount = v_root["lvCount"].to<JsonObject>();
        v_lvCount["err"] = s_lvCount[(uint8_t)EN_LOG_LEVEL_ERROR];
        v_lvCount["warn"] = s_lvCount[(uint8_t)EN_LOG_LEVEL_WARN];
        v_lvCount["info"] = s_lvCount[(uint8_t)EN_LOG_LEVEL_INFO];
        v_lvCount["dbg"] = s_lvCount[(uint8_t)EN_LOG_LEVEL_DEBUG];

        v_root["buffered"] = s_count;
        v_root["capacity"] = BUFFER_SIZE;
        return true;
    }

#if G_LOG_ENABLE_LFS
    // --------------------------------------------------
    // 파일 저장 (옵션)
    // --------------------------------------------------
    static bool saveToFile(const char* p_path = "/json/debug.json", uint16_t p_max = 200)
    {
        if ((p_path == nullptr) || (p_path[0] == '\0'))
        {
            return false;
        }

        File v_file = LittleFS.open(p_path, "w");
        if (!v_file)
        {
            return false;
        }

        String v_json;
        getLogsAsJsonText(v_json, p_max);
        v_file.print(v_json);
        v_file.flush();
        v_file.close();
        return true;
    }
#endif

    static void printBanner()
    {
        if (_serial == nullptr)
        {
            return;
        }

        _serial->println(F("\r\n------------------------------------------------------"));
        _serial->println(F(" LOG Logger (LOG_LOG_001, Header Only + RingBuffer + JSON + Diagnostics)"));
        _serial->println(F("------------------------------------------------------"));
    }

  private:
    static void _pushToRing(LOG_LEVEL_t p_level, const char* p_message)
    {
#if G_LOG_THREAD_SAFE
        portENTER_CRITICAL(&s_mux);
#endif
        if ((uint8_t)p_level < LOG_::kLevelCount)
        {
            s_lvCount[(uint8_t)p_level]++;
        }

        if (p_level == EN_LOG_LEVEL_ERROR)
        {
            s_lastErrMs = millis();
            s_consecFail++;
            s_lastFailMs = s_lastErrMs;
        }

        if (p_level == EN_LOG_LEVEL_WARN)
        {
            s_lastWarnMs = millis();
        }

        uint16_t v_index = (uint16_t)((s_head + s_count) % BUFFER_SIZE);
        s_buffer[v_index].timestamp = millis();
        s_buffer[v_index].level = p_level;
        strlcpy(s_buffer[v_index].message, (p_message != nullptr) ? p_message : "", sizeof(s_buffer[v_index].message));

        if (s_count < BUFFER_SIZE)
        {
            s_count++;
        }
        else
        {
            s_head = (uint16_t)((s_head + 1) % BUFFER_SIZE);
            s_dropCount++;
        }
#if G_LOG_THREAD_SAFE
        portEXIT_CRITICAL(&s_mux);
#endif
    }

    static void _printToSerial(LOG_LEVEL_t p_level, const char* p_message)
    {
        if (_serial == nullptr)
        {
            return;
        }

        const char* v_color = _getColor(p_level);
        const char* v_tag = _getTag(p_level);

        if (_showTimestamp)
        {
            uint32_t v_ms = millis();
            _serial->printf("[%lu.%03u] ", (unsigned long)(v_ms / 1000UL), (uint16_t)(v_ms % 1000UL));
        }

        _serial->printf("%s[%s]%s %s\r\n", v_color, v_tag, G_LOG_COLOR_RESET, (p_message != nullptr) ? p_message : "");

        if (_showMemUsage)
        {
            _serial->printf("   %s(Free:%luB)%s\r\n", G_LOG_COLOR_CYAN, (unsigned long)ESP.getFreeHeap(), G_LOG_COLOR_RESET);
        }
    }

    static const char* _getColor(LOG_LEVEL_t p_level)
    {
        switch (p_level)
        {
            case EN_LOG_LEVEL_ERROR: return G_LOG_COLOR_RED;
            case EN_LOG_LEVEL_WARN:  return G_LOG_COLOR_YELLOW;
            case EN_LOG_LEVEL_INFO:  return G_LOG_COLOR_GREEN;
            case EN_LOG_LEVEL_DEBUG: return G_LOG_COLOR_CYAN;
            default:                 return G_LOG_COLOR_WHITE;
        }
    }

    static const char* _getTag(LOG_LEVEL_t p_level)
    {
        switch (p_level)
        {
            case EN_LOG_LEVEL_ERROR: return "ERR";
            case EN_LOG_LEVEL_WARN:  return "WRN";
            case EN_LOG_LEVEL_INFO:  return "INF";
            case EN_LOG_LEVEL_DEBUG: return "DBG";
            default:                 return "LOG";
        }
    }

    static void _appendJsonEscaped(String& p_out_json, const char* p_string)
    {
        if (p_string == nullptr)
        {
            return;
        }

        for (size_t v_i = 0; p_string[v_i] != '\0'; v_i++)
        {
            char v_char = p_string[v_i];
            if ((v_char == '"') || (v_char == '\\'))
            {
                p_out_json += '\\';
                p_out_json += v_char;
            }
            else if (v_char == '\n')
            {
                p_out_json += "\\n";
            }
            else if (v_char == '\r')
            {
                p_out_json += "\\r";
            }
            else if (v_char == '\t')
            {
                p_out_json += "\\t";
            }
            else
            {
                p_out_json += v_char;
            }
        }
    }

  private:
    static inline Stream*      _serial = nullptr;
    static inline LOG_LEVEL_t  _logLevel = EN_LOG_LEVEL_INFO;
    static inline bool         _showTimestamp = true;
    static inline bool         _showMemUsage = false;

#if G_LOG_THREAD_SAFE
    static inline portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
#endif

    static inline ST_LOG_Entry_t s_buffer[BUFFER_SIZE];
    static inline uint16_t       s_head = 0;
    static inline uint16_t       s_count = 0;

    static inline uint32_t       s_dropCount = 0;
    static inline uint32_t       s_lvCount[LOG_::kLevelCount] = {0, 0, 0, 0, 0};
    static inline uint32_t       s_lastErrMs = 0;
    static inline uint32_t       s_lastWarnMs = 0;

    static inline uint32_t       s_consecFail = 0;
    static inline uint32_t       s_spike = 0;
    static inline uint32_t       s_lastFailMs = 0;
};

// ------------------------------------------------------
// 전역 로깅 매크로 (LOG 접두사)
// ------------------------------------------------------
#define LOGE(_fmt, ...) CL_LOG_Logger::log(EN_LOG_LEVEL_ERROR, "[%s] " _fmt, __func__, ##__VA_ARGS__)
#define LOGW(_fmt, ...) CL_LOG_Logger::log(EN_LOG_LEVEL_WARN,  "[%s] " _fmt, __func__, ##__VA_ARGS__)
#define LOGI(_fmt, ...) CL_LOG_Logger::log(EN_LOG_LEVEL_INFO,  "[%s] " _fmt, __func__, ##__VA_ARGS__)
#define LOGD(_fmt, ...) CL_LOG_Logger::log(EN_LOG_LEVEL_DEBUG, "[%s] " _fmt, __func__, ##__VA_ARGS__)

#define LOG_LOGE_C(_caller, _fmt, ...) \
    CL_LOG_Logger::log(EN_LOG_LEVEL_ERROR, "[%s] " _fmt, LOG_::callerOrUnknown((_caller)), ##__VA_ARGS__)
#define LOG_LOGW_C(_caller, _fmt, ...) \
    CL_LOG_Logger::log(EN_LOG_LEVEL_WARN,  "[%s] " _fmt, LOG_::callerOrUnknown((_caller)), ##__VA_ARGS__)
#define LOG_LOGI_C(_caller, _fmt, ...) \
    CL_LOG_Logger::log(EN_LOG_LEVEL_INFO,  "[%s] " _fmt, LOG_::callerOrUnknown((_caller)), ##__VA_ARGS__)
#define LOG_LOGD_C(_caller, _fmt, ...) \
    CL_LOG_Logger::log(EN_LOG_LEVEL_DEBUG, "[%s] " _fmt, LOG_::callerOrUnknown((_caller)), ##__VA_ARGS__)

