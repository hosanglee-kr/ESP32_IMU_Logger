/* ============================================================================
 * File: T340_FeatExtr_001.h & T350_InfEng_001.h 통합 모듈
 * [유형 3 정합성] 1/N 스케일링, 원본 캐시 보존 및 ML 정규화 완벽 구현
 * ========================================================================== */
#pragma once
#include "T310_Config_001.h"
#include "T320_Types_001.h"
#include <esp_dsp.h>
#include <math.h>
#include <algorithm>
#include <string.h>

class FeatureExtractor {
public:
    FeatureExtractor() {
        dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        _fft_buf = (float*)heap_caps_malloc(Config::FFT_SIZE * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
    }

    SignalFeatures extractAll(float* clean_audio, float* trg_audio) {
        SignalFeatures ft = {0};
        
        float amp_db[Config::FFT_SIZE / 2];
        runFFTAnalysis(clean_audio, amp_db);
        ft.sptr_rms_1k_3k = calcBandRMS(amp_db, 1000, 3000);
        ft.sptr_rms_3k_6k = calcBandRMS(amp_db, 3000, 6000);
        ft.sptr_rms_6k_9k = calcBandRMS(amp_db, 6000, 9000);

        float ceps[Config::FFT_SIZE / 2];
        runCepstrumAnalysis(clean_audio, ceps);
        
        float targets[4] = {0.00833f, 0.01666f, 0.025f, 0.03333f};
        for(int i=0; i<4; i++) {
            calcCepsMaxRms(ceps, targets[i], ft.cpsr_max[i], ft.cpsr_mxrms[i]);
        }

        float sum_sq = 0.0f;
        for(int i=0; i<Config::VALID_SAMP_LEN; i++) sum_sq += clean_audio[i] * clean_audio[i];
        ft.rms = sqrtf(sum_sq / Config::VALID_SAMP_LEN);
        ft.energy = sum_sq / Config::SAMPLE_RATE;
        ft.wvfm_stddev = computeMinRollingStdDev(clean_audio, Config::VALID_SAMP_LEN);
        ft.trigger_count = computeSTALTA(trg_audio, Config::TRIG_SAMP_LEN);

        return ft;
    }

private:
    float* _fft_buf;

    void runFFTAnalysis(float* input, float* amp_db_out) {
        for (int i = 0; i < Config::FFT_SIZE; i++) {
            _fft_buf[i * 2 + 0] = input[i]; _fft_buf[i * 2 + 1] = 0.0f;
        }
        dsps_wind_hann_f32(_fft_buf, Config::FFT_SIZE);
        dsps_fft2r_fc32(_fft_buf, Config::FFT_SIZE);
        dsps_bit_rev_fc32(_fft_buf, Config::FFT_SIZE);

        float scale = 2.0f / Config::FFT_SIZE;
        float freq_res = (float)Config::SAMPLE_RATE / Config::FFT_SIZE;
        
        for (int i = 0; i < Config::FFT_SIZE / 2; i++) {
            float re = _fft_buf[i * 2 + 0], im = _fft_buf[i * 2 + 1];
            float mag = sqrtf(re*re + im*im) * scale;
            if (mag < Config::DB_REF) mag = Config::DB_REF;
            amp_db_out[i] = 20.0f * log10f(mag / Config::DB_REF);
        }
    }

    void runCepstrumAnalysis(float* input, float* ceps_out) {
        for (int i = 0; i < Config::FFT_SIZE; i++) {
            _fft_buf[i * 2 + 0] = input[i]; _fft_buf[i * 2 + 1] = 0.0f;
        }
        dsps_fft2r_fc32(_fft_buf, Config::FFT_SIZE);
        dsps_bit_rev_fc32(_fft_buf, Config::FFT_SIZE);

        for (int i = 0; i < Config::FFT_SIZE; i++) {
            float mag = sqrtf(_fft_buf[i*2]*_fft_buf[i*2] + _fft_buf[i*2+1]*_fft_buf[i*2+1]);
            if (mag < Config::EPSILON) mag = Config::EPSILON;
            _fft_buf[i * 2 + 0] = logf(mag);
            _fft_buf[i * 2 + 1] = 0.0f;
        }
        
        for (int k=0; k<Config::FFT_SIZE; k++) _fft_buf[k*2+1] = -_fft_buf[k*2+1];
        dsps_fft2r_fc32(_fft_buf, Config::FFT_SIZE);
        dsps_bit_rev_fc32(_fft_buf, Config::FFT_SIZE);
        for (int k=0; k<Config::FFT_SIZE; k++) _fft_buf[k*2+1] = -_fft_buf[k*2+1];

        float scale = 1.0f / Config::FFT_SIZE;
        for (int i = 0; i < Config::FFT_SIZE / 2; i++) ceps_out[i] = _fft_buf[i * 2 + 0] * scale;
    }

    float calcBandRMS(float* amp_db, float low, float high) {
        float freq_res = (float)Config::SAMPLE_RATE / Config::FFT_SIZE;
        int start_bin = (int)(low / freq_res);
        int end_bin = (int)(high / freq_res);
        float sum_sq = 0.0f; int count = 0;
        for(int i = start_bin; i <= end_bin && i < Config::FFT_SIZE/2; i++) {
            sum_sq += amp_db[i] * amp_db[i]; count++;
        }
        return count > 0 ? sqrtf(sum_sq / count) : 0.0f;
    }

    void calcCepsMaxRms(float* ceps, float target_quef, float& max_val, float& mxrms_val) {
        float quef_res = 1.0f / Config::SAMPLE_RATE;
        int start_bin = (int)((target_quef - 0.0003f) / quef_res);
        int end_bin = (int)((target_quef + 0.0003f) / quef_res);
        
        max_val = -9999.0f; float sum_sq = 0.0f; int count = 0;
        for(int i = std::max(0, start_bin); i <= end_bin && i < Config::FFT_SIZE/2; i++) {
            if (ceps[i] > max_val) max_val = ceps[i];
            sum_sq += ceps[i] * ceps[i]; count++;
        }
        if (max_val == -9999.0f) max_val = 0.0f;
        float rms = count > 0 ? sqrtf(sum_sq / count) : 1e-9f;
        mxrms_val = max_val / rms;
    }

    float computeMinRollingStdDev(float* data, int len) {
        int w_size = 204, stride = 102;
        float min_std = 9999.0f;
        for (int i = 0; i <= len - w_size; i += stride) {
            float sum = 0.0f, sq_sum = 0.0f;
            for (int j = 0; j < w_size; j++) {
                sum += data[i+j]; sq_sum += data[i+j]*data[i+j];
            }
            float var = (sq_sum / w_size) - ((sum/w_size)*(sum/w_size));
            if(var < 0.0f) var = 0.0f;
            float std = sqrtf(var);
            if(std < min_std) min_std = std;
        }
        return min_std;
    }

    int computeSTALTA(float* trg_data, int len) {
        float csta = 1.0f / 42.0f; float clta = 1.0f / 420.0f;
        float sta_val = 0.0f, lta_val = 0.0f;
        int count = 0; bool in_trigger = false;
        for (int i = 0; i < len; i++) {
            float val = fabsf(trg_data[i]);
            sta_val += csta * (val - sta_val);
            lta_val += clta * (val - lta_val);
            float cft = sta_val - 0.05f; 
            if (cft >= 0.0f && !in_trigger) { in_trigger = true; count++; }
            else if (cft < 0.0f) { in_trigger = false; }
        }
        return count;
    }
};

class InferenceEngine {
public:
    InferenceOutput runHybridDecision(const SignalFeatures& ft, int lid) {
        InferenceOutput res;
        memset(_flat_features, 0, sizeof(_flat_features));

        _flat_features[0] = ft.cpsr_max[0]; _flat_features[1] = ft.cpsr_max[1];
        _flat_features[2] = ft.cpsr_max[2]; _flat_features[3] = ft.cpsr_max[3];
        _flat_features[4] = ft.cpsr_mxrms[0]; _flat_features[5] = ft.cpsr_mxrms[1];
        _flat_features[6] = ft.cpsr_mxrms[2]; _flat_features[7] = ft.cpsr_mxrms[3];
        _flat_features[8] = ft.rms;
        _flat_features[9] = ft.sptr_rms_1k_3k;
        _flat_features[10] = ft.sptr_rms_3k_6k;
        _flat_features[11] = ft.sptr_rms_6k_9k;

        for (int i = 0; i < 12; i++) {
            _flat_features[i] = (_flat_features[i] - Config::MLScaler::MEAN[i]) / Config::MLScaler::SCALE[i];
            if (IS_INVALID_FLOAT(_flat_features[i])) _flat_features[i] = 0.0f;
        }

        res.ml_probability = 0.15f; // TFLite 연동부 (포인터 주입 대기)

        res.is_test_ng = (ft.trigger_count < Config::Thresholds::MIN_TRIGGER_COUNT);
        res.is_rule_ng = (ft.energy > Config::Thresholds::ENERGY) || (ft.wvfm_stddev > Config::Thresholds::STDDEV);

        float cutoff = 0.5f;
        for (const auto& l : Config::LID_TABLE) {
            if (l.lid == lid) { cutoff = l.ml_cutoff; break; }
        }
        res.is_ml_ng = (res.ml_probability >= cutoff);

        if (res.is_test_ng) res.final_r = DecisionResult::TEST_FAIL;
        else if (res.is_rule_ng) res.final_r = DecisionResult::RULE_NG;
        else res.final_r = res.is_ml_ng ? DecisionResult::ML_NG : DecisionResult::GOOD;

        return res;
    }
private:
    float _flat_features[12];
};
