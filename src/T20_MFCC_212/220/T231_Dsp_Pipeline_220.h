/* ============================================================================
 * File: T231_Dsp_Pipeline_220.h
 * Summary: MFCC & DSP Pipeline Engine (SIMD Optimized)
 * * [AI 메모: 제공 기능 요약]
 * 1. ESP32-S3 SIMD 가속을 활용한 고속 FFT 및 MFCC 추출 엔진.
 * 2. 16바이트 정렬된 Internal SRAM 버퍼를 사용하여 연산 지연 최소화.
 * 3. 축별 독립적인 IIR 필터 상태 및 노이즈 프로필 관리.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. SIMD 연산용 버퍼(_work_frame, _fft_io_buf 등)는 반드시 MALLOC_CAP_INTERNAL에
 * 할당되어야 하며, PSRAM에 위치할 경우 연산 오류나 심각한 성능 저하가 발생합니다.
 * 2. dsps_fft2r_init_fc32는 실행 시 내부 룩업 테이블을 생성하므로 begin()에서
 * 한 번만 호출되도록 관리합니다.
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
    void _freeBuffers(); // 기존 할당된 메모리 해제

	// 전처리 파이프라인
	void _applyPreprocess(float* p_data, uint8_t axis_idx);        // [수정] axis_idx 추가
    void _applyRuntimeFilter(float* p_data, uint8_t axis_idx);     // [수정] axis_idx 추가
	void _applyNoiseGate(float* p_data);


	// 주파수 도메인 변환 및 특징 추출
	void _computePowerSpectrum(const float* p_time);
	void _learnNoiseSpectrum(uint8_t axis_idx);
    void _applySpectralSubtraction(uint8_t axis_idx);
	void _applyMelFilterbank(float* p_log_mel_out);
	void _computeDCT2(const float* p_in, float* p_out);

	// 39차원 벡터(Delta, Delta-Delta) 조립
	void _pushHistory(const float* p_mfcc, uint8_t axis_idx);      // [수정] axis_idx 추가

	void _computeDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta_out);
	void _computeDeltaDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta2_out);
	void _build39DVector(ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx); // [수정] axis_idx 추가



   private:
	ST_T20_Config_t 	_cfg;

    // 동적 할당 버퍼 (SIMD 정렬 보장) (Internal SRAM 강제 할당)
    float* 				_work_frame 	= nullptr;
    float* 				_window     	= nullptr;
    float* 				_power      	= nullptr;
    float* 				_noise_spectrum = nullptr;	// 3축 분량
    float* 				_mel_bank_flat 	= nullptr; 	// [MEL_FILTERS * bins] 형태로 관리
    float* 				_fft_io_buf 	= nullptr;  // FFT 연산용 복소수 버퍼 (N*2)

	alignas(16) float 	_log_mel[T20::C10_DSP::MEL_FILTERS];
    alignas(16) float 	_dct_matrix[T20::C10_DSP::MFCC_COEFFS_MAX][T20::C10_DSP::MEL_FILTERS];
    alignas(16) float 	_mfcc_history[T20::C10_DSP::AXIS_COUNT_MAX][T20::C10_DSP::MFCC_HISTORY_LEN][T20::C10_DSP::MFCC_COEFFS_MAX];


	// 필터 및 상태 변수
	alignas(16) float 	_biquad_coeffs[5];

	float 				_biquad_state[T20::C10_DSP::AXIS_COUNT_MAX][2];   // 축별 IIR Biquad 상태 (2-tap)
	float 				_prev_sample[T20::C10_DSP::AXIS_COUNT_MAX];       // 축별 Pre-emphasis 이전 샘플
	float 				_current_rms[T20::C10_DSP::AXIS_COUNT_MAX];       // 축별 실시간 RMS

	uint16_t 			_history_count[T20::C10_DSP::AXIS_COUNT_MAX];
	uint16_t 			_noise_learned_frames[T20::C10_DSP::AXIS_COUNT_MAX]; // 축별 학습 프레임 카운터 분리

	bool	 			_noise_learning_active = false;

	uint16_t 			_current_fft_size = 0;


};
