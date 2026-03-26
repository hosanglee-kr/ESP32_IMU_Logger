#include "T20_Mfcc_Inter_010.h"
#include "esp_dsp.h"

#include <math.h>
#include <string.h>

/*
===============================================================================
소스명: T20_Mfcc_Dsp_010.cpp
버전: v010

[기능 스펙]
- DSP 초기화
- BMI270 SPI / interrupt pin / filter 설정
- Window / FFT / Mel Filterbank / DCT-II
- MFCC 전처리 및 특징 추출 파이프라인

[구조]
- shared impl:
  - window
  - noise_spectrum
  - current cfg filter coeff cache
- frame-local snapshot:
  - cfg snapshot
  - filter coeff/state
  - temp/work/fft/power/logmel/melbank
===============================================================================
*/

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    esp_err_t res = dsps_fft2r_init_fc32(NULL, G_T20_FFT_SIZE);
    if (res != ESP_OK) {
        return false;
    }

    T20_buildHammingWindow(p);
    return true;
}

bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

    const int8_t rslt = p->imu.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    return (rslt == BMI2_OK);
}

bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p)
{
    if (p == nullptr) {
        return false;
    }

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

    // 현재 cfg 기준 coeff cache 생성
    return T20_makeFilterCoeffs(&p->cfg, p->biquad_coeffs);
}

bool T20_prepareDspSnapshot(CL_T20_Mfcc::ST_Impl* p,
                            const ST_T20_Config_t* p_cfg,
                            ST_T20_DspSnapshot_t* p_snap)
{
    if (p == nullptr || p_cfg == nullptr || p_snap == nullptr) {
        return false;
    }

    memset(p_snap, 0, sizeof(ST_T20_DspSnapshot_t));
    p_snap->cfg = *p_cfg;

    if (!T20_makeFilterCoeffs(p_cfg, p_snap->filter_coeffs)) {
        return false;
    }

    memset(p_snap->filter_state, 0, sizeof(p_snap->filter_state));

    T20_buildMelFilterbankFromConfig(p_cfg, p_snap->mel_bank);

    return true;
}

bool T20_makeFilterCoeffs(const ST_T20_Config_t* p_cfg, float* p_coeffs_out)
{
    if (p_cfg == nullptr || p_coeffs_out == nullptr) {
        return false;
    }

    if (!p_cfg->preprocess.filter.enable ||
        p_cfg->preprocess.filter.type == EN_T20_FILTER_OFF) {
        memset(p_coeffs_out, 0, sizeof(float) * 5U);
        return true;
    }

    const float fs = p_cfg->feature.sample_rate_hz;
    const float q  = (p_cfg->preprocess.filter.q_factor <= 0.0f)
        ? 0.707f
        : p_cfg->preprocess.filter.q_factor;

    esp_err_t res = ESP_OK;

    if (p_cfg->preprocess.filter.type == EN_T20_FILTER_LPF) {
        const float norm = p_cfg->preprocess.filter.cutoff_hz_1 / fs;
        res = dsps_biquad_gen_lpf_f32(p_coeffs_out, norm, q);
    }
    else if (p_cfg->preprocess.filter.type == EN_T20_FILTER_HPF) {
        const float norm = p_cfg->preprocess.filter.cutoff_hz_1 / fs;
        res = dsps_biquad_gen_hpf_f32(p_coeffs_out, norm, q);
    }
    else {
        const float low  = p_cfg->preprocess.filter.cutoff_hz_1;
        const float high = p_cfg->preprocess.filter.cutoff_hz_2;

        if (high <= low) {
            return false;
        }

        const float center_hz   = sqrtf(low * high);
        const float bw_hz       = high - low;
        float q_bpf             = center_hz / (bw_hz + G_T20_EPSILON);
        const float center_norm = center_hz / fs;

        if (q_bpf < 0.1f) {
            q_bpf = 0.1f;
        }

        res = dsps_biquad_gen_bpf_f32(p_coeffs_out, center_norm, q_bpf);
    }

    return (res == ESP_OK);
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
    if (p == nullptr) {
        return;
    }

    for (int i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->window[i] =
            0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)i) / (float)(G_T20_FFT_SIZE - 1));
    }
}

void T20_buildMelFilterbankFromConfig(
    const ST_T20_Config_t* p_cfg,
    float p_mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1])
{
    if (p_cfg == nullptr || p_mel_bank == nullptr) {
        return;
    }

    memset(p_mel_bank,
           0,
           sizeof(float) * G_T20_MEL_FILTERS * ((G_T20_FFT_SIZE / 2) + 1U));

    const int   num_bins = (G_T20_FFT_SIZE / 2) + 1;
    const float f_min = 0.0f;
    const float f_max = p_cfg->feature.sample_rate_hz * 0.5f;
    const int   mel_filters = (int)p_cfg->feature.mel_filters;

    float mel_min = T20_hzToMel(f_min);
    float mel_max = T20_hzToMel(f_max);

    float mel_points[G_T20_MEL_FILTERS + 2];
    float hz_points[G_T20_MEL_FILTERS + 2];
    int   bin_points[G_T20_MEL_FILTERS + 2];

    for (int i = 0; i < mel_filters + 2; ++i) {
        const float ratio = (float)i / (float)(mel_filters + 1);
        mel_points[i] = mel_min + (mel_max - mel_min) * ratio;
        hz_points[i] = T20_melToHz(mel_points[i]);

        int bin = (int)floorf(((float)G_T20_FFT_SIZE + 1.0f) * hz_points[i] / p_cfg->feature.sample_rate_hz);
        if (bin < 0) bin = 0;
        if (bin >= num_bins) bin = num_bins - 1;
        bin_points[i] = bin;
    }

    for (int m = 0; m < mel_filters; ++m) {
        int left   = bin_points[m];
        int center = bin_points[m + 1];
        int right  = bin_points[m + 2];

        if (center <= left) center = left + 1;
        if (right <= center) right = center + 1;
        if (right >= num_bins) right = num_bins - 1;

        for (int k = left; k < center; ++k) {
            p_mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        }

        for (int k = center; k <= right; ++k) {
            p_mel_bank[m][k] = (float)(right - k) / (float)(right - center + 1e-6f);
        }
    }
}

void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    if (p_data == nullptr || p_len == 0U) {
        return;
    }

    float mean = 0.0f;
    for (uint16_t i = 0; i < p_len; ++i) {
        mean += p_data[i];
    }
    mean /= (float)p_len;

    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] -= mean;
    }
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p,
                          float* p_data,
                          uint16_t p_len,
                          float p_alpha)
{
    if (p == nullptr || p_data == nullptr || p_len == 0U) {
        return;
    }

    float prev = p->prev_raw_sample;

    for (uint16_t i = 0; i < p_len; ++i) {
        const float cur = p_data[i];
        p_data[i] = cur - (p_alpha * prev);
        prev = cur;
    }

    p->prev_raw_sample = prev;
}

void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs)
{
    if (p_data == nullptr || p_len == 0U) {
        return;
    }

    for (uint16_t i = 0; i < p_len; ++i) {
        if (fabsf(p_data[i]) < p_threshold_abs) {
            p_data[i] = 0.0f;
        }
    }
}

void T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                           float* p_coeffs,
                           float* p_state,
                           const float* p_in,
                           float* p_out,
                           uint16_t p_len)
{
    if (p_cfg == nullptr || p_coeffs == nullptr || p_state == nullptr ||
        p_in == nullptr || p_out == nullptr) {
        return;
    }

    if (!p_cfg->preprocess.filter.enable ||
        p_cfg->preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    dsps_biquad_f32(p_in, p_out, (int)p_len, p_coeffs, p_state);
}

void T20_applyWindowWithTable(const float* p_window, float* p_data, uint16_t p_len)
{
    if (p_window == nullptr || p_data == nullptr || p_len == 0U) {
        return;
    }

    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] *= p_window[i];
    }
}

void T20_computePowerSpectrumSnapshot(ST_T20_DspSnapshot_t* p_snap,
                                      const float* p_time,
                                      float* p_power)
{
    if (p_snap == nullptr || p_time == nullptr || p_power == nullptr) {
        return;
    }

    for (int i = 0; i < G_T20_FFT_SIZE; ++i) {
        p_snap->fft_buffer[2 * i + 0] = p_time[i];
        p_snap->fft_buffer[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_fc32(p_snap->fft_buffer, G_T20_FFT_SIZE);
    dsps_bit_rev_fc32(p_snap->fft_buffer, G_T20_FFT_SIZE);

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        const float re = p_snap->fft_buffer[2 * k + 0];
        const float im = p_snap->fft_buffer[2 * k + 1];
        float pw = (re * re + im * im) / (float)G_T20_FFT_SIZE;

        if (pw < G_T20_EPSILON) {
            pw = G_T20_EPSILON;
        }

        p_power[k] = pw;
    }
}

void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p,
                            const ST_T20_Config_t* p_cfg,
                            const float* p_power)
{
    if (p == nullptr || p_cfg == nullptr || p_power == nullptr) {
        return;
    }

    if (!p_cfg->preprocess.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames >= p_cfg->preprocess.noise.noise_learn_frames) {
        return;
    }

    const uint16_t count = p->noise_learned_frames;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        p->noise_spectrum[k] =
            ((p->noise_spectrum[k] * (float)count) + p_power[k]) / (float)(count + 1U);
    }

    p->noise_learned_frames++;
}

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p,
                                  const ST_T20_Config_t* p_cfg,
                                  float* p_power)
{
    if (p == nullptr || p_cfg == nullptr || p_power == nullptr) {
        return;
    }

    if (!p_cfg->preprocess.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames < p_cfg->preprocess.noise.noise_learn_frames) {
        return;
    }

    const float strength = p_cfg->preprocess.noise.spectral_subtract_strength;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        float sub = p_power[k] - (strength * p->noise_spectrum[k]);
        if (sub < G_T20_EPSILON) {
            sub = G_T20_EPSILON;
        }
        p_power[k] = sub;
    }
}

void T20_applyMelFilterbankSnapshot(ST_T20_DspSnapshot_t* p_snap,
                                    const float* p_power,
                                    float* p_log_mel_out)
{
    if (p_snap == nullptr || p_power == nullptr || p_log_mel_out == nullptr) {
        return;
    }

    const int num_bins = (G_T20_FFT_SIZE / 2) + 1;
    const int mel_filters = (int)p_snap->cfg.feature.mel_filters;

    for (int m = 0; m < mel_filters; ++m) {
        float sum = 0.0f;

        for (int k = 0; k < num_bins; ++k) {
            sum += p_power[k] * p_snap->mel_bank[m][k];
        }

        if (sum < G_T20_EPSILON) {
            sum = G_T20_EPSILON;
        }

        p_log_mel_out[m] = logf(sum);
    }
}

void T20_computeDCT2(const float* p_in,
                     float* p_out,
                     uint16_t p_in_len,
                     uint16_t p_out_len)
{
    if (p_in == nullptr || p_out == nullptr || p_in_len == 0U || p_out_len == 0U) {
        return;
    }

    for (uint16_t n = 0; n < p_out_len; ++n) {
        float sum = 0.0f;
        for (uint16_t k = 0; k < p_in_len; ++k) {
            sum += p_in[k] * cosf((G_T20_PI / (float)p_in_len) * ((float)k + 0.5f) * (float)n);
        }
        p_out[n] = sum;
    }
}

void T20_computeMFCC_Snapshot(CL_T20_Mfcc::ST_Impl* p,
                              ST_T20_DspSnapshot_t* p_snap,
                              const float* p_frame,
                              float* p_mfcc_out)
{
    if (p == nullptr || p_snap == nullptr || p_frame == nullptr || p_mfcc_out == nullptr) {
        return;
    }

    // single-copy pipeline:
    // raw frame -> temp_frame 으로 1회만 복사
    memcpy(p_snap->temp_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);

    if (p_snap->cfg.preprocess.remove_dc) {
        T20_applyDCRemove(p_snap->temp_frame, G_T20_FFT_SIZE);
    }

    if (p_snap->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p,
                             p_snap->temp_frame,
                             G_T20_FFT_SIZE,
                             p_snap->cfg.preprocess.preemphasis.alpha);
    }

    if (p_snap->cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(p_snap->temp_frame,
                           G_T20_FFT_SIZE,
                           p_snap->cfg.preprocess.noise.gate_threshold_abs);
    }

    T20_applyBiquadFilter(&p_snap->cfg,
                          p_snap->filter_coeffs,
                          p_snap->filter_state,
                          p_snap->temp_frame,
                          p_snap->work_frame,
                          G_T20_FFT_SIZE);

    T20_applyWindowWithTable(p->window, p_snap->work_frame, G_T20_FFT_SIZE);

    T20_computePowerSpectrumSnapshot(p_snap, p_snap->work_frame, p_snap->power);

    T20_learnNoiseSpectrum(p, &p_snap->cfg, p_snap->power);
    T20_applySpectralSubtraction(p, &p_snap->cfg, p_snap->power);

    T20_applyMelFilterbankSnapshot(p_snap, p_snap->power, p_snap->log_mel);

    T20_computeDCT2(p_snap->log_mel,
                    p_mfcc_out,
                    p_snap->cfg.feature.mel_filters,
                    p_snap->cfg.feature.mfcc_coeffs);
}
