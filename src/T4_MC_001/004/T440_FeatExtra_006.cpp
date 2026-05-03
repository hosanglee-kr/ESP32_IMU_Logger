/* ============================================================================
 * File: T440_FeatExtra_006.cpp
 * Summary: 39D MFCC & Spatial Feature Implementation (Anti-Reduction Verified)
 * * [AI 메모: 보완 및 최적화 사항]
 * 1. IPD 연산 시 원본 파형의 위상 훼손을 막기 위해 Hanning Window 미적용.
 * 2. Spectral Centroid (주파수 무게 중심) 도출 로직 복원.
 * 3. STA/LTA (단기/장기 에너지 비율) 충격음 감지 피처 복원.
 * 4. [메모리 최적화] Cepstrum 연산 시 스택 오버플로우(8KB)를 막기 위해, 
 * 별도 메모리 할당 없이 기 할당된 _fftSpatialL 포인터를 스크래치 버퍼로 재사용.
 * ========================================================================== */

#include "T440_FeatExtra_006.hpp"
#include "esp_log.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"
#include "dspm_mult.h"  // SIMD 행렬 곱셈 가속 API 헤더 추가
#include <cmath>
#include <cstring>
#include <algorithm> 

static const char* TAG = "T440_FEAT";

T440_FeatureExtractor::T440_FeatureExtractor() {
    _historyCount = 0;
    _melBankFlat = nullptr;
    _dctMatrixFlat = nullptr;
    _fftSpatialL = nullptr;
    _fftSpatialR = nullptr;

    memset(_mfccHistory, 0, sizeof(_mfccHistory));
    memset(_deltaHistory, 0, sizeof(_deltaHistory)); 
}

T440_FeatureExtractor::~T440_FeatureExtractor() {
    if (_melBankFlat) heap_caps_free(_melBankFlat);
    if (_dctMatrixFlat) heap_caps_free(_dctMatrixFlat);
    if (_fftSpatialL) heap_caps_free(_fftSpatialL);
    if (_fftSpatialR) heap_caps_free(_fftSpatialR);
}

bool T440_FeatureExtractor::init() {
    // 1. 거대 텐서 배열 PSRAM 할당 (16바이트 정렬 엄수)
    _melBankFlat = (float*)heap_caps_aligned_alloc(16, BINS_PADDED * MEL_PADDED * sizeof(float), MALLOC_CAP_SPIRAM);
    _dctMatrixFlat = (float*)heap_caps_aligned_alloc(16, MEL_PADDED * MFCC_PADDED * sizeof(float), MALLOC_CAP_SPIRAM);
    
    // [스택 오버플로우 방어] 공간 위상 분석용 16KB 텐서를 PSRAM으로 회피
    _fftSpatialL = (float*)heap_caps_aligned_alloc(16, SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    _fftSpatialR = (float*)heap_caps_aligned_alloc(16, SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_SPIRAM);

    if (!_melBankFlat || !_dctMatrixFlat || !_fftSpatialL || !_fftSpatialR) {
        ESP_LOGE(TAG, "Matrix allocation failed!");
        return false;
    }

    memset(_melBankFlat, 0, BINS_PADDED * MEL_PADDED * sizeof(float));
    memset(_dctMatrixFlat, 0, MEL_PADDED * MFCC_PADDED * sizeof(float));
    memset(_noiseProfile, 0, sizeof(_noiseProfile)); 

    // 2. Hann Window 생성
    dsps_wind_hann_f32(_window, SmeaConfig::System::FFT_SIZE_CONST);

    // 3. Mel Filterbank 생성 (42kHz 대역폭 고려)
    float v_fs = (float)SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_minMel = 0.0f;
    float v_maxMel = 2595.0f * log10f(1.0f + ((v_fs / 2.0f) / 700.0f));
    float v_melStep = (v_maxMel - v_minMel) / (float)(SmeaConfig::System::MEL_BANDS_CONST + 1);

    uint16_t v_binPoints[SmeaConfig::System::MEL_BANDS_CONST + 2];
    for (int i = 0; i < SmeaConfig::System::MEL_BANDS_CONST + 2; i++) {
        float v_hz = 700.0f * (powf(10.0f, (v_minMel + i * v_melStep) / 2595.0f) - 1.0f);
        v_binPoints[i] = (uint16_t)roundf(SmeaConfig::System::FFT_SIZE_CONST * v_hz / v_fs);
    }
    
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    for (int m = 0; m < SmeaConfig::System::MEL_BANDS_CONST; m++) {
        uint16_t v_left = v_binPoints[m], v_center = v_binPoints[m + 1], v_right = v_binPoints[m + 2];
        for (int k = v_left; k < v_center && k < v_bins; k++)
            _melBankFlat[k * MEL_PADDED + m] = (float)(k - v_left) / (float)(v_center - v_left + 1e-12f);
        for (int k = v_center; k < v_right && k < v_bins; k++)
            _melBankFlat[k * MEL_PADDED + m] = (float)(v_right - k) / (float)(v_right - v_center + 1e-12f);
    }

    // 4. DCT-II 행렬 생성
    for (int k = 0; k < SmeaConfig::System::MEL_BANDS_CONST; k++) {
        for (int n = 0; n < SmeaConfig::System::MFCC_COEFFS_CONST; n++) {
            _dctMatrixFlat[k * MFCC_PADDED + n] = cosf(((float)M_PI / (float)SmeaConfig::System::MEL_BANDS_CONST) * (k + 0.5f) * n); 
        }
    }

    _historyCount = 0;
    _noiseLearnedFrames = 0;
    _isNoiseLearning = false;
    return true;
}


void T440_FeatureExtractor::extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                                    uint32_t p_len, SmeaType::FeatureSlot& p_outSlot) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;

    _computeBasicFeatures(p_cleanSignal, p_len, p_outSlot);
    _computePowerSpectrum(p_cleanSignal, p_len);
    _computeBandRMS(p_outSlot); 
    _computeSpectralCentroid(p_outSlot);
    _computeNtopPeaks(p_outSlot);
    _computeMfcc39(p_outSlot);
    _computeCepstrum(p_outSlot);
    _computeSpatialFeatures(p_rawL, p_rawR, p_len, p_outSlot);
}

void T440_FeatureExtractor::_computeBasicFeatures(const float* p_signal, uint32_t p_len, SmeaType::FeatureSlot& p_slot) {
    float v_sumSq = 0.0f;
    float v_peak = 0.0f;
    float v_staEnergy = 0.0f;
    float v_ltaEnergy = 0.0f;
    
    // 첨도(Kurtosis) 연산을 위한 평균(Mean) 계산
    float v_sum = 0.0f;
    for (uint32_t i = 0; i < p_len; i++) v_sum += p_signal[i];
    float v_mean = v_sum / p_len;

    float v_varianceSum = 0.0f;
    float v_moment4Sum = 0.0f;

    for (uint32_t i = 0; i < p_len; i++) {
        float v_val = fabsf(p_signal[i]);
        v_sumSq += v_val * v_val;
        if (v_val > v_peak) v_peak = v_val;
        
        if (i < 42) v_staEnergy += v_val * v_val;
        if (i < 420) v_ltaEnergy += v_val * v_val;

        // 분산 및 4차 모멘트 누적
        float v_diff = p_signal[i] - v_mean;
        float v_diff2 = v_diff * v_diff;
        v_varianceSum += v_diff2;
        v_moment4Sum += v_diff2 * v_diff2;
    }

    p_slot.rms = sqrtf(fmaxf(v_sumSq / p_len, 1e-12f));
    p_slot.energy = v_sumSq / (float)SmeaConfig::System::SAMPLING_RATE_CONST;
    p_slot.crest_factor = p_slot.rms > 0 ? (v_peak / p_slot.rms) : 0.0f;
    p_slot.sta_lta_ratio = v_ltaEnergy > 0 ? (v_staEnergy / (v_ltaEnergy / 10.0f)) : 1.0f;

    // Kurtosis 계산 (정규분포=3. 3보다 크면 뾰족한 임펄스 존재)
    float v_variance = v_varianceSum / p_len;
    float v_moment4 = v_moment4Sum / p_len;
    p_slot.kurtosis = (v_variance > 1e-12f) ? (v_moment4 / (v_variance * v_variance)) : 0.0f;

    // 분산의 제곱근을 통해 표준편차(StdDev) 정상 도출 (Rule 판정용)
    p_slot.pooling_stddev_min = sqrtf(fmaxf(v_variance, 1e-12f)); 
}


void T440_FeatureExtractor::_computePowerSpectrum(const float* p_signal, uint32_t p_len) {
    uint16_t v_bins = (p_len / 2) + 1;

    for (uint32_t i = 0; i < p_len; i++) {
        _fftWorkBuf[i * 2]     = p_signal[i] * _window[i];
        _fftWorkBuf[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(_fftWorkBuf, p_len);
    dsps_bit_rev2r_fc32(_fftWorkBuf, p_len);

    float v_scale = 1.0f / (float)p_len;
    for (uint32_t i = 0; i < v_bins; i++) {
        float v_re = _fftWorkBuf[i * 2] * v_scale;
        float v_im = _fftWorkBuf[i * 2 + 1] * v_scale;
        _powerSpectrum[i] = fmaxf((v_re * v_re + v_im * v_im), 1e-12f);
    }

    // 런타임 설정 가져오기 (매 추출 시 최신 값 반영)
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // ANC: 백그라운드 노이즈 프로파일링 학습 및 스펙트럼 감산
    if (_isNoiseLearning || _noiseLearnedFrames < v_cfg.dsp.noise_learn_frames) {
        float v_count = (float)_noiseLearnedFrames;
        for (uint16_t i = 0; i < v_bins; i++) {
            _noiseProfile[i] = ((_noiseProfile[i] * v_count) + _powerSpectrum[i]) / (v_count + 1.0f);
        }
        if (_noiseLearnedFrames < 0xFFFF) _noiseLearnedFrames++;
    }

    if (!_isNoiseLearning && _noiseLearnedFrames > 0) {
        for (uint16_t i = 0; i < v_bins; i++) {
            _powerSpectrum[i] = fmaxf(_powerSpectrum[i] - (_noiseProfile[i] * v_cfg.dsp.spectral_sub_gain), 1e-12f);
        }
    }
}

void T440_FeatureExtractor::_computeSpectralCentroid(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;
    
    float v_weightedSum = 0.0f;
    float v_energySum = 0.0f;

    for (uint16_t i = 0; i < v_bins; i++) {
        float v_freq = i * v_binRes;
        v_weightedSum += v_freq * _powerSpectrum[i];
        v_energySum += _powerSpectrum[i];
    }

    if (v_energySum > 1e-12f) {
        p_slot.spectral_centroid = v_weightedSum / v_energySum;
    } else {
        p_slot.spectral_centroid = 0.0f;
    }
}

void T440_FeatureExtractor::_computeNtopPeaks(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;

    SmeaType::SpectralPeak v_candidates[128];
    uint16_t v_candCount = 0;

    for (uint16_t i = 1; i < v_bins - 1 && v_candCount < 128; i++) {
        if (_powerSpectrum[i] > _powerSpectrum[i - 1] && _powerSpectrum[i] > _powerSpectrum[i + 1]) {
            v_candidates[v_candCount].frequency = i * v_binRes;
            v_candidates[v_candCount].amplitude = _powerSpectrum[i];
            v_candCount++;
        }
    }

    for (int i = 0; i < v_candCount - 1; i++) {
        for (int j = 0; j < v_candCount - i - 1; j++) {
            if (v_candidates[j].amplitude < v_candidates[j + 1].amplitude) {
                SmeaType::SpectralPeak temp = v_candidates[j];
                v_candidates[j] = v_candidates[j + 1];
                v_candidates[j + 1] = temp;
            }
        }
    }

    for (int i = 0; i < 5; i++) {
        if (i < v_candCount) p_slot.top_peaks[i] = v_candidates[i];
        else p_slot.top_peaks[i] = {0.0f, 0.0f};
    }
}

void T440_FeatureExtractor::_computeMfcc39(SmeaType::FeatureSlot& p_slot) {
    alignas(16) float v_melEnergy[MEL_PADDED] = {0}; 
    dspm_mult_f32(_powerSpectrum, _melBankFlat, v_melEnergy, 1, BINS_PADDED, MEL_PADDED);

    for (int m = 0; m < SmeaConfig::System::MEL_BANDS_CONST; m++) {
        v_melEnergy[m] = logf(fmaxf(v_melEnergy[m], 1e-12f));
    }

    alignas(16) float v_dctOut[MFCC_PADDED] = {0}; 
    dspm_mult_f32(v_melEnergy, _dctMatrixFlat, v_dctOut, 1, MEL_PADDED, MFCC_PADDED);
    
    memcpy(p_slot.mfcc, v_dctOut, SmeaConfig::System::MFCC_COEFFS_CONST * sizeof(float));

    _applyTemporalDerivatives(p_slot);
}


void T440_FeatureExtractor::_applyTemporalDerivatives(SmeaType::FeatureSlot& p_slot) {
    float* p_mfcc39 = p_slot.mfcc;
    uint16_t v_dim = SmeaConfig::System::MFCC_COEFFS_CONST;
    
    // MFCC & RMS 히스토리 링버퍼 밀어내기
    if (_historyCount < 5) {
        memcpy(_mfccHistory[_historyCount], p_mfcc39, sizeof(float) * v_dim);
        _rmsHistory[_historyCount] = p_slot.rms;
        _historyCount++;
    } else {
        for (int i = 0; i < 4; i++) memcpy(_mfccHistory[i], _mfccHistory[i + 1], sizeof(float) * v_dim);
        memcpy(_mfccHistory[4], p_mfcc39, sizeof(float) * v_dim);
        _rmsHistory[4] = p_slot.rms;
    }

    if (_historyCount >= 5) {
        // MFCC Delta 연산
        // N=2 가중합 공식: d[t] = ((c[t+1] - c[t-1]) + 2*(c[t+2] - c[t-2])) / 10.0f
        for (int i = 0; i < v_dim; i++) {
            float v_delta = ((_mfccHistory[3][i] - _mfccHistory[1][i]) + 2.0f * (_mfccHistory[4][i] - _mfccHistory[0][i])) / 10.0f;
            p_mfcc39[v_dim + i] = v_delta;
        }

        // Delta-Delta를 위한 Delta 히스토리 밀어내기
        for (int i = 0; i < 4; i++) memcpy(_deltaHistory[i], _deltaHistory[i + 1], sizeof(float) * v_dim);
        memcpy(_deltaHistory[4], p_mfcc39 + v_dim, sizeof(float) * v_dim);

        // N=2 기반 Delta-Delta 연산
        for (int i = 0; i < v_dim; i++) {
            float v_deltaDelta = ((_deltaHistory[3][i] - _deltaHistory[1][i]) + 2.0f * (_deltaHistory[4][i] - _deltaHistory[0][i])) / 10.0f;
            p_mfcc39[v_dim * 2 + i] = v_deltaDelta;
        }
        
        // RMS Delta & Delta-Delta 연산
        p_slot.delta_rms = (_rmsHistory[4] - _rmsHistory[2]) / 2.0f;
        p_slot.delta_delta_rms = _rmsHistory[4] - (2.0f * _rmsHistory[3]) + _rmsHistory[2];
        
    } else {
        memset(p_mfcc39 + v_dim, 0, v_dim * 2 * sizeof(float));
    }
}


void T440_FeatureExtractor::_computeCepstrum(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    
    // [메모리 최적화] 스택 오버플로우(8KB) 방지를 위해, 
    // 직후에 공간 분석에서 사용될 PSRAM 버퍼(_fftSpatialL)를 임시 스크래치용으로 재사용
    float* v_cepsWorkBuf = _fftSpatialL;
    
    for (uint16_t i = 0; i < SmeaConfig::System::FFT_SIZE_CONST; i++) {
        if (i < v_bins) {
            v_cepsWorkBuf[i * 2] = logf(fmaxf(_powerSpectrum[i], 1e-12f)); 
        } else {
            v_cepsWorkBuf[i * 2] = v_cepsWorkBuf[(SmeaConfig::System::FFT_SIZE_CONST - i) * 2]; 
        }
        v_cepsWorkBuf[i * 2 + 1] = 0.0f; 
    }

    dsps_fft2r_fc32(v_cepsWorkBuf, SmeaConfig::System::FFT_SIZE_CONST);
    dsps_bit_rev2r_fc32(v_cepsWorkBuf, SmeaConfig::System::FFT_SIZE_CONST);

    float v_targets[4] = {0.00833f, 0.01666f, 0.02500f, 0.03333f};
    float v_quefRes = 1.0f / (float)SmeaConfig::System::SAMPLING_RATE_CONST; 

    for (int n = 0; n < 4; n++) {
        int v_startIdx = fmaxf(0, (v_targets[n] - 0.0003f) / v_quefRes);
        int v_endIdx = fminf(SmeaConfig::System::FFT_SIZE_CONST / 2, (v_targets[n] + 0.0003f) / v_quefRes);

        float v_maxVal = 0.0f;
        float v_sumSq = 0.0f;
        int v_count = 0;

        for (int i = v_startIdx; i <= v_endIdx; i++) {
            float v_cepsVal = fabsf(v_cepsWorkBuf[i * 2] / SmeaConfig::System::FFT_SIZE_CONST);
            if (v_cepsVal > v_maxVal) v_maxVal = v_cepsVal;
            v_sumSq += v_cepsVal * v_cepsVal;
            v_count++;
        }

        p_slot.cpsr_max[n] = v_maxVal;
        float v_rms = v_count > 0 ? sqrtf(v_sumSq / v_count) : 1e-9f;
        p_slot.cpsr_mxrms[n] = v_maxVal / fmaxf(v_rms, 1e-9f); 
    }
}

void T440_FeatureExtractor::_computeSpatialFeatures(const float* p_L, const float* p_R, uint32_t p_len, SmeaType::FeatureSlot& p_slot) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;

    for (uint32_t i = 0; i < p_len; i++) {
        // 위상차 분석 시 양 극단 위상 훼손을 막기 위해 _window 곱셈을 완전히 제거
        _fftSpatialL[i * 2] = p_L[i];
        _fftSpatialL[i * 2 + 1] = 0.0f;
        _fftSpatialR[i * 2] = p_R[i];
        _fftSpatialR[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(_fftSpatialL, SmeaConfig::System::FFT_SIZE_CONST);
    dsps_bit_rev2r_fc32(_fftSpatialL, SmeaConfig::System::FFT_SIZE_CONST);
    dsps_fft2r_fc32(_fftSpatialR, SmeaConfig::System::FFT_SIZE_CONST);
    dsps_bit_rev2r_fc32(_fftSpatialR, SmeaConfig::System::FFT_SIZE_CONST);

    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_sumIpd = 0.0f;
    float v_coherenceSum = 0.0f;
    float v_energySum = 0.0f;

    uint16_t v_startBin = (uint16_t)(100.0f * SmeaConfig::System::FFT_SIZE_CONST / SmeaConfig::System::SAMPLING_RATE_CONST);
    uint16_t v_endBin = (uint16_t)(4000.0f * SmeaConfig::System::FFT_SIZE_CONST / SmeaConfig::System::SAMPLING_RATE_CONST);
    if (v_endBin > v_bins) v_endBin = v_bins;

    for (uint16_t i = v_startBin; i < v_endBin; i++) {
        float v_reL = _fftSpatialL[i * 2], v_imL = _fftSpatialL[i * 2 + 1];
        float v_reR = _fftSpatialR[i * 2], v_imR = -_fftSpatialR[i * 2 + 1]; 

        float v_crossRe = v_reL * v_reR - v_imL * v_imR;
        float v_crossIm = v_reL * v_imR + v_imL * v_reR;

        float v_phaseDiff = atan2f(v_crossIm, v_crossRe);
        float v_binEnergy = (v_reL*v_reL + v_imL*v_imL) + (v_reR*v_reR + v_imR*v_imR);
        
        v_sumIpd += fabsf(v_phaseDiff) * v_binEnergy;
        v_coherenceSum += cosf(v_phaseDiff) * v_binEnergy; 
        v_energySum += v_binEnergy;
    }

    if (v_energySum > 1e-6f) {
        p_slot.mean_ipd = v_sumIpd / v_energySum;
        p_slot.phase_coherence = (v_coherenceSum / v_energySum + 1.0f) / 2.0f;
    } else {
        p_slot.mean_ipd = 0.0f;
        p_slot.phase_coherence = 1.0f;
    }
}


// Band RMS 구현부
void T440_FeatureExtractor::_computeBandRMS(SmeaType::FeatureSlot& p_slot) {
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;
    uint16_t v_maxBin = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;

    // 런타임 설정 가져오기 (매 추출 시 최신 값 반영)
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    for (int b = 0; b < v_cfg.feature.band_rms_count; b++) {
        float v_startHz = v_cfg.feature.band_ranges[b][0];
        float v_endHz = v_cfg.feature.band_ranges[b][1];
        
        int v_startBin = (int)(v_startHz / v_binRes);
        int v_endBin = (int)(v_endHz / v_binRes);
        
        float v_sumSq = 0.0f;
        int v_count = 0;
        
        for (int i = v_startBin; i <= v_endBin && i < v_maxBin; i++) {
            v_sumSq += _powerSpectrum[i]; 
            v_count++;
        }
        p_slot.band_rms[b] = v_count > 0 ? sqrtf(v_sumSq / v_count) : 0.0f;
    }
}
