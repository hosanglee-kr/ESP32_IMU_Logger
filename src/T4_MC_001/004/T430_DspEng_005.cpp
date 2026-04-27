/* ============================================================================
 * File: T430_DspEng_005.cpp
 * Summary: SIMD Optimized DSP Implementation (Dynamic Config Applied)
 * ========================================================================== */
#include "T430_DspEng_005.hpp"
#include "esp_log.h"
#include "dsps_math.h"
#include "dsps_wind.h" 
#include <cmath>
#include <cstring>

static const char* G_T430_TAG = "T430_DSP";

T430_DspEngine::T430_DspEngine() {}
T430_DspEngine::~T430_DspEngine() {}

bool T430_DspEngine::init() {
    // 런타임 설정 가져오기 (초기화 시점에 최신 값 사용)
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // 1. FIR LPF 필터 초기화
    _generateFirLpfWindowedSinc(_firLpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_lpf_cutoff);
    dsps_fir_init_f32(&_firInstLpfL, _firLpfCoeffs, _firStateLpfL, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);
    dsps_fir_init_f32(&_firInstLpfR, _firLpfCoeffs, _firStateLpfR, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);

    // HPF 초기화
    _generateFirHpfWindowedSinc(_firHpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_hpf_cutoff);
    dsps_fir_init_f32(&_firInstHpfL, _firHpfCoeffs, _firStateHpfL, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);
    dsps_fir_init_f32(&_firInstHpfR, _firHpfCoeffs, _firStateHpfR, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);
    
    // 2. Notch 필터 계수 계산 (60Hz)
    float v_omega = 2.0f * (float)M_PI * v_cfg.dsp.notch_freq_hz / SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_alpha = sinf(v_omega / v_cfg.dsp.notch_q_factor) / 2.0f;
    float v_a0 = 1.0f + v_alpha;
    _notchCoeffs[0] = 1.0f / v_a0;
    _notchCoeffs[1] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[2] = 1.0f / v_a0;
    _notchCoeffs[3] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[4] = (1.0f - v_alpha) / v_a0;
    
    // 3. Notch 필터 2 (120Hz) 계수 계산 추가
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
    memset(_wNotchL, 0, sizeof(_wNotchL));
    memset(_wNotchR, 0, sizeof(_wNotchR));
    memset(_wNotch2L, 0, sizeof(_wNotch2L));
    memset(_wNotch2R, 0, sizeof(_wNotch2R));
    memset(_firStateLpfL, 0, sizeof(_firStateLpfL));
    memset(_firStateLpfR, 0, sizeof(_firStateLpfR)); 

    memset(_firStateHpfL, 0, sizeof(_firStateHpfL));
    memset(_firStateHpfR, 0, sizeof(_firStateHpfR));

    ESP_LOGI(G_T430_TAG, "DSP states reset.");
}

void T430_DspEngine::process(float* p_micL, float* p_micR, float* p_output, uint32_t p_len) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;

    // 매 사이클마다 최신 설정을 참조
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // [Step 1] Median Filter
    _applyMedianFilter(p_micL, v_cfg.dsp.median_window, p_len);
    _applyMedianFilter(p_micR, v_cfg.dsp.median_window, p_len);

    // [Step 2] DC Removal
    _removeDC(p_micL, p_len);
    _removeDC(p_micR, p_len);

    // [Step 3] 핑퐁 교차 버퍼를 활용한 엄격한 파이프라인 (HPF -> LPF -> Notch 60Hz -> Notch 120Hz)
    // L 채널
    dsps_fir_f32(&_firInstHpfL, p_micL, _workBufA, p_len);
    dsps_fir_f32(&_firInstLpfL, _workBufA, _workBufB, p_len);
    dsps_biquad_f32(_workBufB, _workBufA, p_len, _notchCoeffs, _wNotchL); 
    dsps_biquad_f32(_workBufA, p_micL, p_len, _notch2Coeffs, _wNotch2L); 

    // R 채널
    dsps_fir_f32(&_firInstHpfR, p_micR, _workBufA, p_len);      
    dsps_fir_f32(&_firInstLpfR, _workBufA, _workBufB, p_len);  
    dsps_biquad_f32(_workBufB, _workBufA, p_len, _notchCoeffs, _wNotchR); 
    dsps_biquad_f32(_workBufA, p_micR, p_len, _notch2Coeffs, _wNotch2R); 

    // [Step 4] Noise Gate (동적 임계치 사용)
    _applyNoiseGate(p_micL, p_len, v_cfg.dsp.noise_gate_thresh);
    _applyNoiseGate(p_micR, p_len, v_cfg.dsp.noise_gate_thresh);

    // [Step 5] Pre-Emphasis (동적 알파값 사용)
    _applyPreEmphasis(p_micL, p_len, v_cfg.dsp.pre_emphasis_alpha);
    _applyPreEmphasis(p_micR, p_len, v_cfg.dsp.pre_emphasis_alpha);

    // [Step 6] Broadside Beamforming (동적 게인 사용)
    _applyBeamforming(p_micL, p_micR, p_output, p_len, v_cfg.dsp.beamforming_gain);
}

void T430_DspEngine::_removeDC(float* p_data, uint32_t p_len) {
    float v_sum = 0.0f;
    for (uint32_t i = 0; i < p_len; i++) {
        uint32_t v_bits;
        memcpy(&v_bits, &p_data[i], sizeof(uint32_t));
        if ((v_bits & 0x7F800000) == 0x7F800000) p_data[i] = 0.0f;
        v_sum += p_data[i];
    }
    float v_mean = v_sum / (float)p_len;
    for (uint32_t i = 0; i < p_len; i++) p_data[i] -= v_mean;
}

void T430_DspEngine::_applyPreEmphasis(float* p_data, uint32_t p_len, float p_alpha) {
    for (int i = p_len - 1; i > 0; i--) {
        p_data[i] = p_data[i] - p_alpha * p_data[i - 1];
    }
}

void T430_DspEngine::_applyMedianFilter(float* p_data, int p_windowSize, uint32_t p_len) {
    if (p_windowSize < 3) return;
    int v_half = p_windowSize / 2;
    float v_tempBuf[7];
    float v_origHistory[3]; 

    for (int i = 0; i < v_half; i++) v_origHistory[i] = p_data[i]; 

    for (uint32_t i = 0; i < p_len; i++) {
        for (int j = 0; j < p_windowSize; j++) {
            int v_idx = i + j - v_half;
            if (v_idx < 0) v_tempBuf[j] = p_data[0];
            else if (v_idx < i) v_tempBuf[j] = v_origHistory[v_idx % v_half]; 
            else if (v_idx >= p_len) v_tempBuf[j] = p_data[p_len - 1];
            else v_tempBuf[j] = p_data[v_idx];
        }
        
        if (i < p_len) v_origHistory[i % v_half] = p_data[i];
        
        for (int j = 1; j < p_windowSize; j++) {
            float v_key = v_tempBuf[j];
            int k = j - 1;
            while (k >= 0 && v_tempBuf[k] > v_key) {
                v_tempBuf[k + 1] = v_tempBuf[k];
                k--;
            }
            v_tempBuf[k + 1] = v_key;
        }
        p_data[i] = v_tempBuf[v_half];
    }
}

void T430_DspEngine::_applyNoiseGate(float* p_data, uint32_t p_len, float p_gateThresh) {
    for (uint32_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_gateThresh) p_data[i] = 0.0f;
    }
}

void T430_DspEngine::_applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len, float p_gain) {
    dsps_add_f32(p_L, p_R, p_out, p_len, 1, 1, 1);
    dsps_mulc_f32(p_out, p_out, p_len, p_gain, 1, 1);
}

void T430_DspEngine::_generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    float v_normalizedCutoff = p_cutoffHz / SmeaConfig::System::SAMPLING_RATE_CONST;
    alignas(16) float v_win[128] = {0}; 

    dsps_wind_blackman_f32(v_win, p_taps);
    float v_M = (float)(p_taps - 1);
    float v_sum = 0.0f;
    
    for (int i = 0; i < p_taps; i++) {
        float v_x = (float)i - (v_M / 2.0f);
        if (fabsf(v_x) < 1e-6f) p_coeffs[i] = 2.0f * v_normalizedCutoff; 
        else p_coeffs[i] = sinf(2.0f * (float)M_PI * v_normalizedCutoff * v_x) / ((float)M_PI * v_x);
        p_coeffs[i] *= v_win[i]; 
        v_sum += p_coeffs[i];
    }
    for (int i = 0; i < p_taps; i++) p_coeffs[i] /= v_sum;
}

void T430_DspEngine::_generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    _generateFirLpfWindowedSinc(p_coeffs, p_taps, p_cutoffHz);
    uint16_t v_center = (p_taps - 1) / 2;
    for (int i = 0; i < p_taps; i++) {
        p_coeffs[i] = -p_coeffs[i];
    }
    p_coeffs[v_center] += 1.0f;
}

