/* ============================================================================
 * File: T231_Dsp_Pipeline_217.h
 * Summary: MFCC & DSP 연산 전담 엔진 (v217 Full Version)
 * Description: v216의 동적 필터, 노이즈 게이트, 적응형 학습 기능 완벽 복원
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include "T214_Def_Rec_217.h" // ST_T20_Config_t 정의 포함
#include "esp_dsp.h"

class CL_T20_DspPipeline {
public:
    CL_T20_DspPipeline();
    ~CL_T20_DspPipeline() = default;

    // 엔진 초기화 (프리컴퓨팅 및 필터 생성)
    bool begin(const ST_T20_Config_t& cfg);
    
    // 단일 프레임 처리 (Time Domain -> 39D MFCC Vector)
    bool processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out);

    // 노이즈 학습 제어 (외부 버튼이나 API에서 호출)
    void setNoiseLearning(bool active) { _noise_learning_active = active; }
    void resetNoiseStats() { _noise_learned_frames = 0; }
    bool isNoiseLearning() const { return _noise_learning_active; }

private:
    // v216 전처리 파이프라인 단계별 복원
    void _applyPreprocess(float* p_data);
    void _applyNoiseGate(float* p_data);
    void _applyRuntimeFilter(float* p_data);
    
    // 주파수 도메인 변환 및 특징 추출
    void _computePowerSpectrum(const float* p_time);
    void _learnNoiseSpectrum();
    void _applySpectralSubtraction();
    void _applyMelFilterbank(float* p_log_mel_out);
    void _computeDCT2(const float* p_in, float* p_out);
    
    // 39차원 벡터(Delta, Delta-Delta) 조립
    void _pushHistory(const float* p_mfcc);
    void _computeDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta_out);
    void _computeDeltaDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta2_out);
    void _build39DVector(ST_T20_FeatureVector_t* p_vec_out);

private:
    ST_T20_Config_t _cfg;

    // SIMD 정렬 연산 버퍼
    alignas(16) float _work_frame[T20::C10_DSP::FFT_SIZE];
    alignas(16) float _window[T20::C10_DSP::FFT_SIZE];
    alignas(16) float _power[T20::C10_DSP::FFT_BINS];
    alignas(16) float _noise_spectrum[T20::C10_DSP::FFT_BINS];
    alignas(16) float _log_mel[T20::C10_DSP::MEL_FILTERS];
    alignas(16) float _mel_bank[T20::C10_DSP::MEL_FILTERS][T20::C10_DSP::FFT_BINS];
    alignas(16) float _dct_matrix[T20::C10_DSP::MFCC_COEFFS_MAX][T20::C10_DSP::MEL_FILTERS];
    alignas(16) float _mfcc_history[T20::C10_DSP::MFCC_HISTORY_LEN][T20::C10_DSP::MFCC_COEFFS_MAX];

    // 필터 및 상태 변수
    alignas(16) float _biquad_coeffs[5];
    alignas(16) float _biquad_state[2];
    
    float    _prev_sample;
    uint16_t _history_count;
    uint16_t _noise_learned_frames;
    bool     _noise_learning_active;
};
