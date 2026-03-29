/* ============================================================================
[향후 단계 구현 예정 정리 - DSP]
1. sliding window 경로 정교화
2. preprocess pipeline 확장
3. mel filterbank / mfcc 세부 복구
4. live frame 입력 경로와 DSP 연결 강화
============================================================================ */

#include "T20_Mfcc_Inter_194.h"
#include <math.h>

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    T20_buildHammingWindow(p);

    memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mel_bank, 0, sizeof(p->mel_bank));
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (uint16_t m = 0; m < G_T20_MEL_FILTERS; ++m) {
        uint16_t left   = (uint16_t)((m * bins) / (G_T20_MEL_FILTERS + 1U));
        uint16_t center = (uint16_t)(((m + 1U) * bins) / (G_T20_MEL_FILTERS + 1U));
        uint16_t right  = (uint16_t)(((m + 2U) * bins) / (G_T20_MEL_FILTERS + 1U));
        if (center <= left) center = left + 1U;
        if (right <= center) right = center + 1U;
        if (right > bins) right = bins;

        for (uint16_t k = left; k < center && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        }
        for (uint16_t k = center; k < right && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(right - k) / (float)(right - center + G_T20_EPSILON);
        }
    }

    return T20_configureRuntimeFilter(p);
}

bool T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        return true;
    }

    float fs = p->cfg.feature.sample_rate_hz;
    if (fs <= 0.0f) return false;

    float fc = p->cfg.preprocess.filter.cutoff_hz_1;
    if (fc <= 0.0f) fc = 1.0f;
    float x = expf(-2.0f * G_T20_PI * fc / fs);

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        p->biquad_coeffs[0] = 1.0f - x;
        p->biquad_coeffs[1] = x;
    } else if (p->cfg.preprocess.filter.type == EN_T20_FILTER_HPF) {
        p->biquad_coeffs[0] = (1.0f + x) * 0.5f;
        p->biquad_coeffs[1] = x;
    } else {
        p->biquad_coeffs[0] = (1.0f + x) * 0.5f;
        p->biquad_coeffs[1] = x;
    }

    return true;
}

float T20_hzToMel(float p_hz) { return 2595.0f * log10f(1.0f + (p_hz / 700.0f)); }
float T20_melToHz(float p_mel) { return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f); }

void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return;
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->window[i] = 0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)i) / (float)(G_T20_FFT_SIZE - 1U));
    }
}

void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    if (p_data == nullptr || p_len == 0) return;
    float sum = 0.0f;
    for (uint16_t i = 0; i < p_len; ++i) sum += p_data[i];
    float mean = sum / (float)p_len;
    for (uint16_t i = 0; i < p_len; ++i) p_data[i] -= mean;
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha)
{
    if (p == nullptr || p_data == nullptr || p_len == 0) return;
    float prev = p->prev_raw_sample;
    for (uint16_t i = 0; i < p_len; ++i) {
        float cur = p_data[i];
        p_data[i] = cur - (p_alpha * prev);
        prev = cur;
    }
    p->prev_raw_sample = prev;
}

void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs)
{
    if (p_data == nullptr) return;
    for (uint16_t i = 0; i < p_len; ++i) {
        if (fabsf(p_data[i]) < p_threshold_abs) p_data[i] = 0.0f;
    }
}

void T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len)
{
    if (p == nullptr || p_in == nullptr || p_out == nullptr) return;

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    float s0 = p->biquad_state[0];
    float s1 = p->biquad_state[1];
    float a0 = p->biquad_coeffs[0];
    float a1 = p->biquad_coeffs[1];

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        for (uint16_t i = 0; i < p_len; ++i) {
            s0 = (a0 * p_in[i]) + (a1 * s0);
            p_out[i] = s0;
        }
    } else if (p->cfg.preprocess.filter.type == EN_T20_FILTER_HPF) {
        for (uint16_t i = 0; i < p_len; ++i) {
            float y = a0 * (p_in[i] - s1) + (a1 * s0);
            s1 = p_in[i];
            s0 = y;
            p_out[i] = y;
        }
    } else {
        for (uint16_t i = 0; i < p_len; ++i) {
            float hp = a0 * (p_in[i] - s1) + (a1 * s0);
            s1 = p_in[i];
            s0 = hp;
            p_out[i] = hp;
        }
        float smooth = 0.0f;
        for (uint16_t i = 0; i < p_len; ++i) {
            smooth = 0.2f * p_out[i] + 0.8f * smooth;
            p_out[i] = smooth;
        }
    }

    p->biquad_state[0] = s0;
    p->biquad_state[1] = s1;
}

void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power)
{
    if (p == nullptr || p_time == nullptr || p_power == nullptr) return;
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (uint16_t b = 0; b < bins; ++b) {
        uint16_t start = (uint16_t)((b * G_T20_FFT_SIZE) / bins);
        uint16_t end   = (uint16_t)((((uint32_t)b + 1U) * G_T20_FFT_SIZE) / bins);
        if (end <= start) end = start + 1U;
        if (end > G_T20_FFT_SIZE) end = G_T20_FFT_SIZE;
        float acc = 0.0f;
        for (uint16_t i = start; i < end; ++i) acc += p_time[i] * p_time[i];
        float pw = acc / (float)(end - start);
        if (pw < G_T20_EPSILON) pw = G_T20_EPSILON;
        p_power[b] = pw;
    }
}

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power)
{
    if (p == nullptr || p_power == nullptr) return;
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) return;
    if (p->noise_learned_frames >= p->cfg.preprocess.noise.noise_learn_frames) return;
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    uint16_t count = p->noise_learned_frames;
    for (uint16_t i = 0; i < bins; ++i) {
        p->noise_spectrum[i] = ((p->noise_spectrum[i] * (float)count) + p_power[i]) / (float)(count + 1U);
    }
    p->noise_learned_frames++;
}

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power)
{
    if (p == nullptr || p_power == nullptr) return;
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) return;
    if (p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames) return;
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    float strength = p->cfg.preprocess.noise.spectral_subtract_strength;
    for (uint16_t i = 0; i < bins; ++i) {
        float v = p_power[i] - (strength * p->noise_spectrum[i]);
        if (v < G_T20_EPSILON) v = G_T20_EPSILON;
        p_power[i] = v;
    }
}

void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out)
{
    if (p == nullptr || p_power == nullptr || p_log_mel_out == nullptr) return;
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (uint16_t m = 0; m < G_T20_MEL_FILTERS; ++m) {
        float sum = 0.0f;
        for (uint16_t k = 0; k < bins; ++k) sum += p_power[k] * p->mel_bank[m][k];
        if (sum < G_T20_EPSILON) sum = G_T20_EPSILON;
        p_log_mel_out[m] = logf(sum);
    }
}

void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len)
{
    if (p_in == nullptr || p_out == nullptr) return;
    for (uint16_t n = 0; n < p_out_len; ++n) {
        float sum = 0.0f;
        for (uint16_t k = 0; k < p_in_len; ++k) {
            sum += p_in[k] * cosf((G_T20_PI / (float)p_in_len) * ((float)k + 0.5f) * (float)n);
        }
        p_out[n] = sum;
    }
}

void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out)
{
    if (p == nullptr || p_frame == nullptr || p_mfcc_out == nullptr) return;

    memcpy(p->temp_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);

    if (p->cfg.preprocess.remove_dc) T20_applyDCRemove(p->temp_frame, G_T20_FFT_SIZE);
    if (p->cfg.preprocess.preemphasis.enable) T20_applyPreEmphasis(p, p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    if (p->cfg.preprocess.noise.enable_gate) T20_applyNoiseGate(p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.noise.gate_threshold_abs);

    T20_applyRuntimeFilter(p, p->temp_frame, p->work_frame, G_T20_FFT_SIZE);
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) p->work_frame[i] *= p->window[i];

    T20_computePowerSpectrum(p, p->work_frame, p->power);
    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);
    T20_applyMelFilterbank(p, p->power, p->log_mel);
    T20_computeDCT2(p->log_mel, p_mfcc_out, p->cfg.feature.mel_filters, p->cfg.feature.mfcc_coeffs);
}
