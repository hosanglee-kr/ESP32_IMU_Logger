/* ============================================================================
 * File: T231_Mfcc_Dsp_216.cpp
 * Summary: MFCC 알고리즘 및 DSP 파이프라인 (ESP32-S3 SIMD, C++17 Namespace 통합 반영)
 * Compiler: gnu++17 기준 최적화 (PlatformIO Arduino Core 호환)
 * ========================================================================== */

#include <math.h>
#include <string.h>

#include "esp_dsp.h"

#include "T221_Mfcc_Inter_216.h"

/* ============================================================================
 * 1. 주파수 - Mel 스케일 변환 
 * ========================================================================== */

float T20_hzToMel(float p_hz) {
    return 2595.0f * log10f(1.0f + (p_hz / 700.0f));
}

float T20_melToHz(float p_mel) {
    return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f);
}

/* ============================================================================
 * 2. 초기화 및 윈도우/필터뱅크 생성
 * ========================================================================== */

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    
    static bool dsp_initialized = false;
    if (!dsp_initialized) {
        // ESP-DSP Radix-2 전용 초기화 (S3 최적화 테이블 생성)
        esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        if (err != ESP_OK) return false;
        dsp_initialized = true;
    }

    // ESP-DSP 내장 함수를 이용한 Hamming 윈도우 생성
    T20_buildHammingWindow(p);

    memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mel_bank, 0, sizeof(p->mel_bank));

    // 진정한 비선형 Mel 스케일 필터뱅크 생성
    float min_mel = T20_hzToMel(0.0f);
    float max_mel = T20_hzToMel(p->cfg.feature.sample_rate_hz / 2.0f);
    float mel_step = (max_mel - min_mel) / (T20::C10_DSP::MEL_FILTERS + 1U);

    uint16_t bin_points[T20::C10_DSP::MEL_FILTERS + 2];
    for(uint16_t i = 0; i < T20::C10_DSP::MEL_FILTERS + 2; i++) {
        float hz = T20_melToHz(min_mel + (i * mel_step));
        bin_points[i] = (uint16_t)roundf(T20::C10_DSP::FFT_SIZE * hz / p->cfg.feature.sample_rate_hz);
    }

    const uint16_t bins = (T20::C10_DSP::FFT_SIZE / 2U) + 1U;

    for (uint16_t m = 0; m < T20::C10_DSP::MEL_FILTERS; ++m) {
        uint16_t left   = bin_points[m];
        uint16_t center = bin_points[m + 1];
        uint16_t right  = bin_points[m + 2];

        // FFT Bin 해상도 한계로 인한 오버랩 안전장치
        if (center <= left) center = left + 1U;
        if (right <= center) right = center + 1U;
        if (right > bins) right = bins;

        for (uint16_t k = left; k < center && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        }
        for (uint16_t k = center; k < right && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(right - k) / (float)(right - center + T20::C10_DSP::EPSILON);
        }
    }
    
    // [최적화] DCT 매트릭스 캐싱 (매 프레임마다 cosf 연산을 피하기 위함)
    for (uint16_t n = 0; n < p->cfg.feature.mfcc_coeffs; ++n) {
        for (uint16_t k = 0; k < p->cfg.feature.mel_filters; ++k) {
            p->dct_matrix[n][k] = cosf((T20::C10_DSP::MATH_PI / (float)p->cfg.feature.mel_filters) * ((float)k + 0.5f) * (float)n);
        }
    }

    return T20_configureRuntimeFilter(p);
}

// esp-dsp에 hamming 미지원해서 자체 구현
void T20_dsps_wind_hamming_f32(float *data, int len) {
    float len_mult = 1 / (float)(len - 1);
    for (int i = 0; i < len; i++) {
        // Hamming 공식: w(n) = 0.54 - 0.46 * cos(2 * PI * n / (N - 1))
        data[i] = 0.54f - 0.46f * cosf(i * 2 * M_PI * len_mult);
    }
}

void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p) {
    T20_dsps_wind_hamming_f32(p->window, T20::C10_DSP::FFT_SIZE);
}

/* ============================================================================
 * 3. 런타임 필터 (ESP-DSP Biquad IIR 적용)
 * ========================================================================== */

bool T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        return true;
    }

    float fs = p->cfg.feature.sample_rate_hz;
    float fc = p->cfg.preprocess.filter.cutoff_hz_1;
    if (fs <= 0.0f) return false;
    if (fc <= 0.0f) fc = 1.0f;

    // 나이퀴스트 정규화 주파수 변환
    float f = fc / fs; 
    float q = p->cfg.preprocess.filter.q_factor;

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        dsps_biquad_gen_lpf_f32(p->biquad_coeffs, f, q);
    } else {
        dsps_biquad_gen_hpf_f32(p->biquad_coeffs, f, q);
    }
    return true;
}

void T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len) {
    if (p == nullptr || p_in == nullptr || p_out == nullptr) return;

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    // ESP-DSP SIMD 가속 Biquad 필터 적용
    dsps_biquad_f32(p_in, p_out, p_len, p->biquad_coeffs, p->biquad_state);
}

/* ============================================================================
 * 4. 전처리 및 스펙트럼 연산 (SIMD 가속 FFT 통합)
 * ========================================================================== */

void T20_applyDCRemove(float* p_data, uint16_t p_len) {
    if (!p_data || p_len == 0) return;
    float sum = 0.0f;
    for (uint16_t i = 0; i < p_len; i++) sum += p_data[i];
    float mean = sum / (float)p_len;
    for (uint16_t i = 0; i < p_len; i++) p_data[i] -= mean;
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha) {
    if (!p || !p_data) return;
    float prev = p->prev_raw_sample;
    for (uint16_t i = 0; i < p_len; i++) {
        float cur = p_data[i];
        p_data[i] = cur - (p_alpha * prev);
        prev = cur;
    }
    p->prev_raw_sample = prev;
}

void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs) {
    if (!p_data) return;
    for (uint16_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_threshold_abs) p_data[i] = 0.0f;
    }
}

void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power) {
    if (!p || !p_time || !p_power) return;

    // ESP-DSP를 위한 16바이트 정렬 복소수 버퍼 할당
    alignas(16) static float fft_input[T20::C10_DSP::FFT_SIZE * 2];
    
    for (int i = 0; i < T20::C10_DSP::FFT_SIZE; i++) {
        fft_input[i * 2] = p_time[i];
        fft_input[i * 2 + 1] = 0.0f;
    }

    // Radix-2 Complex FFT 실행 (S3 PIE 가속)
    dsps_fft2r_fc32(fft_input, T20::C10_DSP::FFT_SIZE);
    dsps_bit_rev2r_fc32(fft_input, T20::C10_DSP::FFT_SIZE);

    const uint16_t bins = (T20::C10_DSP::FFT_SIZE / 2U) + 1U;
    for (int i = 0; i < bins; i++) {
        float re = fft_input[i * 2];
        float im = fft_input[i * 2 + 1];
        float mag_sq = (re * re) + (im * im);
        
        p_power[i] = (mag_sq < T20::C10_DSP::EPSILON) ? T20::C10_DSP::EPSILON : mag_sq;
    }
}

/* ============================================================================
 * 5. 노이즈 적응 및 특징량(MFCC) 추출
 * ========================================================================== */

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power) {
    if (!p || !p_power || p->cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;

    const uint16_t bins = (T20::C10_DSP::FFT_SIZE / 2U) + 1U;
    bool is_learning = p->noise_learning_active || (p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames);

    if (!is_learning) return;

    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_ADAPTIVE) {
        float alpha = p->noise_learning_active ? 0.2f : p->cfg.preprocess.noise.adaptive_alpha;
        for (uint16_t i = 0; i < bins; ++i) {
            p->noise_spectrum[i] = (1.0f - alpha) * p->noise_spectrum[i] + alpha * p_power[i];
        }
    } else {
        float count = (float)p->noise_learned_frames;
        for (uint16_t i = 0; i < bins; ++i) {
            p->noise_spectrum[i] = ((p->noise_spectrum[i] * count) + p_power[i]) / (count + 1.0f);
        }
    }

    if (p->noise_learned_frames < 0xFFFF) p->noise_learned_frames++;
}

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power) {
    if (p == nullptr || p_power == nullptr) return;
    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;
    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_FIXED && 
        p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    const uint16_t bins = (T20::C10_DSP::FFT_SIZE / 2U) + 1U;
    const float strength = p->cfg.preprocess.noise.spectral_subtract_strength;

    for (uint16_t i = 0; i < bins; ++i) {
        float sub_result = p_power[i] - (strength * p->noise_spectrum[i]);
        if (sub_result < T20::C10_DSP::EPSILON) sub_result = T20::C10_DSP::EPSILON;
        p_power[i] = sub_result;
    }
}

/* Mel-Filterbank 합산에 SIMD 내적 적용 */
void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out) {
    if (p == nullptr || p_power == nullptr || p_log_mel_out == nullptr) return;
    const uint16_t bins = (T20::C10_DSP::FFT_SIZE / 2U) + 1U;

    for (uint16_t m = 0; m < T20::C10_DSP::MEL_FILTERS; ++m) {
        float sum = 0.0f;
        // [SIMD 가속] dsps_dotprod_f32를 이용한 고속 내적 연산
        dsps_dotprod_f32(p_power, p->mel_bank[m], &sum, bins);
        
        if (sum < T20::C10_DSP::EPSILON) sum = T20::C10_DSP::EPSILON;
        p_log_mel_out[m] = logf(sum);
    }
}

/* DCT 연산을 SIMD 매트릭스 내적으로 교체 */
void T20_computeDCT2(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out) {
    if (p == nullptr || p_in == nullptr || p_out == nullptr) return;
    
    uint16_t out_len = p->cfg.feature.mfcc_coeffs;
    uint16_t in_len  = p->cfg.feature.mel_filters;

    for (uint16_t n = 0; n < out_len; ++n) {
        float sum = 0.0f;
        // [SIMD 가속] 사전 계산된 DCT 행렬 행(Row)과 Log-Mel 벡터의 고속 내적
        dsps_dotprod_f32(p_in, p->dct_matrix[n], &sum, in_len);
        p_out[n] = sum;
    }
}

/* ============================================================================
 * 6. 히스토리 기반 Delta 연산 (39차 벡터 생성)
 * ========================================================================== */

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim) {
    if (!p || !p_mfcc) return;
    if (p->mfcc_history_count < T20::C10_DSP::MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count++], p_mfcc, sizeof(float) * p_dim);
    } else {
        for (int i = 1; i < T20::C10_DSP::MFCC_HISTORY; i++) {
            memcpy(p->mfcc_history[i-1], p->mfcc_history[i], sizeof(float) * p_dim);
        }
        memcpy(p->mfcc_history[T20::C10_DSP::MFCC_HISTORY-1], p_mfcc, sizeof(float) * p_dim);
    }
}

void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out) {
    if (!p || !p_delta_out || p->mfcc_history_count < T20::C10_DSP::MFCC_HISTORY) return;

    float denominator = 0.0f;
    for (int n = 1; n <= p_delta_window; n++) denominator += (float)(n * n);
    denominator *= 2.0f;

    for (uint16_t i = 0; i < p_dim; i++) {
        float numerator = 0.0f;
        for (int n = 1; n <= p_delta_window; n++) {
            numerator += (float)n * (p->mfcc_history[2 + n][i] - p->mfcc_history[2 - n][i]);
        }
        p_delta_out[i] = numerator / (denominator + T20::C10_DSP::EPSILON);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out) {
    if (!p || !p_delta2_out || p->mfcc_history_count < T20::C10_DSP::MFCC_HISTORY) return; 
    const int t = 2; 
    for (uint16_t i = 0; i < p_dim; i++) {
        p_delta2_out[i] = p->mfcc_history[t + 1][i] - (2.0f * p->mfcc_history[t][i]) + p->mfcc_history[t - 1][i];
    }
}

void T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec) {
    if (!p_mfcc || !p_delta || !p_delta2 || !p_out_vec) return;
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i] = p_mfcc[i];          
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i + p_dim] = p_delta[i]; 
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i + (p_dim * 2)] = p_delta2[i]; 
}

/* ============================================================================
 * 7. MFCC 파이프라인 마스터 (SIMD 벡터 곱셈 통합)
 * ========================================================================== */

void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out) {
    if (!p || !p_frame || !p_mfcc_out) return;

    // 1. 전처리 (DC 제거 및 Pre-emphasis)
    memcpy(p->temp_frame, p_frame, sizeof(float) * T20::C10_DSP::FFT_SIZE);
    
    if (p->cfg.preprocess.remove_dc) {
        T20_applyDCRemove(p->temp_frame, T20::C10_DSP::FFT_SIZE);
    }
    
    if (p->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p, p->temp_frame, T20::C10_DSP::FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    }

    // 노이즈 게이트(Noise Gate) 적용
    if (p->cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(p->temp_frame, T20::C10_DSP::FFT_SIZE, p->cfg.preprocess.noise.gate_threshold_abs);
    }

    // 2. IIR 필터 적용 (dsps_biquad_f32)
    T20_applyRuntimeFilter(p, p->temp_frame, p->work_frame, T20::C10_DSP::FFT_SIZE);
    
    // 3. 윈도우 적용 (dsps_mul_f32를 통한 SIMD 벡터 곱셈)
    dsps_mul_f32(p->work_frame, p->window, p->work_frame, T20::C10_DSP::FFT_SIZE, 1, 1, 1);

    // 4. SIMD 가속 복소수 FFT 연산
    T20_computePowerSpectrum(p, p->work_frame, p->power);

    // 5. 적응형 노이즈 게이트 및 서브트랙션
    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);

    // 6. 비선형 스케일 특징량 추출 (Mel 에너지 -> DCT-II)
    T20_applyMelFilterbank(p, p->power, p->log_mel);
    T20_computeDCT2(p, p->log_mel, p_mfcc_out);
}
