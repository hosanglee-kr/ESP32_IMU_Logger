/* ============================================================================
 * File: T231_Dsp_Pipeline_217.h
 * Summary: MFCC & DSP 연산 전담 엔진 (v217)
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include "T212_Def_Sens_217.h" // 기존 Enum 재사용
#include "T214_Def_Rec_217.h" // ST_T20_Config_t 가 정의된 헤더 포함 필수



class CL_T20_DspPipeline {
public:
    CL_T20_DspPipeline();
    ~CL_T20_DspPipeline() = default;

    // 엔진 초기화 (필터 및 윈도우 생성)
    bool begin(const ST_T20_Config_t& cfg);


    // 단일 프레임 처리 (Time Domain -> MFCC Vector)
    bool processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out);

    // 노이즈 학습 제어
    void setNoiseLearning(bool active) { _noise_learning_active = active; }
    void resetNoiseStats() { _noise_learned_frames = 0; }

private:
    // 내부 연산 단계
    void _applyPreprocess(float* p_data);
    void _computePowerSpectrum(const float* p_time);
    void _applyMelFilterbank(float* p_log_mel_out);
    void _computeDCT2(const float* p_in, float* p_out);
    
    // 히스토리 관리 및 특징 벡터 조립
    void _pushHistory(const float* p_mfcc);
    void _build39DVector(ST_T20_FeatureVector_t* p_vec_out);

private:
    ST_T20_Config_t _cfg;
    
    // SIMD 정렬 버퍼 (격리됨)
    alignas(16) float _work_frame[T20::C10_DSP::FFT_SIZE];
    alignas(16) float _window[T20::C10_DSP::FFT_SIZE];
    alignas(16) float _power[T20::C10_DSP::FFT_BINS];
    alignas(16) float _noise_spectrum[T20::C10_DSP::FFT_BINS];
    alignas(16) float _log_mel[T20::C10_DSP::MEL_FILTERS];
    alignas(16) float _mel_bank[T20::C10_DSP::MEL_FILTERS][T20::C10_DSP::FFT_BINS];
    alignas(16) float _dct_matrix[T20::C10_DSP::MFCC_COEFFS_MAX][T20::C10_DSP::MEL_FILTERS];
    alignas(16) float _mfcc_history[T20::C10_DSP::MFCC_HISTORY_LEN][T20::C10_DSP::MFCC_COEFFS_MAX];

    float _biquad_coeffs[5];
    float _biquad_state[2];
    
    // 상태 변수
    float    _prev_sample = 0.0f;
    uint16_t _history_count = 0;
    uint16_t _noise_learned_frames = 0;
    bool     _noise_learning_active = false;
};

