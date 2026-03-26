#include "T20_Mfcc_Inter_011.h"
#include "esp_dsp.h"

#include <math.h>
#include <string.h>

/*
===============================================================================
소스명: T20_Mfcc_Dsp_011.cpp
버전: v011

[기능 스펙]
- DSP 초기화
- BMI270 SPI / interrupt pin / ODR 설정
- Sliding Window 처리에 필요한 frame 단위 특징 추출
- Stage 배열 기반 전처리 파이프라인
- Window / FFT / Power / Mel / MFCC / Delta 전 단계

[향후 단계 구현 예정 사항]
- multi-config FFT / mel bank cache pool
- zero-copy / DMA / cache-aware copy 최소화
- stage 종류 확장(normalize, clipping, RMS normalize, detrend)
- multi-axis / fused feature path
===============================================================================
*/

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    esp_err_t v_res = dsps_fft2r_init_fc32(NULL, G_T20_FRAME_SIZE_FIXED);
    if (v_res != ESP_OK) {
        return false;
    }

    T20_buildHammingWindow(p);
    T20_buildMelFilterbank(p);
    return true;
}

bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p)
{
    int8_t v_rslt = p->imu.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    return (v_rslt == BMI2_OK);
}

bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p)
{
    int8_t v_rslt = BMI2_OK;

    v_rslt = p->imu.setAccelODR(BMI2_ACC_ODR_1600HZ);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setGyroODR(BMI2_GYR_ODR_1600HZ);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setGyroPowerMode(BMI2_PERF_OPT_MODE, BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) return false;

    bmi2_sens_config v_acc_cfg;
    memset(&v_acc_cfg, 0, sizeof(v_acc_cfg));
    v_acc_cfg.type = BMI2_ACCEL;

    v_rslt = p->imu.getConfig(&v_acc_cfg);
    if (v_rslt != BMI2_OK) return false;

    v_acc_cfg.cfg.acc.odr = BMI2_ACC_ODR_1600HZ;
    v_acc_cfg.cfg.acc.range = BMI2_ACC_RANGE_2G;
    v_acc_cfg.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    v_acc_cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    v_rslt = p->imu.setConfig(v_acc_cfg);
    if (v_rslt != BMI2_OK) return false;

    bmi2_int_pin_config v_int_cfg;
    memset(&v_int_cfg, 0, sizeof(v_int_cfg));
    v_int_cfg.pin_type = BMI2_INT1;
    v_int_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    v_int_cfg.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    v_int_cfg.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    v_int_cfg.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    v_int_cfg.int_latch = BMI2_INT_NON_LATCH;

    v_rslt = p->imu.setInterruptPinConfig(v_int_cfg);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.mapInterruptToPin(BMI2_DRDY_INT, BMI2_INT1);
    if (v_rslt != BMI2_OK) return false;

    return true;
}

bool T20_configurePipelineRuntime(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    memset(&p->pipeline_runtime, 0, sizeof(p->pipeline_runtime));
    return T20_buildPipelineSnapshot(&p->cfg, &p->pipeline_runtime);
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
    for (uint16_t v_i = 0; v_i < G_T20_FRAME_SIZE_FIXED; ++v_i) {
        p->window[v_i] = 0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)v_i) / (float)(G_T20_FRAME_SIZE_FIXED - 1U));
    }
}

void T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p)
{
    memset(p->mel_bank, 0, sizeof(p->mel_bank));

    const int v_num_bins = (G_T20_FRAME_SIZE_FIXED / 2) + 1;
    const float v_f_min = 0.0f;
    const float v_f_max = p->cfg.feature.sample_rate_hz * 0.5f;

    float v_mel_min = T20_hzToMel(v_f_min);
    float v_mel_max = T20_hzToMel(v_f_max);

    float v_mel_points[G_T20_MEL_FILTERS_FIXED + 2];
    float v_hz_points[G_T20_MEL_FILTERS_FIXED + 2];
    int   v_bin_points[G_T20_MEL_FILTERS_FIXED + 2];

    for (int v_i = 0; v_i < G_T20_MEL_FILTERS_FIXED + 2; ++v_i) {
        float v_ratio = (float)v_i / (float)(G_T20_MEL_FILTERS_FIXED + 1);
        v_mel_points[v_i] = v_mel_min + (v_mel_max - v_mel_min) * v_ratio;
        v_hz_points[v_i] = T20_melToHz(v_mel_points[v_i]);

        int v_bin = (int)floorf(((float)G_T20_FRAME_SIZE_FIXED + 1.0f) * v_hz_points[v_i] / p->cfg.feature.sample_rate_hz);
        if (v_bin < 0) v_bin = 0;
        if (v_bin >= v_num_bins) v_bin = v_num_bins - 1;
        v_bin_points[v_i] = v_bin;
    }

    for (int v_m = 0; v_m < G_T20_MEL_FILTERS_FIXED; ++v_m) {
        int v_left = v_bin_points[v_m];
        int v_center = v_bin_points[v_m + 1];
        int v_right = v_bin_points[v_m + 2];

        if (v_center <= v_left) v_center = v_left + 1;
        if (v_right <= v_center) v_right = v_center + 1;
        if (v_right >= v_num_bins) v_right = v_num_bins - 1;

        for (int v_k = v_left; v_k < v_center; ++v_k) {
            p->mel_bank[v_m][v_k] = (float)(v_k - v_left) / (float)(v_center - v_left);
        }

        for (int v_k = v_center; v_k <= v_right; ++v_k) {
            p->mel_bank[v_m][v_k] = (float)(v_right - v_k) / (float)(v_right - v_center + 1e-6f);
        }
    }
}

bool T20_buildConfigSnapshot(CL_T20_Mfcc::ST_Impl* p,
                             ST_T20_ConfigSnapshot_t* p_out)
{
    if (p == nullptr || p_out == nullptr || p->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    p_out->cfg = p->cfg;
    p_out->feature_dim = (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U);
    p_out->vector_len = p_out->feature_dim;

    xSemaphoreGive(p->mutex);
    return true;
}

bool T20_buildPipelineSnapshot(const ST_T20_Config_t* p_cfg,
                               ST_T20_PipelineSnapshot_t* p_out)
{
    if (p_cfg == nullptr || p_out == nullptr) {
        return false;
    }

    memset(p_out, 0, sizeof(ST_T20_PipelineSnapshot_t));
    p_out->stage_count = p_cfg->pipeline.stage_count;

    for (uint16_t v_i = 0; v_i < p_cfg->pipeline.stage_count; ++v_i) {
        p_out->stages[v_i].stage_cfg = p_cfg->pipeline.stages[v_i];
        memset(p_out->stages[v_i].biquad_state, 0, sizeof(p_out->stages[v_i].biquad_state));

        if (!T20_makeBiquadCoeffsFromStage(p_cfg,
                                           &p_out->stages[v_i].stage_cfg,
                                           p_out->stages[v_i].biquad_coeffs)) {
            return false;
        }
    }

    return true;
}

bool T20_makeBiquadCoeffsFromStage(const ST_T20_Config_t* p_cfg,
                                   const ST_T20_PreprocessStageConfig_t* p_stage_cfg,
                                   float* p_coeffs_out)
{
    if (p_cfg == nullptr || p_stage_cfg == nullptr || p_coeffs_out == nullptr) {
        return false;
    }

    memset(p_coeffs_out, 0, sizeof(float) * 5U);

    if (!p_stage_cfg->enable) {
        return true;
    }

    const float v_fs = p_cfg->feature.sample_rate_hz;
    const float v_q = (p_stage_cfg->q_factor <= 0.0f) ? 0.707f : p_stage_cfg->q_factor;
    esp_err_t v_res = ESP_OK;

    switch (p_stage_cfg->stage_type) {
        case EN_T20_STAGE_BIQUAD_LPF:
            v_res = dsps_biquad_gen_lpf_f32(p_coeffs_out, p_stage_cfg->param_1 / v_fs, v_q);
            break;
        case EN_T20_STAGE_BIQUAD_HPF:
            v_res = dsps_biquad_gen_hpf_f32(p_coeffs_out, p_stage_cfg->param_1 / v_fs, v_q);
            break;
        case EN_T20_STAGE_BIQUAD_BPF:
        {
            float v_low = p_stage_cfg->param_1;
            float v_high = p_stage_cfg->param_2;
            if (v_high <= v_low) return false;
            float v_center_hz = sqrtf(v_low * v_high);
            float v_bw_hz = v_high - v_low;
            float v_q_bpf = v_center_hz / (v_bw_hz + G_T20_EPSILON);
            if (v_q_bpf < 0.1f) v_q_bpf = 0.1f;
            v_res = dsps_biquad_gen_bpf_f32(p_coeffs_out, v_center_hz / v_fs, v_q_bpf);
            break;
        }
        default:
            return true;
    }

    return (v_res == ESP_OK);
}

void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    float v_mean = 0.0f;
    for (uint16_t v_i = 0; v_i < p_len; ++v_i) {
        v_mean += p_data[v_i];
    }
    v_mean /= (float)p_len;

    for (uint16_t v_i = 0; v_i < p_len; ++v_i) {
        p_data[v_i] -= v_mean;
    }
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p,
                          float* p_data,
                          uint16_t p_len,
                          float p_alpha)
{
    float v_prev = p->prev_raw_sample;
    for (uint16_t v_i = 0; v_i < p_len; ++v_i) {
        float v_cur = p_data[v_i];
        p_data[v_i] = v_cur - (p_alpha * v_prev);
        v_prev = v_cur;
    }
    p->prev_raw_sample = v_prev;
}

void T20_applyNoiseGate(float* p_data,
                        uint16_t p_len,
                        float p_threshold_abs)
{
    for (uint16_t v_i = 0; v_i < p_len; ++v_i) {
        if (fabsf(p_data[v_i]) < p_threshold_abs) {
            p_data[v_i] = 0.0f;
        }
    }
}

void T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                           const float* p_coeffs,
                           float* p_state,
                           const float* p_in,
                           float* p_out,
                           uint16_t p_len)
{
    if (p_cfg == nullptr || p_in == nullptr || p_out == nullptr) {
        return;
    }

    dsps_biquad_f32(p_in, p_out, p_len, const_cast<float*>(p_coeffs), p_state);
}

void T20_applyWindow(CL_T20_Mfcc::ST_Impl* p,
                     float* p_data,
                     uint16_t p_len)
{
    for (uint16_t v_i = 0; v_i < p_len; ++v_i) {
        p_data[v_i] *= p->window[v_i];
    }
}

bool T20_applyStage(CL_T20_Mfcc::ST_Impl* p,
                    ST_T20_StageRuntime_t* p_stage,
                    const float* p_in,
                    float* p_out,
                    uint16_t p_len)
{
    if (p_stage == nullptr || p_in == nullptr || p_out == nullptr) {
        return false;
    }

    if (!p_stage->stage_cfg.enable || p_stage->stage_cfg.stage_type == EN_T20_STAGE_NONE) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return true;
    }

    memcpy(p_out, p_in, sizeof(float) * p_len);

    switch (p_stage->stage_cfg.stage_type) {
        case EN_T20_STAGE_DC_REMOVE:
            T20_applyDCRemove(p_out, p_len);
            return true;

        case EN_T20_STAGE_PREEMPHASIS:
            T20_applyPreEmphasis(p, p_out, p_len, p_stage->stage_cfg.param_1);
            return true;

        case EN_T20_STAGE_NOISE_GATE:
            T20_applyNoiseGate(p_out, p_len, p_stage->stage_cfg.param_1);
            return true;

        case EN_T20_STAGE_BIQUAD_LPF:
        case EN_T20_STAGE_BIQUAD_HPF:
        case EN_T20_STAGE_BIQUAD_BPF:
            T20_applyBiquadFilter(&p->cfg,
                                  p_stage->biquad_coeffs,
                                  p_stage->biquad_state,
                                  p_in,
                                  p_out,
                                  p_len);
            return true;

        default:
            return true;
    }
}

bool T20_applyPreprocessPipeline(CL_T20_Mfcc::ST_Impl* p,
                                 ST_T20_PipelineSnapshot_t* p_pipe,
                                 const float* p_in,
                                 float* p_out,
                                 uint16_t p_len)
{
    if (p == nullptr || p_pipe == nullptr || p_in == nullptr || p_out == nullptr) {
        return false;
    }

    const float* v_src = p_in;
    float* v_dst = p->frame_stage_a;
    float* v_swap = p->frame_stage_b;

    if (p_pipe->stage_count == 0U) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return true;
    }

    for (uint16_t v_i = 0; v_i < p_pipe->stage_count; ++v_i) {
        if (!T20_applyStage(p, &p_pipe->stages[v_i], v_src, v_dst, p_len)) {
            return false;
        }

        v_src = v_dst;
        v_dst = (v_dst == p->frame_stage_a) ? v_swap : p->frame_stage_a;
    }

    memcpy(p_out, v_src, sizeof(float) * p_len);
    return true;
}

void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p,
                              const float* p_time,
                              float* p_power)
{
    for (uint16_t v_i = 0; v_i < G_T20_FRAME_SIZE_FIXED; ++v_i) {
        p->fft_buffer[2U * v_i] = p_time[v_i];
        p->fft_buffer[(2U * v_i) + 1U] = 0.0f;
    }

    dsps_fft2r_fc32(p->fft_buffer, G_T20_FRAME_SIZE_FIXED);
    dsps_bit_rev_fc32(p->fft_buffer, G_T20_FRAME_SIZE_FIXED);

    for (uint16_t v_k = 0; v_k <= (G_T20_FRAME_SIZE_FIXED / 2U); ++v_k) {
        float v_re = p->fft_buffer[2U * v_k];
        float v_im = p->fft_buffer[(2U * v_k) + 1U];
        float v_pw = (v_re * v_re + v_im * v_im) / (float)G_T20_FRAME_SIZE_FIXED;
        if (v_pw < G_T20_EPSILON) v_pw = G_T20_EPSILON;
        p_power[v_k] = v_pw;
    }
}

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p,
                            const float* p_power)
{
    if (!p->cfg.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames >= p->cfg.noise.noise_learn_frames) {
        return;
    }

    uint16_t v_count = p->noise_learned_frames;
    for (uint16_t v_k = 0; v_k <= (G_T20_FRAME_SIZE_FIXED / 2U); ++v_k) {
        p->noise_spectrum[v_k] = ((p->noise_spectrum[v_k] * (float)v_count) + p_power[v_k]) / (float)(v_count + 1U);
    }
    p->noise_learned_frames++;
}

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p,
                                  float* p_power)
{
    if (!p->cfg.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames < p->cfg.noise.noise_learn_frames) {
        return;
    }

    const float v_strength = p->cfg.noise.spectral_subtract_strength;
    for (uint16_t v_k = 0; v_k <= (G_T20_FRAME_SIZE_FIXED / 2U); ++v_k) {
        float v_sub = p_power[v_k] - (v_strength * p->noise_spectrum[v_k]);
        if (v_sub < G_T20_EPSILON) v_sub = G_T20_EPSILON;
        p_power[v_k] = v_sub;
    }
}

void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p,
                            const float* p_power,
                            float* p_log_mel_out)
{
    const uint16_t v_num_bins = (G_T20_FRAME_SIZE_FIXED / 2U) + 1U;
    for (uint16_t v_m = 0; v_m < G_T20_MEL_FILTERS_FIXED; ++v_m) {
        float v_sum = 0.0f;
        for (uint16_t v_k = 0; v_k < v_num_bins; ++v_k) {
            v_sum += p_power[v_k] * p->mel_bank[v_m][v_k];
        }
        if (v_sum < G_T20_EPSILON) v_sum = G_T20_EPSILON;
        p_log_mel_out[v_m] = logf(v_sum);
    }
}

void T20_computeDCT2(const float* p_in,
                     float* p_out,
                     uint16_t p_in_len,
                     uint16_t p_out_len)
{
    for (uint16_t v_n = 0; v_n < p_out_len; ++v_n) {
        float v_sum = 0.0f;
        for (uint16_t v_k = 0; v_k < p_in_len; ++v_k) {
            v_sum += p_in[v_k] * cosf((G_T20_PI / (float)p_in_len) * ((float)v_k + 0.5f) * (float)v_n);
        }
        p_out[v_n] = v_sum;
    }
}

void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                     const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                     ST_T20_PipelineSnapshot_t* p_pipe_snap,
                     const float* p_frame,
                     float* p_mfcc_out)
{
    // 1) Stage pipeline
    if (!T20_applyPreprocessPipeline(p,
                                     p_pipe_snap,
                                     p_frame,
                                     p->frame_stage_a,
                                     p_cfg_snap->cfg.feature.frame_size)) {
        memset(p_mfcc_out, 0, sizeof(float) * p_cfg_snap->cfg.feature.mfcc_coeffs);
        return;
    }

    // 2) Window
    memcpy(p->frame_stage_b, p->frame_stage_a, sizeof(float) * p_cfg_snap->cfg.feature.frame_size);
    T20_applyWindow(p, p->frame_stage_b, p_cfg_snap->cfg.feature.frame_size);

    // 3) FFT / Power
    T20_computePowerSpectrum(p, p->frame_stage_b, p->power);

    // 4) Noise profile / spectral subtraction
    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);

    // 5) Mel -> log mel
    T20_applyMelFilterbank(p, p->power, p->log_mel);

    // 6) DCT-II -> MFCC
    T20_computeDCT2(p->log_mel,
                    p_mfcc_out,
                    p_cfg_snap->cfg.feature.mel_filters,
                    p_cfg_snap->cfg.feature.mfcc_coeffs);
}
