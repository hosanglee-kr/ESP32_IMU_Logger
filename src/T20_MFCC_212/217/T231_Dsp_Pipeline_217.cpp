/* ============================================================================
 * File: T231_Dsp_Pipeline_217.cpp
 * Summary: MFCC & DSP Pipeline Engine Implementation (v217)
 * ========================================================================== */

#include "T231_Dsp_Pipeline_217.h"
#include <math.h>
#include <string.h>

CL_T20_DspPipeline::CL_T20_DspPipeline() {
    memset(_mfcc_history, 0, sizeof(_mfcc_history));
    memset(_noise_spectrum, 0, sizeof(_noise_spectrum));
    memset(_biquad_state, 0, sizeof(_biquad_state));
    memset(_biquad_coeffs, 0, sizeof(_biquad_coeffs));

    static bool dsp_lib_init = false;
    if (!dsp_lib_init) {
        dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        dsp_lib_init = true;
    }
}

bool CL_T20_DspPipeline::begin(const ST_T20_Config_t& cfg) {
    _cfg = cfg;
    _history_count = 0;
    _noise_learned_frames = 0;
    _prev_sample = 0.0f;
    _noise_learning_active = false;

    const uint16_t N = T20::C10_DSP::FFT_SIZE;
    const uint16_t bins = T20::C10_DSP::FFT_BINS;
    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;

    // 1. Hamming Window
    for (int i = 0; i < N; i++) {
        _window[i] = 0.54f - 0.46f * cosf(i * 2.0f * M_PI / (float)(N - 1));
    }

    // 2. Mel Filterbank
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
        if (center <= left) center = left + 1;
        if (right <= center) right = center + 1;
        if (right > bins) right = bins;

        for (int k = left; k < center && k < bins; k++) 
            _mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        for (int k = center; k < right && k < bins; k++) 
            _mel_bank[m][k] = (float)(right - k) / (float)(right - center + 1e-12f);
    }

    // 3. DCT-II Matrix
    for (int n = 0; n < T20::C10_DSP::MFCC_COEFFS_MAX; n++) {
        for (int k = 0; k < T20::C10_DSP::MEL_FILTERS; k++) {
            _dct_matrix[n][k] = cosf((M_PI / (float)T20::C10_DSP::MEL_FILTERS) * (k + 0.5f) * n);
        }
    }

    // 4. 동적 IIR Biquad 필터 설정 (v216 복원)
    if (_cfg.preprocess.filter.enable && _cfg.preprocess.filter.type != EN_T20_FILTER_OFF) {
        float fc = _cfg.preprocess.filter.cutoff_hz_1;
        if (fc <= 0.0f) fc = 1.0f;
        float f_norm = fc / fs;
        float q = _cfg.preprocess.filter.q_factor;

        if (_cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
            dsps_biquad_gen_lpf_f32(_biquad_coeffs, f_norm, q);
        } else {
            dsps_biquad_gen_hpf_f32(_biquad_coeffs, f_norm, q);
        }
    }

    return true;
}

bool CL_T20_DspPipeline::processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out) {
    if (!p_time_in || !p_vec_out) return false;

    // 1. 버퍼 복사 및 파이프라인 수행 (v216 순서 완벽 일치)
    memcpy(_work_frame, p_time_in, sizeof(float) * T20::C10_DSP::FFT_SIZE);
    
    _applyPreprocess(_work_frame);
    _applyNoiseGate(_work_frame);
    _applyRuntimeFilter(_work_frame);

    dsps_mul_f32(_work_frame, _window, _work_frame, T20::C10_DSP::FFT_SIZE, 1, 1, 1);

    _computePowerSpectrum(_work_frame);
    
    _learnNoiseSpectrum();
    _applySpectralSubtraction();

    alignas(16) float current_mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    _applyMelFilterbank(_log_mel);
    _computeDCT2(_log_mel, current_mfcc);

    _pushHistory(current_mfcc);
    
    // 히스토리가 5프레임 모이면 39차원 벡터 반환
    if (_history_count >= T20::C10_DSP::MFCC_HISTORY_LEN) {
        _build39DVector(p_vec_out);
        return true;
    }

    return false;
}

void CL_T20_DspPipeline::_applyPreprocess(float* p_data) {
    if (_cfg.preprocess.remove_dc) {
        float sum = 0.0f;
        for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) sum += p_data[i];
        float mean = sum / (float)T20::C10_DSP::FFT_SIZE;
        for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) p_data[i] -= mean;
    }

    if (_cfg.preprocess.preemphasis.enable) {
        float alpha = _cfg.preprocess.preemphasis.alpha;
        for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) {
            float cur = p_data[i];
            p_data[i] = cur - (alpha * _prev_sample);
            _prev_sample = cur;
        }
    }
}

void CL_T20_DspPipeline::_applyNoiseGate(float* p_data) {
    if (!_cfg.preprocess.noise.enable_gate) return;
    float threshold = _cfg.preprocess.noise.gate_threshold_abs;
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) {
        if (fabsf(p_data[i]) < threshold) p_data[i] = 0.0f;
    }
}

void CL_T20_DspPipeline::_applyRuntimeFilter(float* p_data) {
    if (!_cfg.preprocess.filter.enable || _cfg.preprocess.filter.type == EN_T20_FILTER_OFF) return;
    dsps_biquad_f32(p_data, p_data, T20::C10_DSP::FFT_SIZE, _biquad_coeffs, _biquad_state);
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
        _power[i] = fmaxf((re * re + im * im), 1e-12f);
    }
}

void CL_T20_DspPipeline::_learnNoiseSpectrum() {
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;

    bool is_learning = _noise_learning_active || (_noise_learned_frames < _cfg.preprocess.noise.noise_learn_frames);
    if (!is_learning) return;

    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_ADAPTIVE) {
        float alpha = _noise_learning_active ? 0.2f : _cfg.preprocess.noise.adaptive_alpha;
        for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
            _noise_spectrum[i] = (1.0f - alpha) * _noise_spectrum[i] + alpha * _power[i];
        }
    } else { // EN_T20_NOISE_FIXED
        float count = (float)_noise_learned_frames;
        for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
            _noise_spectrum[i] = ((_noise_spectrum[i] * count) + _power[i]) / (count + 1.0f);
        }
    }
    if (_noise_learned_frames < 0xFFFF) _noise_learned_frames++;
}

void CL_T20_DspPipeline::_applySpectralSubtraction() {
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_FIXED && _noise_learned_frames < _cfg.preprocess.noise.noise_learn_frames) return;

    float strength = _cfg.preprocess.noise.spectral_subtract_strength;
    for (int i = 0; i < T20::C10_DSP::FFT_BINS; i++) {
        _power[i] = fmaxf(_power[i] - (strength * _noise_spectrum[i]), 1e-12f);
    }
}

void CL_T20_DspPipeline::_applyMelFilterbank(float* p_log_mel_out) {
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        float sum = 0.0f;
        dsps_dotprod_f32(_power, _mel_bank[m], &sum, T20::C10_DSP::FFT_BINS);
        p_log_mel_out[m] = logf(fmaxf(sum, 1e-12f));
    }
}

void CL_T20_DspPipeline::_computeDCT2(const float* p_in, float* p_out) {
    uint16_t dim = _cfg.feature.mfcc_coeffs;
    for (int i = 0; i < dim; i++) {
        float sum = 0.0f;
        dsps_dotprod_f32(p_in, _dct_matrix[i], &sum, T20::C10_DSP::MEL_FILTERS);
        p_out[i] = sum;
    }
}

void CL_T20_DspPipeline::_pushHistory(const float* p_mfcc) {
    uint16_t dim = _cfg.feature.mfcc_coeffs;
    if (_history_count < T20::C10_DSP::MFCC_HISTORY_LEN) {
        memcpy(_mfcc_history[_history_count++], p_mfcc, sizeof(float) * dim);
    } else {
        memmove(_mfcc_history[0], _mfcc_history[1], sizeof(float) * dim * (T20::C10_DSP::MFCC_HISTORY_LEN - 1));
        memcpy(_mfcc_history[T20::C10_DSP::MFCC_HISTORY_LEN - 1], p_mfcc, sizeof(float) * dim);
    }
}

void CL_T20_DspPipeline::_computeDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta_out) {
    // Window = 2 기준 공식: (2*h[t+2] + h[t+1] - h[t-1] - 2*h[t-2]) / 10
    // 여기서는 v216의 Window=2 간소화 공식을 적용합니다.
    const int t = 2; 
    for (int i = 0; i < dim; i++) {
        delta_out[i] = (history[t+1][i] - history[t-1][i]) / 2.0f; 
    }
}

void CL_T20_DspPipeline::_computeDeltaDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta2_out) {
    const int t = 2;
    for (int i = 0; i < dim; i++) {
        delta2_out[i] = history[t+1][i] - (2.0f * history[t][i]) + history[t-1][i];
    }
}

void CL_T20_DspPipeline::_build39DVector(ST_T20_FeatureVector_t* p_vec_out) {
    uint16_t dim = _cfg.feature.mfcc_coeffs;
    
    alignas(16) float delta[T20::C10_DSP::MFCC_COEFFS_MAX];
    alignas(16) float delta2[T20::C10_DSP::MFCC_COEFFS_MAX];
    
    _computeDelta(_mfcc_history, dim, delta);
    _computeDeltaDelta(_mfcc_history, dim, delta2);

    // MFCC, Delta, Delta2 결합
    memcpy(&p_vec_out->vector[0], _mfcc_history[2], sizeof(float) * dim);
    memcpy(&p_vec_out->vector[dim], delta, sizeof(float) * dim);
    memcpy(&p_vec_out->vector[dim * 2], delta2, sizeof(float) * dim);
    
    p_vec_out->vector_len = dim * 3;
}
