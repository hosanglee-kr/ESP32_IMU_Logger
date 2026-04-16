/* ============================================================================
 * File: T231_Dsp_Pipeline_221.h
 * Summary: Advanced MFCC & DSP Pipeline Engine (v220.020 / SIMD + Matrix Optimized)
 * * [AI 메모: 제공 기능 요약]
 * 1. ESP32-S3 SIMD 및 Matrix API(dspm_mult)를 활용한 초고속 행렬 연산 가속.
 * 2. 다중 필터(Median, Cascade IIR, Notch) 및 가변 Window 함수 완벽 지원.
 * 3. 16바이트 정렬(Alignment) 예외 처리 및 NaN/Inf 묵음 처리를 통한 무결점 안정성 보장.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. [정렬] SIMD 연산용 버퍼(_work_frame 등)는 반드시 MALLOC_CAP_INTERNAL에 
 * 할당되어야 하며, 16바이트 정렬이 깨지면 하드웨어 패닉(LoadStoreError)이 발생합니다.
 * 2. [안정성] 필터 위상 왜곡을 막기 위해 반드시 [Median -> DC(NaN제거) -> IIR -> Notch -> RMS] 
 * 순서의 파이프라인을 엄격히 준수합니다.
 * 3. [성능] _calcRMS는 dsps_dotprod_f32 내적 연산을 이용해 O(N) 루프를 O(1) 수준으로 가속합니다.
 * ========================================================================== */


/* ============================================================================
 * File: T231_Dsp_Pipeline_221.h
 * Summary: Advanced MFCC & DSP Pipeline Engine (v220.020 / SIMD + Matrix Optimized)
 * * [AI 메모: 제공 기능 요약]
 * 1. ESP32-S3 SIMD 및 Matrix API(dspm)를 활용한 초고속 FFT 및 MFCC 행렬 연산 가속.
 * 2. 다중 필터(Cascade IIR HPF/LPF, Adaptive Notch) 및 가변 Median/Window 함수 완벽 지원.
 * 3. 16바이트 정렬된 Internal SRAM 플랫 버퍼를 사용하여 연산 지연 및 메모리 병목 원천 차단.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. SIMD 연산용 버퍼(_work_frame 등)와 행렬 버퍼(_dct_matrix_flat)는 반드시 
 * MALLOC_CAP_INTERNAL에 할당되어야 S3 PIE 가속기가 정상 작동합니다.
 * 2. 다중 필터는 위상 왜곡을 막기 위해 반드시 [Median -> DC제거 -> IIR -> Notch -> RMS -> PreEmphasis] 
 * 순서의 파이프라인을 엄격히 준수해야 합니다.
 * 3. MFCC 계수(Static, Delta, Delta-Delta) 조립 시 메모리 정렬(alignas(16))을 유지합니다.
 * ========================================================================== */






#pragma once

#include "T210_Def_221.h"  // ST_T20_Config_t 정의 포함
#include "esp_dsp.h"


class CL_T20_DspPipeline {
   public:
	CL_T20_DspPipeline();
	~CL_T20_DspPipeline(); // 소멸자 추가 (freeBuffers 호출)


	// 엔진 초기화 (프리컴퓨팅 및 필터 생성)
	bool begin(const ST_T20_Config_t& cfg);

	// 단일 프레임 처리 (Time Domain -> 39D MFCC Vector)
    bool processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx = 0);


	// 노이즈 학습 제어 (외부 버튼이나 API에서 호출)
	void setNoiseLearning(bool active) {
		_noise_learning_active = active;
	}

	void resetNoiseStats() {
	    memset(_noise_learned_frames, 0, sizeof(_noise_learned_frames));
	}

	bool isNoiseLearning() const {
		return _noise_learning_active;
	}

    const float* getPowerSpectrum() const { return _power; }

	float getBandEnergy(float start_hz, float end_hz);
	float getLatestRMS(uint8_t axis_idx) const { return _current_rms[axis_idx]; }

   private:
    void _freeBuffers();

    // 전처리 파이프라인 (순서 분리)
    void _applyMedianFilter(float* p_data, int window_size);
    void _removeDC(float* p_data);                                 // [분리됨]
    void _calcRMS(const float* p_data, uint8_t axis_idx);          // [분리됨]
    void _applyAdaptiveNotch(float* p_data, uint8_t axis_idx);
    void _applyPreEmphasis(float* p_data, uint8_t axis_idx);
    void _applyNoiseGate(float* p_data);
    void _generateWindow(float* p_out, int n, EM_T20_WindowType_t type);

    // 주파수 도메인 변환 및 특징 추출
    void _computePowerSpectrum(const float* p_time);
    void _learnNoiseSpectrum(uint8_t axis_idx);
    void _applySpectralSubtraction(uint8_t axis_idx);
    void _applyMelFilterbank(float* p_log_mel_out);
    void _computeDCT2(const float* p_in, float* p_out);

    // 39차원 벡터 조립
    void _pushHistory(const float* p_mfcc, uint8_t axis_idx);
    void _build39DVector(ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx);

   private:
	ST_T20_Config_t _cfg;

    // 동적 할당 버퍼 (SIMD 정렬)
    float* _work_frame     = nullptr;
    float* _window         = nullptr;
    float* _power          = nullptr;
    float* _noise_spectrum = nullptr;
    float* _mel_bank_flat  = nullptr;
    float* _fft_io_buf     = nullptr;

    // 행렬 연산 가속을 위한 플랫 버퍼
    float* _dct_matrix_flat = nullptr;

    alignas(16) float _log_mel[T20::C10_DSP::MEL_FILTERS];
    alignas(16) float _mfcc_history[T20::C10_DSP::AXIS_COUNT_MAX][T20::C10_DSP::MFCC_HISTORY_LEN][T20::C10_DSP::MFCC_COEFFS_MAX];

    // 필터 계수 및 상태 변수 (분리 및 정렬 완료)
    alignas(16) float _hpf_coeffs[5];
    alignas(16) float _lpf_coeffs[5];
    alignas(16) float _notch_coeffs[5];

    float _hpf_state[T20::C10_DSP::AXIS_COUNT_MAX][2];
    float _lpf_state[T20::C10_DSP::AXIS_COUNT_MAX][2];
    float _notch_state[T20::C10_DSP::AXIS_COUNT_MAX][2];

    float _prev_sample[T20::C10_DSP::AXIS_COUNT_MAX];
    float _current_rms[T20::C10_DSP::AXIS_COUNT_MAX];

    uint16_t _history_count[T20::C10_DSP::AXIS_COUNT_MAX];
    uint16_t _noise_learned_frames[T20::C10_DSP::AXIS_COUNT_MAX];

    bool     _noise_learning_active = false;
    uint16_t _current_fft_size = 0;


};


