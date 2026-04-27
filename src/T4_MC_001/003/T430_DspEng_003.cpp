
/* ============================================================================
 * File: T430_DspEng_003.cpp
 * Summary: SIMD Optimized DSP Implementation
 * ========================================================================== */
#include "T430_DspEng_003.hpp"
#include "esp_log.h"
#include "dsps_math.h"
#include "dsps_wind.h" 
#include <cmath>
#include <cstring>

static const char* G_T430_TAG = "T430_DSP";

T430_DspEngine::T430_DspEngine() {
}

T430_DspEngine::~T430_DspEngine() {

}



bool T430_DspEngine::init() {
    // 1. FIR 필터 초기화
    generateFirLpfWindowedSinc(v_firLpfCoeffs, SmeaConfig::Dsp::FIR_TAPS, SmeaConfig::Dsp::FIR_LPF_CUTOFF);

    dsps_fir_init_f32(&v_firInstLpfL, v_firLpfCoeffs, v_firStateLpfL, SmeaConfig::Dsp::FIR_TAPS);
    dsps_fir_init_f32(&v_firInstLpfR, v_firLpfCoeffs, v_firStateLpfR, SmeaConfig::Dsp::FIR_TAPS);

    /*
    // 1. FIR 필터 초기화
    generateFirLpfWindowedSinc(v_firLpfCoeffs, SmeaConfig::Dsp::FIR_TAPS, SmeaConfig::Dsp::FIR_LPF_CUTOFF);
    dsps_fir_init_f32(&v_firInstL, v_firLpfCoeffs, v_firStateLpfL, SmeaConfig::Dsp::FIR_TAPS);
    dsps_fir_init_f32(&v_firInstR, v_firLpfCoeffs, v_firStateLpfR, SmeaConfig::Dsp::FIR_TAPS);
    */
    
    // 2. Notch 필터 계수 계산
    float v_omega = 2.0f * (float)M_PI * SmeaConfig::Dsp::NOTCH_FREQ_HZ / SmeaConfig::SAMPLING_RATE;
    float v_alpha = sinf(v_omega / SmeaConfig::Dsp::NOTCH_Q_FACTOR) / 2.0f;
    float v_a0 = 1.0f + v_alpha;
    v_notchCoeffs[0] = 1.0f / v_a0;
    v_notchCoeffs[1] = (-2.0f * cosf(v_omega)) / v_a0;
    v_notchCoeffs[2] = 1.0f / v_a0;
    v_notchCoeffs[3] = (-2.0f * cosf(v_omega)) / v_a0;
    v_notchCoeffs[4] = (1.0f - v_alpha) / v_a0;
	
	// Notch 필터 2 (120Hz) 계수 계산 추가
    float v_omega2 = 2.0f * (float)M_PI * SmeaConfig::Dsp::NOTCH_FREQ_2_HZ / SmeaConfig::SAMPLING_RATE;
    float v_alpha2 = sinf(v_omega2 / SmeaConfig::Dsp::NOTCH_Q_FACTOR) / 2.0f;
    float v_a0_2 = 1.0f + v_alpha2;
    v_notch2Coeffs[0] = 1.0f / v_a0_2;
    v_notch2Coeffs[1] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    v_notch2Coeffs[2] = 1.0f / v_a0_2;
    v_notch2Coeffs[3] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    v_notch2Coeffs[4] = (1.0f - v_alpha2) / v_a0_2;

    // HPF 초기화 로직
    generateFirLpfWindowedSinc(v_firLpfCoeffs, SmeaConfig::Dsp::FIR_TAPS, SmeaConfig::Dsp::FIR_LPF_CUTOFF);
    dsps_fir_init_f32(&v_firInstLpfL, v_firLpfCoeffs, v_firStateLpfL, SmeaConfig::Dsp::FIR_TAPS); // [수정] 변수명 매핑
    dsps_fir_init_f32(&v_firInstLpfR, v_firLpfCoeffs, v_firStateLpfR, SmeaConfig::Dsp::FIR_TAPS); // [수정] 변수명 매핑

    resetFilterStates();
    return true;
}

void T430_DspEngine::resetFilterStates() {
    memset(v_wNotchL, 0, sizeof(v_wNotchL));
    memset(v_wNotchR, 0, sizeof(v_wNotchR));
    memset(v_wNotch2L, 0, sizeof(v_wNotch2L));
    memset(v_wNotch2R, 0, sizeof(v_wNotch2R));
    memset(v_firStateLpfL, 0, sizeof(v_firStateLpfL)); // [수정] 변수명 매핑
    memset(v_firStateLpfR, 0, sizeof(v_firStateLpfR)); // [수정] 변수명 매핑

    // HPF 딜레이 라인 소거 로직
    memset(v_firStateHpfL, 0, sizeof(v_firStateHpfL));
    memset(v_firStateHpfR, 0, sizeof(v_firStateHpfR));

    ESP_LOGI(G_T430_TAG, "DSP states reset.");
}


void T430_DspEngine::process(float* p_micL, float* p_micR, float* p_output, uint32_t p_len) {
    if (p_len > SmeaConfig::FFT_SIZE) return;

    // [Step 1] Median Filter (Impulse Noise 차단)
    applyMedianFilter(p_micL, SmeaConfig::Dsp::MEDIAN_WINDOW, p_len);
    applyMedianFilter(p_micR, SmeaConfig::Dsp::MEDIAN_WINDOW, p_len);

    // [Step 2] DC Removal (NaN 비트마스크 방어 포함)
    removeDC(p_micL, p_len);
    removeDC(p_micR, p_len);


    // [Step 3] 핑퐁 교차 버퍼를 활용한 엄격한 파이프라인 (HPF -> LPF -> Notch 60Hz -> Notch 120Hz)
    // L 채널 연산 파이프라인: p_micL로 시작하여 p_micL로 되돌아옴
    dsps_fir_f32(&v_firInstHpfL, p_micL, v_workBufA, p_len);      // HPF: p_micL -> v_workBufA
    dsps_fir_f32(&v_firInstLpfL, v_workBufA, v_workBufB, p_len);  // LPF: v_workBufA -> v_workBufB
	dsps_biquad_f32(v_workBufB, v_workBufA, p_len, v_notchCoeffs, v_wNotchL); 
    dsps_biquad_f32(v_workBufA, p_micL, p_len, v_notch2Coeffs, v_wNotch2L); // [패치 2] 120Hz 직렬 연결
    // dsps_biquad_f32(v_workBufB, p_micL, p_len, v_notchCoeffs, v_wNotchL); // Notch: v_workBufB -> p_micL

    // R 채널 연산 파이프라인: p_micR로 시작하여 p_micR로 되돌아옴
    dsps_fir_f32(&v_firInstHpfR, p_micR, v_workBufA, p_len);      
    dsps_fir_f32(&v_firInstLpfR, v_workBufA, v_workBufB, p_len);  
    dsps_biquad_f32(v_workBufB, v_workBufA, p_len, v_notchCoeffs, v_wNotchR); 
    dsps_biquad_f32(v_workBufA, p_micR, p_len, v_notch2Coeffs, v_wNotch2R); // [패치 2] 120Hz 직렬 연결
    // dsps_biquad_f32(v_workBufB, p_micR, p_len, v_notchCoeffs, v_wNotchR); 


    // [Step 4] Noise Gate (무음구간 정숙성 확보)
    applyNoiseGate(p_micL, p_len);
    applyNoiseGate(p_micR, p_len);

    // [Step 5] Pre-Emphasis (오버랩 경계 이월 파열음 방지를 위한 역순 루프 적용)
    applyPreEmphasis(p_micL, p_len);
    applyPreEmphasis(p_micR, p_len);

    // [Step 6] Broadside Beamforming (L + R 채널 합성 및 증폭)
    applyBeamforming(p_micL, p_micR, p_output, p_len);
}


void T430_DspEngine::removeDC(float* p_data, uint32_t p_len) {
    float v_sum = 0.0f;
    for (uint32_t i = 0; i < p_len; i++) {
        // [방어] IEEE-754 마스킹으로 NaN/Inf의 시스템 마비 전파 차단
        uint32_t v_bits;
        memcpy(&v_bits, &p_data[i], sizeof(uint32_t));
        if ((v_bits & 0x7F800000) == 0x7F800000) p_data[i] = 0.0f;
        v_sum += p_data[i];
    }
    float v_mean = v_sum / (float)p_len;
    for (uint32_t i = 0; i < p_len; i++) p_data[i] -= v_mean;
}

void T430_DspEngine::applyPreEmphasis(float* p_data, uint32_t p_len) {
    for (int i = p_len - 1; i > 0; i--) {
        p_data[i] = p_data[i] - SmeaConfig::Dsp::PRE_EMPHASIS_ALPHA * p_data[i - 1];
    }
}

void T430_DspEngine::applyMedianFilter(float* p_data, int p_windowSize, uint32_t p_len) {
    if (p_windowSize < 3) return;
    int v_half = p_windowSize / 2;
    float v_tempBuf[7];
    float v_origHistory[3]; 

    for (int i = 0; i < v_half; i++) v_origHistory[i] = p_data[i]; 

    for (uint32_t i = 0; i < p_len; i++) {
        for (int j = 0; j < p_windowSize; j++) {
            int v_idx = i + j - v_half;
            if (v_idx < 0) v_tempBuf[j] = p_data[0];
            else if (v_idx < i) v_tempBuf[j] = v_origHistory[v_idx % v_half]; // 원본 캐시 참조
            else if (v_idx >= p_len) v_tempBuf[j] = p_data[p_len - 1];
            else v_tempBuf[j] = p_data[v_idx];
        }
        
        if (i < p_len) v_origHistory[i % v_half] = p_data[i]; // 원본 백업
        
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

void T430_DspEngine::applyNoiseGate(float* p_data, uint32_t p_len) {
    for (uint32_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < SmeaConfig::Dsp::NOISE_GATE_THRESH) p_data[i] = 0.0f;
    }
}

void T430_DspEngine::applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len) {
    // 1. L과 R 채널 배열을 더하여 p_out에 저장 
    // 파라미터: (입력1, 입력2, 출력, 처리길이, 입력1_간격, 입력2_간격, 출력_간격)
    dsps_add_f32(p_L, p_R, p_out, p_len, 1, 1, 1);

    // 2. 합쳐진 p_out 배열에 BEAMFORMING_GAIN(0.5f) 상수를 곱하여 제자리(In-place) 저장
    // 파라미터: (입력, 출력, 처리길이, 곱할상수, 입력_간격, 출력_간격)
    dsps_mulc_f32(p_out, p_out, p_len, SmeaConfig::Dsp::BEAMFORMING_GAIN, 1, 1);
}

void T430_DspEngine::generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    float v_normalizedCutoff = p_cutoffHz / SmeaConfig::SAMPLING_RATE;
    alignas(16) float v_win[128] = {0}; // 스택 OOM 차단

    dsps_wind_blackman_f32(v_win, p_taps);
    float v_M = (float)(p_taps - 1);
    float v_sum = 0.0f;
    
    for (int i = 0; i < p_taps; i++) {
        float v_x = (float)i - (v_M / 2.0f);
        // Epsilon 비교로 Sinc 분모 0나누기 차단
        if (fabsf(v_x) < 1e-6f) p_coeffs[i] = 2.0f * v_normalizedCutoff; 
        else p_coeffs[i] = sinf(2.0f * (float)M_PI * v_normalizedCutoff * v_x) / ((float)M_PI * v_x);
        p_coeffs[i] *= v_win[i]; 
        v_sum += p_coeffs[i];
    }
    for (int i = 0; i < p_taps; i++) p_coeffs[i] /= v_sum;
}


// HPF 계수 생성 구현부  (LPF를 반전하여 생성하는 스펙트럴 인버전 방식)
void T430_DspEngine::generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    generateFirLpfWindowedSinc(p_coeffs, p_taps, p_cutoffHz);
    uint16_t v_center = (p_taps - 1) / 2;
    for (int i = 0; i < p_taps; i++) {
        p_coeffs[i] = -p_coeffs[i];
    }
    p_coeffs[v_center] += 1.0f;
}
