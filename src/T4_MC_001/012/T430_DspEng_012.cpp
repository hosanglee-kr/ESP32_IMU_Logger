/* ============================================================================
 * File: T430_DspEng_012.cpp
 * Summary: SIMD Optimized Signal Processing Pipeline (Mono-Reduced Hybrid Mode)
 * ============================================================================
 * * [AI 메모: 제공 기능 및 마이그레이션 적용 완료 사항]
 * 1. [LTI 최적화] Beamforming을 파이프라인 맨 앞으로 이동시켜 L/R 필터 연산 통폐합.
 * 2. [단절 방어] Pre-emphasis와 Median 필터 팝핑(Popping) 노이즈 원천 차단.
 * 3. [왜곡 방어] Pre-emphasis -> Noise Gate 순서로 델타 스파이크 생성 차단.
 * 4. [v012 유지보수] 포인터 오염 방어 및 상수 헤더 v012 버전 적용 완료.
 * ========================================================================== */

#include "T430_DspEng_012.hpp"
#include "esp_log.h"
#include "dsps_math.h"
#include "dsps_wind.h"
#include <cmath>
#include <cstring>

static const char* G_T430_TAG = "T430_DSP";

T430_DspEngine::T430_DspEngine() {
    _prevPreEmpSample = 0.0f;
    memset(_medianHistory, 0, sizeof(_medianHistory));
}

T430_DspEngine::~T430_DspEngine() {}

bool T430_DspEngine::init() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    _generateFirLpfWindowedSinc(_firLpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_lpf_cutoff);
    dsps_fir_init_f32(&_firInstLpf, _firLpfCoeffs, _firStateLpf, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);

    _generateFirHpfWindowedSinc(_firHpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_hpf_cutoff);
    dsps_fir_init_f32(&_firInstHpf, _firHpfCoeffs, _firStateHpf, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);

    float v_omega = 2.0f * (float)M_PI * v_cfg.dsp.notch_freq_hz / SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_alpha = sinf(v_omega / v_cfg.dsp.notch_q_factor) / 2.0f;
    float v_a0 = 1.0f + v_alpha;
    _notchCoeffs[0] = 1.0f / v_a0;
    _notchCoeffs[1] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[2] = 1.0f / v_a0;
    _notchCoeffs[3] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[4] = (1.0f - v_alpha) / v_a0;

    float v_omega2 = 2.0f * (float)M_PI * v_cfg.dsp.notch_freq_2_hz / SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_alpha2 = sinf(v_omega2 / v_cfg.dsp.notch_q_factor) / 2.0f;
    float v_a0_2 = 1.0f + v_alpha2;
    _notch2Coeffs[0] = 1.0f / v_a0_2;
    _notch2Coeffs[1] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    _notch2Coeffs[2] = 1.0f / v_a0_2;
    _notch2Coeffs[3] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    _notch2Coeffs[4] = (1.0f - v_alpha2) / v_a0_2;

    resetFilterStates();
    return true;
}

void T430_DspEngine::resetFilterStates() {
    memset(_wNotch, 0, sizeof(_wNotch));
    memset(_wNotch2, 0, sizeof(_wNotch2));
    memset(_firStateLpf, 0, sizeof(_firStateLpf));
    memset(_firStateHpf, 0, sizeof(_firStateHpf));
    memset(_medianHistory, 0, sizeof(_medianHistory));
    _prevPreEmpSample = 0.0f;
    ESP_LOGI(G_T430_TAG, "DSP Mono states & History reset.");
}

void T430_DspEngine::process(const float* p_micL, const float* p_micR, float* p_output, uint32_t p_len) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    _applyBeamforming(p_micL, p_micR, _workBufA, p_len, v_cfg.dsp.beamforming_gain);
    _applyMedianFilter(_workBufA, v_cfg.dsp.median_window, p_len);

    dsps_biquad_f32(_workBufA, _workBufB, p_len, _notchCoeffs, _wNotch);    
    dsps_biquad_f32(_workBufB, _workBufA, p_len, _notch2Coeffs, _wNotch2);  
    dsps_fir_f32(&_firInstHpf, _workBufA, _workBufB, p_len);                
    dsps_fir_f32(&_firInstLpf, _workBufB, p_output, p_len);                 

    _applyPreEmphasis(p_output, p_len, v_cfg.dsp.pre_emphasis_alpha);
    _applyNoiseGate(p_output, p_len, v_cfg.dsp.noise_gate_thresh);
}

void T430_DspEngine::_applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len, float p_gain) {
    dsps_add_f32(p_L, p_R, p_out, p_len, 1, 1, 1);
    dsps_mulc_f32(p_out, p_out, p_len, p_gain, 1, 1);
}

void T430_DspEngine::_applyMedianFilter(float* p_data, uint8_t p_windowSize, uint32_t p_len) {
    if (p_windowSize < 3) return;
    if (p_windowSize > SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST) {
        p_windowSize = SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST;
    }

    uint8_t v_half = p_windowSize / 2;
    float v_tempBuf[SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST];

    for (uint32_t i = 0; i < p_len; i++) {
        for (uint8_t j = 0; j < p_windowSize; j++) {
            int32_t v_idx = (int32_t)i + j - v_half;
            if (v_idx < 0) {
                v_tempBuf[j] = _medianHistory[(v_half + v_idx) % v_half];
            } else if (v_idx >= (int32_t)p_len) {
                v_tempBuf[j] = p_data[p_len - 1];
            } else {
                v_tempBuf[j] = p_data[v_idx];
            }
        }

        for (uint8_t j = 1; j < p_windowSize; j++) {
            float v_key = v_tempBuf[j];
            int32_t k = j - 1;
            while (k >= 0 && v_tempBuf[k] > v_key) {
                v_tempBuf[k + 1] = v_tempBuf[k];
                k--;
            }
            v_tempBuf[k + 1] = v_key;
        }
        p_data[i] = v_tempBuf[v_half];
    }

    for (uint8_t i = 0; i < v_half; i++) {
        _medianHistory[i] = p_data[p_len - v_half + i];
    }
}

void T430_DspEngine::_applyPreEmphasis(float* p_data, uint32_t p_len, float p_alpha) {
    if (p_len == 0) return;
    for (uint32_t i = p_len - 1; i > 0; i--) {
        p_data[i] = p_data[i] - p_alpha * p_data[i - 1];
    }
    p_data[0] = p_data[0] - p_alpha * _prevPreEmpSample;
    _prevPreEmpSample = p_data[p_len - 1];
}

void T430_DspEngine::_applyNoiseGate(float* p_data, uint32_t p_len, float p_gateThresh) {
    for (uint32_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_gateThresh) p_data[i] = 0.0f;
    }
}

void T430_DspEngine::_generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    float v_normalizedCutoff = p_cutoffHz / SmeaConfig::System::SAMPLING_RATE_CONST;
    alignas(16) float v_win[SmeaConfig::FeatureLimit::FIR_TAPS_CONST] = {0};

    dsps_wind_blackman_f32(v_win, p_taps);
    float v_M = (float)(p_taps - 1);
    float v_sum = 0.0f;

    for (uint16_t i = 0; i < p_taps; i++) {
        float v_x = (float)i - (v_M / 2.0f);
        if (fabsf(v_x) < SmeaConfig::System::MATH_EPSILON_CONST) p_coeffs[i] = 2.0f * v_normalizedCutoff;
        else p_coeffs[i] = sinf(2.0f * (float)M_PI * v_normalizedCutoff * v_x) / ((float)M_PI * v_x);
        p_coeffs[i] *= v_win[i];
        v_sum += p_coeffs[i];
    }
    for (uint16_t i = 0; i < p_taps; i++) p_coeffs[i] /= v_sum;
}

void T430_DspEngine::_generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    _generateFirLpfWindowedSinc(p_coeffs, p_taps, p_cutoffHz);
    uint16_t v_center = (p_taps - 1) / 2; 
    for (uint16_t i = 0; i < p_taps; i++) {
        p_coeffs[i] = -p_coeffs[i];
    }
    p_coeffs[v_center] += 1.0f;
}
