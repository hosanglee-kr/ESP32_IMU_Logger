#include "T20_Mfcc_Inter_056.h"
#include <math.h>

/* ============================================================================
 * File: T20_Mfcc_Dsp_056.cpp
 * Summary:
 *   DSP 최소 구현
 *
 * [주의]
 * - 이번 버전은 컴파일 복구용 베이스라인이라 실제 esp-dsp 고도화는 제외
 * - 향후 단계 구현 예정 사항은 TODO로 유지
 * ========================================================================== */

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    T20_buildHammingWindow(p);
    return true;
}

float T20_hzToMel(float p_hz)
{
    return 2595.0f * log10f(1.0f + (p_hz / 700.0f));
}

float T20_melToHz(float p_mel)
{
    return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f);
}

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

void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power)
{
    if (p == nullptr || p_time == nullptr || p_power == nullptr) return;
    uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (uint16_t i = 0; i < bins; ++i) {
        float v = p_time[i];
        p_power[i] = (v * v) + G_T20_EPSILON;
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

    if (p->cfg.preprocess.remove_dc) {
        T20_applyDCRemove(p->temp_frame, G_T20_FFT_SIZE);
    }

    if (p->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p, p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    }

    if (p->cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.noise.gate_threshold_abs);
    }

    memcpy(p->work_frame, p->temp_frame, sizeof(float) * G_T20_FFT_SIZE);
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->work_frame[i] *= p->window[i];
    }

    T20_computePowerSpectrum(p, p->work_frame, p->power);

    for (uint16_t i = 0; i < G_T20_MEL_FILTERS; ++i) {
        p->log_mel[i] = logf(p->power[i] + G_T20_EPSILON);
    }

    T20_computeDCT2(p->log_mel, p_mfcc_out, p->cfg.feature.mel_filters, p->cfg.feature.mfcc_coeffs);
}
