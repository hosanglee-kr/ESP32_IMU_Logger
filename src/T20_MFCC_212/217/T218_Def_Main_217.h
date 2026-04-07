/* ============================================================================
 * File: T218_Def_Main_217.h
 * Summary: Default Configuration Builder (v217)
 * ========================================================================== */
#pragma once
#include "T214_Def_Rec_217.h"
#include <string.h>

static inline ST_T20_Config_t T20_makeDefaultConfig() {
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    // [1] 센서 기본값 (가속도 Z축, 8G)
    cfg.sensor.axis = EN_T20_AXIS_ACCEL_Z;
    cfg.sensor.accel_range = EN_T20_ACCEL_8G;
    cfg.sensor.gyro_range = EN_T20_GYRO_2000;

    // [2] WiFi 기본값 (AP 모드 우선)
    cfg.wifi.mode = EN_T20_WIFI_AUTO_FALLBACK;
    strlcpy(cfg.wifi.ap_ssid, "T20_v217_AP", 32);
    strlcpy(cfg.wifi.ap_password, "12345678", 64);

    // [3] DSP 기본값
    cfg.feature.hop_size = T20::C10_DSP::FFT_SIZE;
    cfg.feature.mfcc_coeffs = T20::C10_DSP::MFCC_COEFFS_DEF;

    // [4] 레코더 기본값
    cfg.recorder.enabled = true;
    cfg.recorder.rotate_keep = 8;

    return cfg;
}

