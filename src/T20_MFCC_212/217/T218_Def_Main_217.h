/* ============================================================================
 * File: T218_Def_Main_217.h
 * Summary: Main Configuration Builder (v217 Full)
 * ========================================================================== */
#pragma once
#include "T214_Def_Rec_217.h"
#include <string.h>

static inline ST_T20_Config_t T20_makeDefaultConfig() {
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.preprocess.remove_dc = true;
    cfg.preprocess.preemphasis.enable = true;
    cfg.preprocess.preemphasis.alpha = 0.97f;
    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    cfg.preprocess.filter.q_factor = 0.707f;
    
    cfg.preprocess.noise.enable_gate = true;
    cfg.preprocess.noise.gate_threshold_abs = 0.002f;
    cfg.preprocess.noise.mode = EN_T20_NOISE_ADAPTIVE;
    cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    cfg.preprocess.noise.adaptive_alpha = 0.05f;
    cfg.preprocess.noise.noise_learn_frames = 8U;

    cfg.sensor.axis = EN_T20_AXIS_ACCEL_Z;
    cfg.sensor.accel_range = EN_T20_ACCEL_8G;
    cfg.sensor.gyro_range = EN_T20_GYRO_2000;
    
    cfg.wifi.mode = EN_T20_WIFI_AUTO_FALLBACK;
    strlcpy(cfg.wifi.ap_ssid, "T20_MFCC_AP", 32);
    strlcpy(cfg.wifi.ap_password, "12345678", 64);

    cfg.feature.hop_size = T20::C10_DSP::FFT_SIZE;
    cfg.feature.mfcc_coeffs = T20::C10_DSP::MFCC_COEFFS_DEF;
    
    cfg.output.sequence_frames = T20::C10_Sys::SEQUENCE_FRAMES_MAX;
    cfg.output.enabled = true;
    
    cfg.system.auto_start = true;
    cfg.system.button_pin = T20::C10_Pin::BTN_CONTROL;

    return cfg;
}
