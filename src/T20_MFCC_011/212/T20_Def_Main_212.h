

/* ============================================================================
 * File: T20_Def_Main_211.h

 * ========================================================================== */

#pragma once

#include "T20_Def_Comm_022.h"
#include "T20_Def_Recorder_022.h"
#include "T20_Def_SensDsp_022.h"
#include "T20_Def_Viewer_022.h"
#include "T20_Def_Web_212.h"

/* ============================================================================
[v210 기본 설정 생성 함수]
============================================================================ */

static inline ST_T20_Config_t T20_makeDefaultConfig(void) {
	ST_T20_Config_t cfg;
	memset(&cfg, 0, sizeof(cfg));

	cfg.preprocess.axis								= EN_T20_AXIS_Z;
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
	cfg.preprocess.noise.enable_spectral_subtract	= true;
	cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
	cfg.preprocess.noise.noise_learn_frames			= G_T20_NOISE_MIN_FRAMES;

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

	cfg.feature.fft_size							= G_T20_FFT_SIZE;
	cfg.feature.frame_size							= G_T20_FFT_SIZE;
	cfg.feature.hop_size							= G_T20_FFT_SIZE;
	cfg.feature.sample_rate_hz						= G_T20_SAMPLE_RATE_HZ;
	cfg.feature.mel_filters							= G_T20_MEL_FILTERS;
	cfg.feature.mfcc_coeffs							= G_T20_MFCC_COEFFS_DEFAULT;
	cfg.feature.delta_window						= 2;

	cfg.output.output_mode							= EN_T20_OUTPUT_VECTOR;
	cfg.output.sequence_frames						= G_T20_SEQUENCE_FRAMES_DEFAULT;
	cfg.output.sequence_flatten						= true;

	return cfg;
}
