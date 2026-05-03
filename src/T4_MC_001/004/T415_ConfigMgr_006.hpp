
// ################################################
/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. [동적 설정 관리]: 런타임에 웹 API로 변경 가능한 설정값들을 JSON으로 관리.
 * 2. [스레드 안전성]: FSM 태스크와 Web 서버 콜백이 동시 접근하므로 반드시 
 * Mutex 락(_lock)을 통해 구조체를 복사(Copy)하여 반환한다.
 * 3. [버전 호환]: ArduinoJson V7.4.x의 동적 메모리 할당(JsonDocument) 규격 준수.
 * 4. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 5. [명시적 구조체 설계]: 메모리 오염 방지 및 타입 안정성을 위해 모든 
 * 하위 설정은 익명 구조체가 아닌 명시적(Explicit) 구조체로 정의한다.
 * 6. [WiFi 확장]: T20 프로젝트의 다중 AP 순회 및 Auto-Fallback 설정을 수용한다.
 * ============================================================================
 * File: T415_ConfigMgr_006.hpp
 * Summary: Dynamic JSON Configuration Manager (Singleton)
 * ========================================================================== */
#pragma once

#include "T410_Config_006.hpp"
#include <LittleFS.h>
#include <ArduinoJson.h> // V7.4.x 사용
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstring>

// ============================================================================
// 명시적 하위 설정 구조체 정의
// ============================================================================

struct ST_Config_Dsp {
    float window_ms;
    float hop_ms;
    float notch_freq_hz;
    float notch_freq_2_hz;
    float notch_q_factor;
    float pre_emphasis_alpha;
    float beamforming_gain;
    float fir_lpf_cutoff;
    float fir_hpf_cutoff;
    int   median_window;
    float noise_gate_thresh;
    int   noise_learn_frames;
    float spectral_sub_gain;
};

struct ST_Config_Feature {
    uint8_t band_rms_count;
    float   band_ranges[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST][2];
};

struct ST_Config_Decision {
    float rule_enrg_threshold;
    float rule_stddev_threshold;
    float test_ng_min_energy;
    int   min_trigger_count;
    float noise_profile_sec;
    float valid_start_sec;
    float valid_end_sec;
};

struct ST_Config_Storage {
    uint8_t  pre_trigger_sec;
    uint32_t rotate_mb;
    uint32_t rotate_min;
    uint32_t idle_flush_ms;
};

struct ST_Config_Mqtt {
    char mqtt_broker[64];
    uint32_t retry_interval_ms;
    uint16_t default_port;
};

// [추가] 외부 공유기 접속(STA) 설정 단위 구조체
struct ST_Config_WiFi_AP {
    char ssid[32];
    char password[64];
    bool use_static_ip;
    char local_ip[16];
    char gateway[16];
    char subnet[16];
    char dns1[16];
    char dns2[16];
};

// [추가] WiFi 최상위 설정 구조체
struct ST_Config_WiFi {
    uint8_t mode; // 0: AP_ONLY, 1: STA, 2: AP_STA, 3: AUTO_FALLBACK
    char ap_ssid[32];
    char ap_password[64];
    char ap_ip[16];
    ST_Config_WiFi_AP multi_ap[3]; // 최대 3개의 공유기 다중 접속 지원
};

// ============================================================================
// 최상위 런타임 동적 설정 구조체
// ============================================================================
struct DynamicConfig {
    ST_Config_Dsp      dsp;
    ST_Config_Feature  feature;
    ST_Config_Decision decision;
    ST_Config_Storage  storage;
    ST_Config_Mqtt     mqtt;
    ST_Config_WiFi     wifi;  // [추가] WiFi 설정 연동
};

class T415_ConfigManager {
private:
    DynamicConfig     _config;
    SemaphoreHandle_t _lock;
    bool              _isLoaded;

public:
    static T415_ConfigManager& getInstance() {
        static T415_ConfigManager v_instance;
        return v_instance;
    }

    bool init();
    bool load();
    bool save();
    void resetToDefault();
    DynamicConfig getConfig();
    bool updateConfig(const DynamicConfig& p_newConfig);

private:
    T415_ConfigManager();
    ~T415_ConfigManager();
    void _loadDefaults();
};
