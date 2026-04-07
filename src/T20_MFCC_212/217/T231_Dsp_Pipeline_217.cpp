/* ============================================================================
 * File: T231_Dsp_Pipeline_217.cpp
 * Summary: MFCC & DSP Pipeline Engine Implementation (v217)
 * Compiler: gnu++17 / ESP32-S3 SIMD Optimized
 * ========================================================================== */

#include "T231_Dsp_Pipeline_217.h"
#include <esp_dsp.h>
#include <math.h>
#include <string.h>

CL_T20_DspPipeline::CL_T20_DspPipeline() {
    // 버퍼 초기화 (정렬된 메모리 안전 확보)
    memset(_mfcc_history, 0, sizeof(_mfcc_history));
    memset(_noise_spectrum, 0, sizeof(_noise_spectrum));
    memset(_biquad_state, 0, sizeof(_biquad_state));
    
    // ESP-DSP 초기화 (정적 플래그 등을 통해 시스템 전체에서 한 번만 수행되도록 권장)
    static bool dsp_lib_init = false;
    if (!dsp_lib_init) {
        esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        if (err == ESP_OK) dsp_lib_init = true;
    }
}

bool CL_T20_DspPipeline::begin(const ST_T20_Config_t& cfg) {
    _cfg = cfg;
    _history_count = 0;
    _noise_learned_frames = 0;
    _prev_sample = 0.0f;

    const uint16_t N = T20::C10_DSP::FFT_SIZE;
    const uint16_t bins = T20::C10_DSP::FFT_BINS;
    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;

    // [1] Hamming Window 프리컴퓨팅
    for (int i = 0; i < N; i++) {
        _window[i] = 0.54f - 0.46f * cosf(i * 2.0f * M_PI / (float)(N - 1));
    }

    // [2] Mel Filterbank 매트릭스 생성 (Triangular Filters)
    float min_mel = 2595.0f * log10f(1.0f + (0.0f / 700.0f));
    float max_mel = 2595.0f * log10f(1.0f + ((fs / 2.0f) / 700.0f));
    float mel_step = (max_mel - min_mel) / (float)(T20::C10_DSP::MEL_FILTERS + 1);

    uint16_t bin_points[T20::C10_DSP::MEL_FILTERS + 2];
    for (int i = 0; i < T20::C10_DSP::MEL_FILTERS + 2; i++) {
        float hz = 700.0f * (powf(10.0f, (min_mel + i * mel_step) / 2595.0f) - 1.0f);
        bin_points[i] = (uint16_t)roundf(N * hz / fs);
    }

    memset(_mel_bank, 0, sizeof(_mel_bank));
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        uint16_t left = bin_points[m], center = bin_points[m+1], right = bin_points[m+2];
        for (int k = left; k < center && k < bins; k++) 
            _mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        for (int k = center; k < right && k < bins; k++) 
            _mel_bank[m][k] = (float)(right - k) / (float)(right - center + 1e-12f);
    }

    // [3] DCT-II Matrix 프리컴퓨팅 (v217 효율화 핵심)
    for (int n = 0; n < T20::C10_DSP::MFCC_COEFFS_MAX; n++) {
        for (int k = 0; k < T20::C10_DSP::MEL_FILTERS; k++) {
            _dct_matrix[n][k] = cosf((M_PI / (float)T20::C10_DSP::MEL_FILTERS) * (k + 0.5f) * n);
        }
    }

    // [4] IIR Biquad 필터 설계 (HPF 기준)
    float f_norm = _cfg.sensor.accel_range > 0 ? 15.0f / fs : 1.0f / fs; // 예시 조건부 설정
    dsps_biquad_gen_hpf_f32(_biquad_coeffs, f_norm, 0.707f);

    return true;
}

bool CL_T20_DspPipeline::processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out) {
    if (!p_time_in || !p_vec_out) return false;

    // 1. 데이터 복사 및 전처리 (DC 제거 & Pre-emphasis)
    memcpy(_work_frame, p_time_in, sizeof(float) * T20::C10_DSP::FFT_SIZE);
    _applyPreprocess(_work_frame);

    // 2. 런타임 IIR 필터 적용 (SIMD 가속)
    dsps_biquad_f32(_work_frame, _work_frame, T20::C10_DSP::FFT_SIZE, _biquad_coeffs, _biquad_state);

    // 3. 윈도우 함수 적용 (SIMD 가속 벡터 곱셈)
    dsps_mul_f32(_work_frame, _window, _work_frame, T20::C10_DSP::FFT_SIZE, 1, 1, 1);

    // 4. Power Spectrum 계산 (Complex FFT -> Magnitude Squared)
    _computePowerSpectrum(_work_frame);

    // 5. 노이즈 제거 (Spectral Subtraction)
    if (_noise_learning_active) {
        float alpha = 0.05f;
        for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
            _noise_spectrum[i] = (1.0f - alpha) * _noise_spectrum[i] + alpha * _power[i];
        }
    }
    for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
        _power[i] = fmaxf(_power[i] - _noise_spectrum[i], 1e-12f);
    }

    // 6. Mel Filterbank 합산 (SIMD Dot Product 가속)
    alignas(16) float current_mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    _applyMelFilterbank(_log_mel);

    // 7. DCT-II 변환 (특징 벡터 압축)
    _computeDCT2(_log_mel, current_mfcc);

    // 8. 히스토리 업데이트 및 39차 특징 벡터 조립 (MFCC + Delta + Delta-Delta)
    _pushHistory(current_mfcc);
    if (_history_count >= T20::C10_DSP::MFCC_HISTORY_LEN) {
        _build39DVector(p_vec_out);
        return true;
    }

    return false;
}

void CL_T20_DspPipeline::_applyPreprocess(float* p_data) {
    // DC 제거
    float sum = 0.0f;
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) sum += p_data[i];
    float mean = sum / (float)T20::C10_DSP::FFT_SIZE;
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) p_data[i] -= mean;

    // Pre-emphasis (고주파 성분 강조)
    float alpha = 0.97f;
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) {
        float cur = p_data[i];
        p_data[i] = cur - (alpha * _prev_sample);
        _prev_sample = cur;
    }
}

void CL_T20_DspPipeline::_computePowerSpectrum(const float* p_time) {
    alignas(16) static float fft_io[T20::C10_DSP::FFT_SIZE * 2];
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) {
        fft_io[i * 2] = p_time[i];
        fft_io[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(fft_io, T20::C10_DSP::FFT_SIZE);
    dsps_bit_rev2r_fc32(fft_io, T20::C10_DSP::FFT_SIZE);

    for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
        float re = fft_io[i * 2], im = fft_io[i * 2 + 1];
        _power[i] = (re * re + im * im) + 1e-12f;
    }
}

void CL_T20_DspPipeline::_applyMelFilterbank(float* p_log_mel_out) {
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        float sum = 0.0f;
        dsps_dotprod_f32(_power, _mel_bank[m], &sum, T20::C10_DSP::FFT_BINS);
        p_log_mel_out[m] = logf(sum + 1e-12f);
    }
}

void CL_T20_DspPipeline::_computeDCT2(const float* p_in, float* p_out) {
    uint16_t n_mfcc = _cfg.feature.mfcc_coeffs;
    for (int i = 0; i < n_mfcc; i++) {
        float sum = 0.0f;
        dsps_dotprod_f32(p_in, _dct_matrix[i], &sum, T20::C10_DSP::MEL_FILTERS);
        p_out[i] = sum;
    }
}

void CL_T20_DspPipeline::_pushHistory(const float* p_mfcc) {
    uint16_t n_mfcc = _cfg.feature.mfcc_coeffs;
    if (_history_count < T20::C10_DSP::MFCC_HISTORY_LEN) {
        memcpy(_mfcc_history[_history_count++], p_mfcc, sizeof(float) * n_mfcc);
    } else {
        memmove(_mfcc_history[0], _mfcc_history[1], sizeof(float) * n_mfcc * (T20::C10_DSP::MFCC_HISTORY_LEN - 1));
        memcpy(_mfcc_history[T20::C10_DSP::MFCC_HISTORY_LEN - 1], p_mfcc, sizeof(float) * n_mfcc);
    }
}

void CL_T20_DspPipeline::_build39DVector(ST_T20_FeatureVector_t* p_vec_out) {
    uint16_t n = _cfg.feature.mfcc_coeffs;
    const int t = 2; // 중심 프레임 인덱스

    // 1. Static (MFCC)
    memcpy(&p_vec_out->vector[0], _mfcc_history[t], sizeof(float) * n);

    // 2. Delta (1차 미분)
    for (int i = 0; i < n; i++) {
        p_vec_out->vector[i + n] = (_mfcc_history[t+1][i] - _mfcc_history[t-1][i]) / 2.0f;
    }

    // 3. Delta-Delta (2차 미분)
    for (int i = 0; i < n; i++) {
        p_vec_out->vector[i + 2 * n] = (_mfcc_history[t+1][i] - 2.0f * _mfcc_history[t][i] + _mfcc_history[t-1][i]);
    }
    
    p_vec_out->vector_len = n * 3;
}

