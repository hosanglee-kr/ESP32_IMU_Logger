/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. [동적 설정 관리]: 런타임에 웹 API로 변경 가능한 설정값들을 JSON으로 관리.
 * 2. [스레드 안전성]: FSM 태스크와 Web 서버 콜백이 동시 접근하므로 반드시 
 * Mutex 락(_lock)을 통해 구조체를 복사(Copy)하여 반환한다.
 * 3. [버전 호환]: ArduinoJson V7.4.x의 동적 메모리 할당(JsonDocument) 규격 준수.
 * 4. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 5. [명시적 구조체 설계]: 메모리 오염 방지 및 타입 안정성을 위해 모든 
 * 하위 설정은 익명 구조체가 아닌 명시적(Explicit) 구조체로 정의한다.
 * 6. [매직 넘버 철폐]: 버퍼 길이, AP 접속 기본값 등 하드코딩 요소를 모두 
 * T410_Config의 상수(_CONST, _DEF)로 이관하여 중앙 통제력을 확보한다.
 * 7. [부분 업데이트 방어]: 웹에서 전달된 Partial JSON을 메모리 객체에 먼저 
 * 병합(Merge)한 뒤 원자적 저장을 수행하도록 updateFromJson 메서드를 추가한다.
 * ============================================================================
 * File: T415_ConfigMgr_009.hpp
 * Summary: Dynamic JSON Configuration Manager (Singleton)
 * ========================================================================== */
#pragma once

#include "T410_Def_009.hpp" // 최신 007 설정 인클루드
#include <LittleFS.h>
#include <ArduinoJson.h> 
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
    float   ceps_targets[SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST]; 
    float   spatial_freq_min_hz; 
    float   spatial_freq_max_hz; 
	
    // [신규 추가] Top Peaks 검출용 임계치 및 간격
    float   peak_amplitude_limit_min;
    float   peak_freq_gap_limit_hz_min;
};

struct ST_Config_Decision {
    float rule_enrg_threshold;
    float rule_stddev_threshold;
    float test_ng_min_energy;
    int   min_trigger_count;
    float noise_profile_sec;
    float valid_start_sec;
    float valid_end_sec;
    float sta_lta_threshold; 
};

struct ST_Config_Storage {
    uint8_t  pre_trigger_sec;
    uint32_t rotate_mb;
    uint32_t rotate_min;
    uint32_t idle_flush_ms;
};

struct ST_Config_Mqtt {
    char mqtt_broker[SmeaConfig::NetworkLimit::MAX_BROKER_LEN_CONST];
    uint32_t retry_interval_ms;
    uint16_t default_port;
};

struct ST_Config_WiFi_AP {
    char ssid[SmeaConfig::NetworkLimit::MAX_SSID_LEN_CONST];
    char password[SmeaConfig::NetworkLimit::MAX_PW_LEN_CONST];
    bool use_static_ip;
    char local_ip[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
    char gateway[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
    char subnet[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
    char dns1[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
    char dns2[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
};

struct ST_Config_WiFi {
    uint8_t mode; 
    char ap_ssid[SmeaConfig::NetworkLimit::MAX_SSID_LEN_CONST];
    char ap_password[SmeaConfig::NetworkLimit::MAX_PW_LEN_CONST];
    char ap_ip[SmeaConfig::NetworkLimit::MAX_IP_LEN_CONST];
    ST_Config_WiFi_AP multi_ap[SmeaConfig::NetworkLimit::MAX_MULTI_AP_CONST]; 
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
    ST_Config_WiFi     wifi; 
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
    
    // 전체 구조체를 덮어쓰는 갱신
    bool updateConfig(const DynamicConfig& p_newConfig);
    
    // 부분 JSON 문자열을 수신받아 병합(Merge) 후 원자적 저장 수행
    bool updateFromJson(const char* p_jsonString);

private:
    T415_ConfigManager();
    ~T415_ConfigManager();
    
    void _loadDefaults();
    
    // JSON 문서 객체를 구조체에 매핑하는 공통 루틴
    void _applyJson(const JsonDocument& p_doc);
};

