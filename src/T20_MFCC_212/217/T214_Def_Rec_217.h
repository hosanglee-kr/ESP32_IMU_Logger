/* ============================================================================
 * File: T214_Def_Rec_217.h (T212, T216, T217 통합 포괄 헤더)
 * Summary: System Configuration Types (v217 Full)
 * ========================================================================== */
#pragma once
#include "T210_Def_Com_217.h"

// --- [1] Sensor Types ---
typedef enum { EN_T20_AXIS_ACCEL_X = 0, EN_T20_AXIS_ACCEL_Y, EN_T20_AXIS_ACCEL_Z, EN_T20_AXIS_GYRO_X, EN_T20_AXIS_GYRO_Y, EN_T20_AXIS_GYRO_Z } EM_T20_SensorAxis_t;
typedef enum { EN_T20_ACCEL_2G = 0, EN_T20_ACCEL_4G, EN_T20_ACCEL_8G, EN_T20_ACCEL_16G } EM_T20_AccelRange_t;
typedef enum { EN_T20_GYRO_125 = 0, EN_T20_GYRO_250, EN_T20_GYRO_500, EN_T20_GYRO_1000, EN_T20_GYRO_2000 } EM_T20_GyroRange_t;

typedef struct {
    EM_T20_SensorAxis_t axis;
    EM_T20_AccelRange_t accel_range;
    EM_T20_GyroRange_t  gyro_range;
} ST_T20_ConfigSensor_t;

typedef struct {
    uint32_t frame_id;
    uint16_t vector_len; 
    float    vector[T20::C10_DSP::MFCC_COEFFS_MAX * 3];
} ST_T20_FeatureVector_t;

// --- [2] DSP Preprocess Types ---
typedef enum { EN_T20_FILTER_OFF = 0, EN_T20_FILTER_LPF, EN_T20_FILTER_HPF } EM_T20_FilterType_t;
typedef enum { EN_T20_NOISE_OFF = 0, EN_T20_NOISE_FIXED, EN_T20_NOISE_ADAPTIVE } EM_T20_NoiseMode_t;

typedef struct {
    bool remove_dc;
    struct { bool enable; float alpha; } preemphasis;
    struct { bool enable; EM_T20_FilterType_t type; float cutoff_hz_1; float q_factor; } filter;
    struct { bool enable_gate; float gate_threshold_abs; EM_T20_NoiseMode_t mode; float spectral_subtract_strength; float adaptive_alpha; uint16_t noise_learn_frames; } noise;
} ST_T20_PreprocessConfig_t;

// --- [3] Network & Recorder Types ---
typedef enum { EN_T20_WIFI_STA_ONLY = 0, EN_T20_WIFI_AP_ONLY, EN_T20_WIFI_AP_STA, EN_T20_WIFI_AUTO_FALLBACK } EM_T20_WiFiMode_t;
typedef enum { EN_T20_STORAGE_LITTLEFS = 0, EN_T20_STORAGE_SDMMC } EM_T20_StorageBackend_t;

typedef struct {
    char ssid[32];
    char password[64];
} ST_T20_WiFiCredential_t;

typedef struct {
    EM_T20_WiFiMode_t mode;
    ST_T20_WiFiCredential_t multi_ap[T20::C10_Net::WIFI_MULTI_MAX];
    char ap_ssid[32];
    char ap_password[64];
    bool use_static_ip;
    char local_ip[16], gateway[16], subnet[16], dns1[16], dns2[16];
} ST_T20_ConfigWiFi_t;

typedef struct {
    char profile_name[32];
    bool use_1bit_mode;
    uint8_t clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint32_t record_count;
} ST_T20_RecorderBinaryHeader_t;

// --- [4] Master Config ---
typedef struct {
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_ConfigSensor_t     sensor;
    ST_T20_ConfigWiFi_t       wifi;
    struct { uint16_t hop_size; uint16_t mfcc_coeffs; } feature;
    struct { bool enabled; uint16_t sequence_frames; } output;
    struct { bool auto_start; uint8_t button_pin; } system;
} ST_T20_Config_t;
