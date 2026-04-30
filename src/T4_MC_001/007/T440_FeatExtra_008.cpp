/* ============================================================================
 * File: T440_FeatExtra_008.cpp
 * Summary: 39D MFCC, Cepstrum & Spatial Feature Implementation
 * ============================================================================
 * * [AI 메모: 제공 기능 요약]
 * 1. 시간 도메인, 주파수 스펙트럼, 위상 공간 차이(IPD), 켑스트럼을 아우르는 
 * 산업용 AI 예지보전 핵심 척도 40여 종 추출 파이프라인.
 * 2. 배경 소음 능동 학습(ANC) 및 스펙트럼 감산을 통한 신호 순도 극대화.
 * 3. 5프레임 링버퍼(N=2)를 활용한 실시간 MFCC 미분(Delta, Delta-Delta) 가속도 연산.
 * ============================================================================
 * * [AI 메모: 핵심 구현 원칙, 방어 로직 및 AI 셀프 회고 바이블]
 * 1. [방어] 거대 버퍼 지역 변수 선언 금지: RTOS Stack Overflow 방지를 위해 
 * 수 KB에 달하는 FFT 워크 버퍼는 init() 시 MALLOC_CAP_SPIRAM으로 힙에 할당한다.
 * 2. [주의] 위상 보존 원칙: 공간 차이(IPD, Coherence) 연산 시에는 원본 파형의 
 * 위상 정보 훼손을 막기 위해 Hanning Window를 절대 적용하지 않는다.
 * 3. [방어] 메모리 재사용(Scratch Buffer): Cepstrum 연산 시 OOM을 막기 위해 
 * 독립 버퍼를 만들지 않고 기 할당된 _fftSpatialL 포인터를 스크래치로 재사용한다.
 * 4. [방어] SIMD 정렬 위반 차단: ESP-DSP의 dspm_mult_f32 행렬 곱셈을 호출하기 전, 
 * 모든 입출력 텐서가 반드시 alignas(16)으로 정렬되었는지 컴파일 타임에 보장한다.
 * 5. [방어] 0 나누기(NaN) 확산 차단: sqrtf, logf, 나눗셈 등 발산 위험이 있는 모든 
 * 연산 전후에 fmaxf(val, MATH_EPSILON)을 적용하여 Inf/NaN 오염을 원천 차단한다.
 * ========================================================================== */

#include "T440_FeatExtra_008.hpp"
#include "esp_log.h"
#include "dsps_fft2r.h"
#include "dsps_wind.h"
#include "dspm_mult.h"  
#include <cmath>
#include <cstring>
#include <algorithm> 

static const char* TAG = "T440_FEAT";

// ----------------------------------------------------------------------------
// [기능설명] 생성자: 포인터 및 링버퍼 초기화
// ----------------------------------------------------------------------------
T440_FeatureExtractor::T440_FeatureExtractor() {
    _historyCount = 0;
    _melBankFlat = nullptr;
    _dctMatrixFlat = nullptr;
    _fftSpatialL = nullptr;
    _fftSpatialR = nullptr;

    // [주의/방어] 쓰레기값에 의한 초기 미분 연산 발산을 막기 위해 배열을 0으로 명시적 초기화
    memset(_mfccHistory, 0, sizeof(_mfccHistory));
    memset(_deltaHistory, 0, sizeof(_deltaHistory)); 
}

// ----------------------------------------------------------------------------
// [기능설명] 소멸자: 힙 메모리 누수(Leak) 방지용 해제
// ----------------------------------------------------------------------------
T440_FeatureExtractor::~T440_FeatureExtractor() {
    if (_melBankFlat) heap_caps_free(_melBankFlat);
    if (_dctMatrixFlat) heap_caps_free(_dctMatrixFlat);
    if (_fftSpatialL) heap_caps_free(_fftSpatialL);
    if (_fftSpatialR) heap_caps_free(_fftSpatialR);
}

// ----------------------------------------------------------------------------
// [기능설명] 파이프라인 초기화 및 상수/행렬 사전 계산 (Boot 시 1회 실행)
// ----------------------------------------------------------------------------
bool T440_FeatureExtractor::init() {
    
    // 1. [기능설명] 거대 행렬 및 텐서 배열을 PSRAM에 동적 할당
    // [주의/방어] ESP-DSP SIMD 최적화를 위해 16바이트(128bit) 강제 정렬 할당(aligned_alloc)
    _melBankFlat = (float*)heap_caps_aligned_alloc(16, BINS_PADDED * MEL_PADDED * sizeof(float), MALLOC_CAP_SPIRAM);
    _dctMatrixFlat = (float*)heap_caps_aligned_alloc(16, MEL_PADDED * MFCC_PADDED * sizeof(float), MALLOC_CAP_SPIRAM);
    
    // [주의/방어] 16KB 텐서를 태스크 스택(Stack)에 선언하면 RTOS 패닉이 발생하므로 반드시 힙으로 회피
    _fftSpatialL = (float*)heap_caps_aligned_alloc(16, SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_SPIRAM);
    _fftSpatialR = (float*)heap_caps_aligned_alloc(16, SmeaConfig::System::FFT_SIZE_CONST * 2 * sizeof(float), MALLOC_CAP_SPIRAM);

    // [주의/방어] 메모리 부족(OOM)으로 인한 널 포인터 참조(NullPointerException) 방어
    if (!_melBankFlat || !_dctMatrixFlat || !_fftSpatialL || !_fftSpatialR) {
        ESP_LOGE(TAG, "Matrix allocation failed! (Check PSRAM)");
        return false;
    }

    memset(_melBankFlat, 0, BINS_PADDED * MEL_PADDED * sizeof(float));
    memset(_dctMatrixFlat, 0, MEL_PADDED * MFCC_PADDED * sizeof(float));
    memset(_noiseProfile, 0, sizeof(_noiseProfile)); 

    // 2. [기능설명] 스펙트럼 누출(Leakage) 완화를 위한 Hann Window 사전 생성
    dsps_wind_hann_f32(_window, SmeaConfig::System::FFT_SIZE_CONST);

    // 3. [기능설명] Mel Filterbank 변환 행렬 생성 (2595/700 공식 기반)
    float v_fs = (float)SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_minMel = 0.0f;
    float v_maxMel = SmeaConfig::FeatureLimit::MEL_SCALE_2595_CONST * log10f(1.0f + ((v_fs / 2.0f) / SmeaConfig::FeatureLimit::MEL_SCALE_700_CONST));
    float v_melStep = (v_maxMel - v_minMel) / (float)(SmeaConfig::System::MEL_BANDS_CONST + 1);

    uint16_t v_binPoints[SmeaConfig::System::MEL_BANDS_CONST + 2];
    for (int i = 0; i < SmeaConfig::System::MEL_BANDS_CONST + 2; i++) {
        float v_hz = SmeaConfig::FeatureLimit::MEL_SCALE_700_CONST * (powf(10.0f, (v_minMel + i * v_melStep) / SmeaConfig::FeatureLimit::MEL_SCALE_2595_CONST) - 1.0f);
        v_binPoints[i] = (uint16_t)roundf(SmeaConfig::System::FFT_SIZE_CONST * v_hz / v_fs);
    }
    
    // [주의/방어] 삼각 필터 가중치 계산 시 0 나누기 방어를 위해 MATH_EPSILON_12_CONST 추가
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    for (int m = 0; m < SmeaConfig::System::MEL_BANDS_CONST; m++) {
        uint16_t v_left = v_binPoints[m], v_center = v_binPoints[m + 1], v_right = v_binPoints[m + 2];
        for (int k = v_left; k < v_center && k < v_bins; k++)
            _melBankFlat[k * MEL_PADDED + m] = (float)(k - v_left) / (float)(v_center - v_left + SmeaConfig::System::MATH_EPSILON_12_CONST);
        for (int k = v_center; k < v_right && k < v_bins; k++)
            _melBankFlat[k * MEL_PADDED + m] = (float)(v_right - k) / (float)(v_right - v_center + SmeaConfig::System::MATH_EPSILON_12_CONST);
    }

    // 4. [기능설명] DCT-II (이산 코사인 변환) 행렬 생성 (Mel 에너지 -> MFCC 압축용)
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

// ----------------------------------------------------------------------------
// [기능설명] 특징량 추출 파이프라인 마스터 함수 (매 프레임 호출)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                                    uint32_t p_len, SmeaType::FeatureSlot& p_outSlot) {
    
    // [주의/방어] 입력 버퍼의 길이가 배열 상한선을 초과할 경우 메모리 침범 방지를 위해 즉시 리턴
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;

    _computeBasicFeatures(p_cleanSignal, p_len, p_outSlot);
    _computePowerSpectrum(p_cleanSignal, p_len);
    _computeBandRMS(p_outSlot); 
    _computeSpectralCentroid(p_outSlot);
    _computeNtopPeaks(p_outSlot);
    _computeMfcc39(p_outSlot);
    _computeCepstrum(p_outSlot);
    
    // [주의/방어] 위상 연산은 노이즈 필터링/빔포밍을 거치지 않은 순수 원본(Raw) 채널 파형을 인가해야 함
    _computeSpatialFeatures(p_rawL, p_rawR, p_len, p_outSlot);
}

// ----------------------------------------------------------------------------
// [기능설명] 시간 도메인 기본 통계 지표 추출
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeBasicFeatures(const float* p_signal, uint32_t p_len, SmeaType::FeatureSlot& p_slot) {
    float v_sumSq = 0.0f;
    float v_peak = 0.0f;
    float v_staEnergy = 0.0f;
    float v_ltaEnergy = 0.0f;
    
    float v_sum = 0.0f;
    for (uint32_t i = 0; i < p_len; i++) v_sum += p_signal[i];
    float v_mean = v_sum / p_len;

    float v_varianceSum = 0.0f;
    float v_moment4Sum = 0.0f;

    for (uint32_t i = 0; i < p_len; i++) {
        float v_val = fabsf(p_signal[i]);
        v_sumSq += v_val * v_val;
        if (v_val > v_peak) v_peak = v_val;
        
        // [기능설명] 단기 에너지(충격)와 장기 에너지(배경) 누적 분리
        if (i < SmeaConfig::FeatureLimit::STA_SAMPLES_CONST) v_staEnergy += v_val * v_val;
        if (i < SmeaConfig::FeatureLimit::LTA_SAMPLES_CONST) v_ltaEnergy += v_val * v_val;

        // 첨도(Kurtosis) 연산을 위한 분산 및 4차 모멘트 누적
        float v_diff = p_signal[i] - v_mean;
        float v_diff2 = v_diff * v_diff;
        v_varianceSum += v_diff2;
        v_moment4Sum += v_diff2 * v_diff2;
    }

    // [주의/방어] sqrtf 내부 값이 음수가 되거나 완전히 0이 되어 NaN이 발생하는 것을 차단
    p_slot.rms = sqrtf(fmaxf(v_sumSq / p_len, SmeaConfig::System::MATH_EPSILON_12_CONST));
    p_slot.energy = v_sumSq / (float)SmeaConfig::System::SAMPLING_RATE_CONST;
    
    // [주의/방어] RMS가 0인 극단적인 상황(마이크 고장)에서 0 나누기 방어
    p_slot.crest_factor = p_slot.rms > 0 ? (v_peak / p_slot.rms) : 0.0f;
    
    // [기능설명] 충격음 비율. 장기 에너지(평균) 대비 단기 에너지(현재)가 얼마나 튀어 올랐는지 동적 비율 연산
    float v_ltaRatio = (float)SmeaConfig::FeatureLimit::LTA_SAMPLES_CONST / SmeaConfig::FeatureLimit::STA_SAMPLES_CONST;
    p_slot.sta_lta_ratio = v_ltaEnergy > 0 ? (v_staEnergy / (v_ltaEnergy / v_ltaRatio)) : 1.0f;

    // [기능설명] 첨도 (정규분포 기준 3. 값이 클수록 뾰족한 임펄스성 노이즈 존재 확률 높음)
    float v_variance = v_varianceSum / p_len;
    float v_moment4 = v_moment4Sum / p_len;
    p_slot.kurtosis = (v_variance > SmeaConfig::System::MATH_EPSILON_12_CONST) ? (v_moment4 / (v_variance * v_variance)) : 0.0f;

    p_slot.pooling_stddev_min = sqrtf(fmaxf(v_variance, SmeaConfig::System::MATH_EPSILON_12_CONST)); 
}

// ----------------------------------------------------------------------------
// [기능설명] 주파수 파워 스펙트럼 도출 및 능동 배경소음 감산 (ANC)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computePowerSpectrum(const float* p_signal, uint32_t p_len) {
    uint16_t v_bins = (p_len / 2) + 1;

    // [기능설명] 실수 신호에 Hanning Window를 곱하여 스펙트럼 누출 완화
    for (uint32_t i = 0; i < p_len; i++) {
        _fftWorkBuf[i * 2]     = p_signal[i] * _window[i];
        _fftWorkBuf[i * 2 + 1] = 0.0f; // 복소수 허수부 초기화
    }

    // Radix-2 FFT 수행 및 비트 리버스(정렬)
    dsps_fft2r_fc32(_fftWorkBuf, p_len);
    dsps_bit_rev2r_fc32(_fftWorkBuf, p_len);

    float v_scale = 1.0f / (float)p_len;
    for (uint32_t i = 0; i < v_bins; i++) {
        float v_re = _fftWorkBuf[i * 2] * v_scale;
        float v_im = _fftWorkBuf[i * 2 + 1] * v_scale;
        // 복소수 파워(Magnitude Squared) = Re^2 + Im^2
        _powerSpectrum[i] = fmaxf((v_re * v_re + v_im * v_im), SmeaConfig::System::MATH_EPSILON_12_CONST);
    }

    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // [기능설명] 노이즈 학습 모드: 설정된 프레임 동안 스펙트럼의 시계열 평균값을 누적하여 프로파일 생성
    if (_isNoiseLearning || _noiseLearnedFrames < v_cfg.dsp.noise_learn_frames) {
        float v_count = (float)_noiseLearnedFrames;
        for (uint16_t i = 0; i < v_bins; i++) {
            _noiseProfile[i] = ((_noiseProfile[i] * v_count) + _powerSpectrum[i]) / (v_count + 1.0f);
        }
        if (_noiseLearnedFrames < UINT16_MAX) _noiseLearnedFrames++;
    }

    // [기능설명] 배경소음 차감 (Spectral Subtraction): 현재 파워에서 학습된 소음 파워를 가중치(Gain)만큼 제거
    if (!_isNoiseLearning && _noiseLearnedFrames > 0) {
        for (uint16_t i = 0; i < v_bins; i++) {
            // [주의/방어] 차감 결과가 음수가 되어 파워가 붕괴되는 것을 막기 위해 fmaxf로 하한선 보장
            _powerSpectrum[i] = fmaxf(_powerSpectrum[i] - (_noiseProfile[i] * v_cfg.dsp.spectral_sub_gain), SmeaConfig::System::MATH_EPSILON_12_CONST);
        }
    }
}

// ----------------------------------------------------------------------------
// [기능설명] 스펙트럼 무게 중심 (에너지가 저주파/고주파 중 어디에 믾은지, 소리의 날카로움/밝기를 대변하는 주파수 지표)
// ----------------------------------------------------------------------------
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

    // [주의/방어] 분모(energySum)가 0인 묵음 상태일 때 치명적인 발산 에러(Zero Division) 방어
    if (v_energySum > SmeaConfig::System::MATH_EPSILON_12_CONST) {
        p_slot.spectral_centroid = v_weightedSum / v_energySum;
    } else {
        p_slot.spectral_centroid = 0.0f;
    }
}

// ----------------------------------------------------------------------------
// [기능설명] N개의 핵심 주파수 피크(Top-Peaks) 검출
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// [기능설명] N개의 핵심 주파수 피크(Top-Peaks) 검출
// [보완] Spectral Leakage로 인한 가짜 중복 피크 방어 및 진폭 하한선(Threshold) 필터링 적용
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeNtopPeaks(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;

    // 동적 설정 가져오기
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    
    // [방어/설정] 진폭 하한선 (노이즈 게이트 임계치 재사용 또는 최소치 1e-4f 보장)
    const float v_minAmplitude = fmaxf(v_cfg.dsp.noise_gate_thresh, 0.0001f);
    
    // [방어/설정] 피크 간 최소 이격 거리 (이 거리 내에 있는 피크는 곁가지로 간주하여 무시)
    // (추후 T410_Config의 FeatureLimit 상수로 승격 가능)
    const float MIN_FREQ_DISTANCE_HZ = 50.0f; 

    SmeaType::SpectralPeak v_candidates[SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST];
    uint16_t v_candCount = 0;

    // 1. [기능설명] 지역 극댓값(Local Maxima) 스캔 및 1차 하한선 필터링
    for (uint16_t i = 1; i < v_bins - 1 && v_candCount < SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST; i++) {
        // [방어/보완] 진폭이 하한선(v_minAmplitude) 이하인 무의미한 노이즈 피크는 후보에서 즉시 배제
        if (_powerSpectrum[i] > v_minAmplitude) {
            // 양옆의 Bin보다 현재 Bin의 에너지가 더 크면 극댓값(Peak)으로 판정
            if (_powerSpectrum[i] > _powerSpectrum[i - 1] && _powerSpectrum[i] > _powerSpectrum[i + 1]) {
                v_candidates[v_candCount].frequency = i * v_binRes;
                v_candidates[v_candCount].amplitude = _powerSpectrum[i];
                v_candCount++;
            }
        }
    }

    // 2. [기능설명] 진폭(Amplitude) 기준 내림차순 정렬
    // [메모리 최적화] 데이터 수가 작으므로 <algorithm> 오버헤드 대신 인플레이스 버블 소트 사용
    for (int i = 0; i < v_candCount - 1; i++) {
        for (int j = 0; j < v_candCount - i - 1; j++) {
            if (v_candidates[j].amplitude < v_candidates[j + 1].amplitude) {
                SmeaType::SpectralPeak temp = v_candidates[j];
                v_candidates[j] = v_candidates[j + 1];
                v_candidates[j + 1] = temp;
            }
        }
    }

    // 3. [기능설명] 상위 N개 추출 및 스펙트럼 누출(Spectral Leakage) 중복 방어
    uint8_t v_topCount = 0;
    
    for (int i = 0; i < v_candCount && v_topCount < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
        bool v_isTooClose = false;
        
        // [방어/보완] 현재 검토 중인 피크가 '이미 선택된 더 큰 피크'들과 주파수가 너무 가까운지 검사
        for (int t = 0; t < v_topCount; t++) {
            if (fabsf(v_candidates[i].frequency - p_slot.top_peaks[t].frequency) < MIN_FREQ_DISTANCE_HZ) {
                v_isTooClose = true; // Sidelobe (어깨 피크)로 판정
                break;
            }
        }
        
        // [기능설명] 거리가 충분히 떨어져 있는 독립적인 결함 주파수만 최종 등록
        if (!v_isTooClose) {
            p_slot.top_peaks[v_topCount] = v_candidates[i];
            v_topCount++;
        }
    }

    // 4. [방어] 남은 빈 슬롯이 있다면 0으로 패딩하여 C++ 쓰레기 메모리값 송출 방어
    for (int i = v_topCount; i < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
        p_slot.top_peaks[i] = {0.0f, 0.0f};
    }
}


/*
void T440_FeatureExtractor::_computeNtopPeaks(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;

    SmeaType::SpectralPeak v_candidates[SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST];
    uint16_t v_candCount = 0;

    // 1. [기능설명] 지역 극댓값(Local Maxima) 스캔
    for (uint16_t i = 1; i < v_bins - 1 && v_candCount < SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST; i++) {
        if (_powerSpectrum[i] > _powerSpectrum[i - 1] && _powerSpectrum[i] > _powerSpectrum[i + 1]) {
            v_candidates[v_candCount].frequency = i * v_binRes;
            v_candidates[v_candCount].amplitude = _powerSpectrum[i];
            v_candCount++;
        }
    }

    // 2. [기능설명] 진폭(Amplitude) 기준 내림차순 정렬
    // [메모리 최적화] 데이터 수가 작으므로 <algorithm> 오버헤드 대신 인플레이스 버블 소트 사용
    for (int i = 0; i < v_candCount - 1; i++) {
        for (int j = 0; j < v_candCount - i - 1; j++) {
            if (v_candidates[j].amplitude < v_candidates[j + 1].amplitude) {
                SmeaType::SpectralPeak temp = v_candidates[j];
                v_candidates[j] = v_candidates[j + 1];
                v_candidates[j + 1] = temp;
            }
        }
    }

    // 3. 상위 N개 추출 (없을 경우 0으로 패딩)
    for (int i = 0; i < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
        if (i < v_candCount) p_slot.top_peaks[i] = v_candidates[i];
        else p_slot.top_peaks[i] = {0.0f, 0.0f};
    }
}
*/

// ----------------------------------------------------------------------------
// [기능설명] MFCC (Mel-Frequency Cepstral Coefficients) 추출 (SIMD 행렬 곱 가속)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeMfcc39(SmeaType::FeatureSlot& p_slot) {
    alignas(16) float v_melEnergy[MEL_PADDED] = {0}; 
    
    // [주의/방어] ESP-DSP SIMD 행렬 곱 (파워 스펙트럼 행렬 X 멜 필터뱅크 행렬)
    // 연산 속도를 수십 배 끌어올리나 입력 배열들이 반드시 16바이트 정렬(alignas 16) 되어야만 정상 동작함
    dspm_mult_f32(_powerSpectrum, _melBankFlat, v_melEnergy, 1, BINS_PADDED, MEL_PADDED);

    // 인간의 청각 인지 특성을 모방하기 위한 자연로그(Log) 변환
    // [주의/방어] logf 진입 전 음수 및 0에 의한 -Inf 발산을 fmaxf로 차단
    for (int m = 0; m < SmeaConfig::System::MEL_BANDS_CONST; m++) {
        v_melEnergy[m] = logf(fmaxf(v_melEnergy[m], SmeaConfig::System::MATH_EPSILON_12_CONST));
    }

    alignas(16) float v_dctOut[MFCC_PADDED] = {0}; 
    
    // 스펙트럼 에너지 압축 및 상관성 제거를 위한 이산 코사인 변환 (DCT-II) SIMD 행렬 곱
    dspm_mult_f32(v_melEnergy, _dctMatrixFlat, v_dctOut, 1, MEL_PADDED, MFCC_PADDED);
    
    // Static MFCC 13차원 구조체 복사
    memcpy(p_slot.mfcc, v_dctOut, SmeaConfig::System::MFCC_COEFFS_CONST * sizeof(float));

    // 시간 미분 (Delta, Delta-Delta) 추가 연산을 호출하여 총 39차원 배열 완성
    _applyTemporalDerivatives(p_slot);
}

// ----------------------------------------------------------------------------
// [기능설명] 시간 미분(Temporal Derivatives) 텐서 확장
// 과거 N=2(5프레임) 링버퍼를 참조하여 신호 변화의 속도와 가속도를 MFCC 계수에 병합
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_applyTemporalDerivatives(SmeaType::FeatureSlot& p_slot) {
    float* p_mfcc39 = p_slot.mfcc;
    uint16_t v_dim = SmeaConfig::System::MFCC_COEFFS_CONST;
    
    // 1. [기능설명] Static MFCC 및 RMS 히스토리 링버퍼 삽입 및 슬라이딩 밀어내기
    if (_historyCount < SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST) {
        memcpy(_mfccHistory[_historyCount], p_mfcc39, sizeof(float) * v_dim);
        _rmsHistory[_historyCount] = p_slot.rms;
        _historyCount++;
    } else {
        for (int i = 0; i < SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST - 1; i++) {
            memcpy(_mfccHistory[i], _mfccHistory[i + 1], sizeof(float) * v_dim);
        }
        memcpy(_mfccHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST - 1], p_mfcc39, sizeof(float) * v_dim);
        _rmsHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST - 1] = p_slot.rms;
    }

    // 2. [기능설명] 버퍼가 꽉 찼을 때부터 본격적인 미분 연산 수행
    if (_historyCount >= SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST) {
        
        // 1차 미분 (Delta): N=2 중심 차분 가중합 공식
        for (int i = 0; i < v_dim; i++) {
            float v_delta = ((_mfccHistory[3][i] - _mfccHistory[1][i]) + 2.0f * (_mfccHistory[4][i] - _mfccHistory[0][i])) / 10.0f;
            p_mfcc39[v_dim + i] = v_delta;
        }

        // 2차 미분 (Delta-Delta) 연산을 위해 Delta 배열 자체도 링버퍼로 슬라이딩 보존
        for (int i = 0; i < SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST - 1; i++) {
            memcpy(_deltaHistory[i], _deltaHistory[i + 1], sizeof(float) * v_dim);
        }
        memcpy(_deltaHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST - 1], p_mfcc39 + v_dim, sizeof(float) * v_dim);

        for (int i = 0; i < v_dim; i++) {
            float v_deltaDelta = ((_deltaHistory[3][i] - _deltaHistory[1][i]) + 2.0f * (_deltaHistory[4][i] - _deltaHistory[0][i])) / 10.0f;
            p_mfcc39[v_dim * 2 + i] = v_deltaDelta;
        }
        
        // RMS 파워의 변화율(단순/가속 차분) 별도 계산
        p_slot.delta_rms = (_rmsHistory[4] - _rmsHistory[2]) / 2.0f;
        p_slot.delta_delta_rms = _rmsHistory[4] - (2.0f * _rmsHistory[3]) + _rmsHistory[2];
        
    } else {
        // [방어] 아직 히스토리가 안 채워졌을 때는 미분 구간을 0으로 묵음 패딩 (초기 발산 방어)
        memset(p_mfcc39 + v_dim, 0, v_dim * 2 * sizeof(float));
    }
}

// ----------------------------------------------------------------------------
// [기능설명] 켑스트럼 (Cepstrum) 도출을 통한 회전체(모터) 결함 주기 식별
// 파워 스펙트럼의 로그(Log)를 다시 역푸리에 변환(IFFT) 하여 Quefrency 성분 도출
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeCepstrum(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    
    // [주의/방어/메모리최적화] 8KB에 달하는 IFFT 버퍼를 새로 생성하면 스택 오버플로우 패닉에 빠짐.
    // 따라서 이 시점에서 사용하지 않는 _fftSpatialL 포인터 버퍼를 임시 스크래치 공간으로 100% 재사용함.
    float* v_cepsWorkBuf = _fftSpatialL;
    
    // IFFT 연산을 위한 복소수 버퍼 조립 (로그 파워 스펙트럼 삽입 및 좌우 대칭 미러링)
    for (uint16_t i = 0; i < SmeaConfig::System::FFT_SIZE_CONST; i++) {
        if (i < v_bins) {
            v_cepsWorkBuf[i * 2] = logf(fmaxf(_powerSpectrum[i], SmeaConfig::System::MATH_EPSILON_12_CONST)); 
        } else {
            v_cepsWorkBuf[i * 2] = v_cepsWorkBuf[(SmeaConfig::System::FFT_SIZE_CONST - i) * 2]; 
        }
        v_cepsWorkBuf[i * 2 + 1] = 0.0f; 
    }

    // [기능설명] 로그 스펙트럼에 대한 FFT(실제로는 IFFT 효과) 수행으로 Cepstrum 생성
    dsps_fft2r_fc32(v_cepsWorkBuf, SmeaConfig::System::FFT_SIZE_CONST);
    dsps_bit_rev2r_fc32(v_cepsWorkBuf, SmeaConfig::System::FFT_SIZE_CONST);

    float v_quefRes = 1.0f / (float)SmeaConfig::System::SAMPLING_RATE_CONST; 

    // 웹에서 설정한 결함 주파수 타겟 배열(예: 60Hz, 120Hz)을 순회하며 Cepstrum 진폭 매칭
    for (int n = 0; n < SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST; n++) {
        float v_target = v_cfg.feature.ceps_targets[n];
        
        // 오차율(Tolerance)을 반영한 탐색 인덱스 바운더리
        int v_startIdx = fmaxf(0, (v_target - SmeaConfig::FeatureLimit::CEPS_TOLERANCE_CONST) / v_quefRes);
        int v_endIdx = fminf(SmeaConfig::System::FFT_SIZE_CONST / 2, (v_target + SmeaConfig::FeatureLimit::CEPS_TOLERANCE_CONST) / v_quefRes);

        float v_maxVal = 0.0f;
        float v_sumSq = 0.0f;
        int v_count = 0;

        for (int i = v_startIdx; i <= v_endIdx; i++) {
            float v_cepsVal = fabsf(v_cepsWorkBuf[i * 2] / SmeaConfig::System::FFT_SIZE_CONST);
            if (v_cepsVal > v_maxVal) v_maxVal = v_cepsVal;
            v_sumSq += v_cepsVal * v_cepsVal;
            v_count++;
        }

        // 해당 주파수(RPM) 결함 성분의 돌출(Peak) 정도와 RMS 대비 선명도 저장
        p_slot.cpsr_max[n] = v_maxVal;
        float v_rms = v_count > 0 ? sqrtf(v_sumSq / v_count) : SmeaConfig::System::MATH_EPSILON_12_CONST;
        p_slot.cpsr_mxrms[n] = v_maxVal / fmaxf(v_rms, SmeaConfig::System::MATH_EPSILON_12_CONST); 
    }
}

// ----------------------------------------------------------------------------
// [기능설명] 공간 위상 분석 (IPD & Phase Coherence)
// 다채널 마이크로폰 배열에서 소리의 도달 시간차와 파형 유사도를 비교 (방향성/단일소음원 탐지)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeSpatialFeatures(const float* p_L, const float* p_R, uint32_t p_len, SmeaType::FeatureSlot& p_slot) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // [주의/방어] 공간 위상을 완벽히 보존하기 위해 Hanning Window(가장자리 감쇠 윈도우)를 절대 곱하지 않음.
    // 순수 원본 Raw 신호 포인터를 그대로 Complex 실수부 공간에 매핑.
    for (uint32_t i = 0; i < p_len; i++) {
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

    // 동적 설정된 위상 분석 주파수 대역폭(예: 100Hz ~ 4000Hz) 바운더리 매핑
    uint16_t v_startBin = (uint16_t)(v_cfg.feature.spatial_freq_min_hz * SmeaConfig::System::FFT_SIZE_CONST / SmeaConfig::System::SAMPLING_RATE_CONST);
    uint16_t v_endBin = (uint16_t)(v_cfg.feature.spatial_freq_max_hz * SmeaConfig::System::FFT_SIZE_CONST / SmeaConfig::System::SAMPLING_RATE_CONST);
    if (v_endBin > v_bins) v_endBin = v_bins; // 인덱스 초과 방어

    for (uint16_t i = v_startBin; i < v_endBin; i++) {
        float v_reL = _fftSpatialL[i * 2], v_imL = _fftSpatialL[i * 2 + 1];
        // 복소 공액(Conjugate) 곱을 위해 R채널 허수부 부호 반전
        float v_reR = _fftSpatialR[i * 2], v_imR = -_fftSpatialR[i * 2 + 1]; 

        // Cross-Power Spectrum (상호 파워 스펙트럼) 도출
        float v_crossRe = v_reL * v_reR - v_imL * v_imR;
        float v_crossIm = v_reL * v_imR + v_imL * v_reR;

        // 아크탄젠트(atan2)로 해당 주파수 성분의 위상차(Phase Difference) 라디안 추출
        float v_phaseDiff = atan2f(v_crossIm, v_crossRe);
        float v_binEnergy = (v_reL*v_reL + v_imL*v_imL) + (v_reR*v_reR + v_imR*v_imR);
        
        v_sumIpd += fabsf(v_phaseDiff) * v_binEnergy;
        v_coherenceSum += cosf(v_phaseDiff) * v_binEnergy; 
        v_energySum += v_binEnergy;
    }

    // [주의/방어] 에너지 0(묵음)일 때의 치명적 0 나누기 방어
    if (v_energySum > SmeaConfig::System::MATH_EPSILON_12_CONST) {
        p_slot.mean_ipd = v_sumIpd / v_energySum;
        // 위상 일관성(Coherence)을 0.0 ~ 1.0(완전 일치) 스케일로 정규화
        p_slot.phase_coherence = (v_coherenceSum / v_energySum + 1.0f) / 2.0f;
    } else {
        p_slot.mean_ipd = 0.0f;
        p_slot.phase_coherence = 1.0f;
    }
}

// ----------------------------------------------------------------------------
// [기능설명] Multi-Band RMS 에너지 도출
// 사용자가 웹에서 지정한 임의의 다중 주파수 대역의 에너지를 쪼개서 추출 (에너지 밸런스 체크)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeBandRMS(SmeaType::FeatureSlot& p_slot) {
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;
    uint16_t v_maxBin = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;

    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // 설정된 활성 밴드 개수만큼만 순회하여 연산 부하 절감
    for (int b = 0; b < v_cfg.feature.band_rms_count; b++) {
        float v_startHz = v_cfg.feature.band_ranges[b][0];
        float v_endHz = v_cfg.feature.band_ranges[b][1];
        
        int v_startBin = (int)(v_startHz / v_binRes);
        int v_endBin = (int)(v_endHz / v_binRes);
        
        float v_sumSq = 0.0f;
        int v_count = 0;
        
        // [방어] 인덱스가 파워 스펙트럼 배열 상한을 뚫고 나가지 않도록 바운더리 클램프(Clamp)
        for (int i = v_startBin; i <= v_endBin && i < v_maxBin; i++) {
            v_sumSq += _powerSpectrum[i]; 
            v_count++;
        }
        
        // 지정된 주파수 대역 내 평균 에너지 레벨 계산
        p_slot.band_rms[b] = v_count > 0 ? sqrtf(v_sumSq / v_count) : 0.0f;
    }
}
