#include "T20_Mfcc_Inter_015.h"
#include "esp_dsp.h"

#include <math.h>
#include <string.h>

/*
===============================================================================
소스명: T20_Mfcc_Dsp_015.cpp
버전: v015

[기능 스펙]
- DSP 초기화
- BMI270 SPI / interrupt pin / filter 설정
- stage 배열형 전처리 파이프라인
- Window / FFT / Mel Filterbank / DCT-II
- MFCC 전처리 및 특징 추출 파이프라인

[실컴파일 기준 보완 포인트]
- dsps_biquad_f32 계수 인자 non-const 요구 반영
- stage별 coeff/state 로컬 runtime 운용
===============================================================================
*/

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;

    esp_err_t res = dsps_fft2r_init_fc32(NULL, G_T20_FFT_SIZE);
    if (res != ESP_OK) {
        return false;
    }

    T20_buildHammingWindow(p);
    T20_buildMelFilterbank(p, &p->cfg);
    return true;
}

bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    int8_t rslt = p->imu.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    return (rslt == BMI2_OK);
}

bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) return false;
    int8_t rslt = BMI2_OK;

    rslt = p->imu.setAccelODR(BMI2_ACC_ODR_1600HZ);
    if (rslt != BMI2_OK) return false;

    rslt = p->imu.setGyroODR(BMI2_GYR_ODR_1600HZ);
    if (rslt != BMI2_OK) return false;

    rslt = p->imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
    if (rslt != BMI2_OK) return false;

    rslt = p->imu.setGyroPowerMode(BMI2_PERF_OPT_MODE, BMI2_PERF_OPT_MODE);
    if (rslt != BMI2_OK) return false;

    bmi2_sens_config acc_cfg;
    memset(&acc_cfg, 0, sizeof(acc_cfg));
    acc_cfg.type = BMI2_ACCEL;

    rslt = p->imu.getConfig(&acc_cfg);
    if (rslt != BMI2_OK) return false;

    acc_cfg.cfg.acc.odr         = BMI2_ACC_ODR_1600HZ;
    acc_cfg.cfg.acc.range       = BMI2_ACC_RANGE_2G;
    acc_cfg.cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    acc_cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    rslt = p->imu.setConfig(acc_cfg);
    if (rslt != BMI2_OK) return false;

    bmi2_sens_config gyr_cfg;
    memset(&gyr_cfg, 0, sizeof(gyr_cfg));
    gyr_cfg.type = BMI2_GYRO;

    rslt = p->imu.getConfig(&gyr_cfg);
    if (rslt != BMI2_OK) return false;

    gyr_cfg.cfg.gyr.odr         = BMI2_GYR_ODR_1600HZ;
    gyr_cfg.cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    gyr_cfg.cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    gyr_cfg.cfg.gyr.noise_perf  = BMI2_PERF_OPT_MODE;
    gyr_cfg.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    rslt = p->imu.setConfig(gyr_cfg);
    if (rslt != BMI2_OK) return false;

    bmi2_int_pin_config int_cfg;
    memset(&int_cfg, 0, sizeof(int_cfg));

    int_cfg.pin_type = BMI2_INT1;
    int_cfg.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    int_cfg.pin_cfg[0].od        = BMI2_INT_PUSH_PULL;
    int_cfg.pin_cfg[0].lvl       = BMI2_INT_ACTIVE_HIGH;
    int_cfg.pin_cfg[0].input_en  = BMI2_INT_INPUT_DISABLE;
    int_cfg.int_latch            = BMI2_INT_NON_LATCH;

    rslt = p->imu.setInterruptPinConfig(int_cfg);
    if (rslt != BMI2_OK) return false;

    rslt = p->imu.mapInterruptToPin(BMI2_DRDY_INT, BMI2_INT1);
    if (rslt != BMI2_OK) return false;

    return true;
}

bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    for (uint16_t i = 0; i < p->cfg.preprocess.pipeline.stage_count; ++i) {
        const ST_T20_PreprocessStageConfig_t& st = p->cfg.preprocess.pipeline.stages[i];
        if (!st.enable) continue;

        if (st.stage_type == EN_T20_STAGE_BIQUAD_LPF ||
            st.stage_type == EN_T20_STAGE_BIQUAD_HPF ||
            st.stage_type == EN_T20_STAGE_BIQUAD_BPF) {
            return T20_makeBiquadCoeffsFromStage(&p->cfg, &st, p->biquad_coeffs);
        }
    }

    return true;
}

bool T20_buildConfigSnapshot(CL_T20_Mfcc::ST_Impl* p, ST_T20_ConfigSnapshot_t* p_out)
{
    if (p == nullptr || p_out == nullptr || p->mutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    p_out->cfg = p->cfg;
    p_out->feature_dim = (uint16_t)(p->cfg.feature.mfcc_coeffs * 3U);
    p_out->vector_len  = p_out->feature_dim;

    xSemaphoreGive(p->mutex);
    return true;
}

bool T20_buildPipelineSnapshot(const ST_T20_Config_t* p_cfg, ST_T20_PipelineSnapshot_t* p_out)
{
    if (p_cfg == nullptr || p_out == nullptr) {
        return false;
    }

    memset(p_out, 0, sizeof(ST_T20_PipelineSnapshot_t));
    p_out->stage_count = p_cfg->preprocess.pipeline.stage_count;

    for (uint16_t i = 0; i < p_out->stage_count; ++i) {
        p_out->stages[i].stage_cfg = p_cfg->preprocess.pipeline.stages[i];

        if (!p_out->stages[i].stage_cfg.enable) {
            continue;
        }

        if (p_out->stages[i].stage_cfg.stage_type == EN_T20_STAGE_BIQUAD_LPF ||
            p_out->stages[i].stage_cfg.stage_type == EN_T20_STAGE_BIQUAD_HPF ||
            p_out->stages[i].stage_cfg.stage_type == EN_T20_STAGE_BIQUAD_BPF) {
            if (!T20_makeBiquadCoeffsFromStage(p_cfg,
                                               &p_out->stages[i].stage_cfg,
                                               p_out->stages[i].biquad_coeffs)) {
                return false;
            }
        }
    }

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

void T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg)
{
    if (p == nullptr || p_cfg == nullptr) return;

    memset(p->mel_bank, 0, sizeof(p->mel_bank));

    const int num_bins = (G_T20_FFT_SIZE / 2) + 1;
    const float f_min = 0.0f;
    const float f_max = p_cfg->feature.sample_rate_hz * 0.5f;

    float mel_min = T20_hzToMel(f_min);
    float mel_max = T20_hzToMel(f_max);

    float mel_points[G_T20_MEL_FILTERS + 2];
    float hz_points[G_T20_MEL_FILTERS + 2];
    int   bin_points[G_T20_MEL_FILTERS + 2];

    for (int i = 0; i < (G_T20_MEL_FILTERS + 2); ++i) {
        float ratio = (float)i / (float)(G_T20_MEL_FILTERS + 1);
        mel_points[i] = mel_min + (mel_max - mel_min) * ratio;
        hz_points[i] = T20_melToHz(mel_points[i]);

        int bin = (int)floorf(((float)G_T20_FFT_SIZE + 1.0f) * hz_points[i] / p_cfg->feature.sample_rate_hz);
        if (bin < 0) bin = 0;
        if (bin >= num_bins) bin = num_bins - 1;
        bin_points[i] = bin;
    }

    for (int m = 0; m < G_T20_MEL_FILTERS; ++m) {
        int left   = bin_points[m];
        int center = bin_points[m + 1];
        int right  = bin_points[m + 2];

        if (center <= left) center = left + 1;
        if (right <= center) right = center + 1;
        if (right >= num_bins) right = num_bins - 1;

        for (int k = left; k < center; ++k) {
            p->mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        }
        for (int k = center; k <= right; ++k) {
            p->mel_bank[m][k] = (float)(right - k) / (float)(right - center + 1e-6f);
        }
    }
}

void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    if (p_data == nullptr || p_len == 0) return;

    float mean = 0.0f;
    for (uint16_t i = 0; i < p_len; ++i) {
        mean += p_data[i];
    }
    mean /= (float)p_len;

    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] -= mean;
    }
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha)
{
    if (p == nullptr || p_data == nullptr) return;

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
        if (fabsf(p_data[i]) < p_threshold_abs) {
            p_data[i] = 0.0f;
        }
    }
}

bool T20_makeBiquadCoeffsFromStage(const ST_T20_Config_t* p_cfg,
                                   const ST_T20_PreprocessStageConfig_t* p_stage,
                                   float* p_coeffs_out)
{
    if (p_cfg == nullptr || p_stage == nullptr || p_coeffs_out == nullptr) {
        return false;
    }

    memset(p_coeffs_out, 0, sizeof(float) * 5U);

    float fs = p_cfg->feature.sample_rate_hz;
    float q  = (p_stage->q_factor <= 0.0f) ? 0.707f : p_stage->q_factor;
    esp_err_t res = ESP_OK;

    if (p_stage->stage_type == EN_T20_STAGE_BIQUAD_LPF) {
        float norm = p_stage->param_1 / fs;
        res = dsps_biquad_gen_lpf_f32(p_coeffs_out, norm, q);
    }
    else if (p_stage->stage_type == EN_T20_STAGE_BIQUAD_HPF) {
        float norm = p_stage->param_1 / fs;
        res = dsps_biquad_gen_hpf_f32(p_coeffs_out, norm, q);
    }
    else if (p_stage->stage_type == EN_T20_STAGE_BIQUAD_BPF) {
        float low = p_stage->param_1;
        float high = p_stage->param_2;
        if (high <= low) return false;

        float center_hz = sqrtf(low * high);
        float bw_hz = high - low;
        float q_bpf = center_hz / (bw_hz + G_T20_EPSILON);
        float center_norm = center_hz / fs;
        if (q_bpf < 0.1f) q_bpf = 0.1f;
        res = dsps_biquad_gen_bpf_f32(p_coeffs_out, center_norm, q_bpf);
    }
    else {
        return true;
    }

    return (res == ESP_OK);
}

void T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                           const ST_T20_PreprocessStageConfig_t* p_stage,
                           float* p_coeffs,
                           float* p_state,
                           const float* p_in,
                           float* p_out,
                           uint16_t p_len)
{
    (void)p_cfg;
    (void)p_stage;

    if (p_coeffs == nullptr || p_state == nullptr || p_in == nullptr || p_out == nullptr) {
        return;
    }

    dsps_biquad_f32(p_in, p_out, p_len, p_coeffs, p_state);
}

bool T20_applyStage(CL_T20_Mfcc::ST_Impl* p,
                    const ST_T20_Config_t* p_cfg,
                    ST_T20_StageRuntime_t* p_stage_rt,
                    const float* p_in,
                    float* p_out,
                    uint16_t p_len)
{
    if (p == nullptr || p_cfg == nullptr || p_stage_rt == nullptr || p_in == nullptr || p_out == nullptr) {
        return false;
    }

    if (!p_stage_rt->stage_cfg.enable || p_stage_rt->stage_cfg.stage_type == EN_T20_STAGE_NONE) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return true;
    }

    memcpy(p_out, p_in, sizeof(float) * p_len);

    switch (p_stage_rt->stage_cfg.stage_type) {
        case EN_T20_STAGE_DC_REMOVE:
            T20_applyDCRemove(p_out, p_len);
            return true;

        case EN_T20_STAGE_PREEMPHASIS:
            T20_applyPreEmphasis(p, p_out, p_len, p_stage_rt->stage_cfg.param_1);
            return true;

        case EN_T20_STAGE_NOISE_GATE:
            T20_applyNoiseGate(p_out, p_len, p_stage_rt->stage_cfg.param_1);
            return true;

        case EN_T20_STAGE_BIQUAD_LPF:
        case EN_T20_STAGE_BIQUAD_HPF:
        case EN_T20_STAGE_BIQUAD_BPF:
            T20_applyBiquadFilter(p_cfg,
                                  &p_stage_rt->stage_cfg,
                                  p_stage_rt->biquad_coeffs,
                                  p_stage_rt->biquad_state,
                                  p_in,
                                  p_out,
                                  p_len);
            return true;

        default:
            return false;
    }
}

bool T20_applyPreprocessPipeline(CL_T20_Mfcc::ST_Impl* p,
                                 const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                                 ST_T20_PipelineSnapshot_t* p_pipe_snap,
                                 const float* p_in,
                                 float* p_out,
                                 uint16_t p_len)
{
    if (p == nullptr || p_cfg_snap == nullptr || p_pipe_snap == nullptr || p_in == nullptr || p_out == nullptr) {
        return false;
    }

    const float* src = p_in;
    float* dst = p->frame_stage_a;
    bool using_a = true;

    if (p_pipe_snap->stage_count == 0) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return true;
    }

    for (uint16_t i = 0; i < p_pipe_snap->stage_count; ++i) {
        if (!T20_applyStage(p, &p_cfg_snap->cfg, &p_pipe_snap->stages[i], src, dst, p_len)) {
            return false;
        }

        src = dst;
        if (using_a) {
            dst = p->frame_stage_b;
            using_a = false;
        } else {
            dst = p->frame_stage_a;
            using_a = true;
        }
    }

    memcpy(p_out, src, sizeof(float) * p_len);
    return true;
}

void T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len)
{
    if (p == nullptr || p_data == nullptr) return;

    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] *= p->window[i];
    }
}

void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power)
{
    if (p == nullptr || p_time == nullptr || p_power == nullptr) return;

    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->fft_buffer[2U * i + 0U] = p_time[i];
        p->fft_buffer[2U * i + 1U] = 0.0f;
    }

    dsps_fft2r_fc32(p->fft_buffer, G_T20_FFT_SIZE);
    dsps_bit_rev_fc32(p->fft_buffer, G_T20_FFT_SIZE);

    for (uint16_t k = 0; k <= (G_T20_FFT_SIZE / 2U); ++k) {
        float re = p->fft_buffer[2U * k + 0U];
        float im = p->fft_buffer[2U * k + 1U];
        float pw = (re * re + im * im) / (float)G_T20_FFT_SIZE;
        if (pw < G_T20_EPSILON) pw = G_T20_EPSILON;
        p_power[k] = pw;
    }
}

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, const float* p_power)
{
    (void)p_cfg;

    if (p == nullptr || p_power == nullptr) return;
    if (p->noise_learned_frames >= G_T20_NOISE_MIN_FRAMES) return;

    uint16_t count = p->noise_learned_frames;
    for (uint16_t k = 0; k <= (G_T20_FFT_SIZE / 2U); ++k) {
        p->noise_spectrum[k] =
            ((p->noise_spectrum[k] * (float)count) + p_power[k]) / (float)(count + 1U);
    }
    p->noise_learned_frames++;
}

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, float* p_power)
{
    (void)p_cfg;
    (void)p_power;

    if (p == nullptr) return;

    // 향후 공개 설정 추가 예정.
    // 현재 단계에서는 컴파일 안전성과 구조 유지 목적의 no-op 처리.
}

void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const ST_T20_Config_t* p_cfg, const float* p_power, float* p_log_mel_out)
{
    if (p == nullptr || p_cfg == nullptr || p_power == nullptr || p_log_mel_out == nullptr) return;

    const int num_bins = (G_T20_FFT_SIZE / 2) + 1;

    for (uint16_t m = 0; m < p_cfg->feature.mel_filters; ++m) {
        float sum = 0.0f;
        for (int k = 0; k < num_bins; ++k) {
            sum += p_power[k] * p->mel_bank[m][k];
        }
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

void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                     const ST_T20_ConfigSnapshot_t* p_cfg_snap,
                     ST_T20_PipelineSnapshot_t* p_pipe_snap,
                     const float* p_frame,
                     float* p_mfcc_out)
{
    if (p == nullptr || p_cfg_snap == nullptr || p_pipe_snap == nullptr ||
        p_frame == nullptr || p_mfcc_out == nullptr) {
        return;
    }

    if (!T20_applyPreprocessPipeline(p,
                                     p_cfg_snap,
                                     p_pipe_snap,
                                     p_frame,
                                     p->frame_stage_a,
                                     p_cfg_snap->cfg.feature.frame_size)) {
        memset(p_mfcc_out, 0, sizeof(float) * p_cfg_snap->cfg.feature.mfcc_coeffs);
        return;
    }

    T20_applyWindow(p, p->frame_stage_a, p_cfg_snap->cfg.feature.frame_size);
    T20_computePowerSpectrum(p, p->frame_stage_a, p->power);
    T20_learnNoiseSpectrum(p, &p_cfg_snap->cfg, p->power);
    T20_applySpectralSubtraction(p, &p_cfg_snap->cfg, p->power);
    T20_applyMelFilterbank(p, &p_cfg_snap->cfg, p->power, p->log_mel);
    T20_computeDCT2(p->log_mel,
                    p_mfcc_out,
                    p_cfg_snap->cfg.feature.mel_filters,
                    p_cfg_snap->cfg.feature.mfcc_coeffs);
}
