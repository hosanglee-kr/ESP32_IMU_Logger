/* ============================================================================
 * File: T218_Def_Main_216.h
 * Summary: Main Configuration Builder (C++17 Namespace 통합 반영)
 * ========================================================================== */

#pragma once

#include <string.h>

// 새롭게 통합된 216 버전 헤더 파일들 인클루드
#include "T210_Def_Com_216.h"
#include "T212_Def_Sens_216.h"
#include "T214_Def_Rec_216.h"
#include "T216_Def_View_216.h"
#include "T217_Def_Net_216.h"

/* ============================================================================
[기본 설정 생성 함수 - C++17 Namespace 적용]
============================================================================ */

static inline ST_T20_Config_t T20_makeDefaultConfig(void) {
	ST_T20_Config_t cfg;
	memset(&cfg, 0, sizeof(cfg));

	// cfg.preprocess.axis								= EN_T20_AXIS_Z;
	cfg.preprocess.remove_dc						= true;
	cfg.preprocess.preemphasis.enable				= true;
	cfg.preprocess.preemphasis.alpha				= 0.97f;
	cfg.preprocess.filter.enable					= true;
	cfg.preprocess.filter.type						= EN_T20_FILTER_HPF;
	cfg.preprocess.filter.cutoff_hz_1				= 15.0f;
	cfg.preprocess.filter.cutoff_hz_2				= 250.0f;
	cfg.preprocess.filter.q_factor					= 0.707f;
	cfg.preprocess.noise.enable_gate				= true;
	cfg.preprocess.noise.gate_threshold_abs			= 0.002f;
	cfg.preprocess.noise.mode                       = EN_T20_NOISE_ADAPTIVE;
	cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    
    // G_T20_SYSTEM_LIMITS 래퍼 삭제에 따른 직접 상수 접근
	cfg.preprocess.noise.noise_learn_frames			= T20::C10_DSP::NOISE_MIN_FRAMES;
	
	// 적응형 노이즈 업데이트 비율 설정 (예: 5%씩 최신 스펙트럼 반영)
    cfg.preprocess.noise.adaptive_alpha             = 0.05f; 

	cfg.preprocess.pipeline.stage_count				= 4;
	cfg.preprocess.pipeline.stages[0].enable		= true;
	cfg.preprocess.pipeline.stages[0].stage_type	= EN_T20_STAGE_DC_REMOVE;
	cfg.preprocess.pipeline.stages[1].enable		= true;
	cfg.preprocess.pipeline.stages[1].stage_type	= EN_T20_STAGE_PREEMPHASIS;
	cfg.preprocess.pipeline.stages[1].param_1		= 0.97f;
	cfg.preprocess.pipeline.stages[2].enable		= true;
	cfg.preprocess.pipeline.stages[2].stage_type	= EN_T20_STAGE_GATE;
	cfg.preprocess.pipeline.stages[2].param_1		= 0.002f;
	cfg.preprocess.pipeline.stages[3].enable		= true;
	cfg.preprocess.pipeline.stages[3].stage_type	= EN_T20_STAGE_FILTER;
	cfg.preprocess.pipeline.stages[3].param_1		= 15.0f;
	cfg.preprocess.pipeline.stages[3].param_2		= 250.0f;
	cfg.preprocess.pipeline.stages[3].q_factor		= 0.707f;
	
	// 센서 초기 기본값: 가속도 Z축, 8G, 2000dps
    cfg.sensor.axis        = EN_T20_AXIS_ACCEL_Z;
    cfg.sensor.accel_range = EN_T20_ACCEL_RANGE_8G;
    cfg.sensor.gyro_range  = EN_T20_GYRO_RANGE_2000;
    
    cfg.wifi.mode = EN_T20_WIFI_AUTO_FALLBACK;
    strlcpy(cfg.wifi.ap_ssid, T20::C10_Net::AP_SSID_DEFAULT, 32);
    strlcpy(cfg.wifi.ap_password, T20::C10_Net::AP_PASS_DEFAULT, 64);
    cfg.wifi.use_static_ip = false;


    // G_T20_ 매크로들을 T20::C10_ 네임스페이스로 치환
	cfg.feature.fft_size							= T20::C10_DSP::FFT_SIZE;
	cfg.feature.frame_size							= T20::C10_DSP::FFT_SIZE;
	cfg.feature.hop_size							= T20::C10_DSP::FFT_SIZE;
	cfg.feature.sample_rate_hz						= T20::C10_DSP::SAMPLE_RATE_HZ;
	cfg.feature.mel_filters							= T20::C10_DSP::MEL_FILTERS;
	cfg.feature.mfcc_coeffs							= T20::C10_DSP::MFCC_COEFFS_DEF;
	cfg.feature.delta_window						= 2;

	cfg.output.output_mode							= EN_T20_OUTPUT_VECTOR;
	cfg.output.sequence_frames						= T20::C10_Sys::SEQUENCE_FRAMES_DEFAULT;
	cfg.output.sequence_flatten						= true;
	
	cfg.system.auto_start                           = true;
    cfg.system.button_pin                           = T20::C10_Pin::BTN_CONTROL;

	return cfg;
}


