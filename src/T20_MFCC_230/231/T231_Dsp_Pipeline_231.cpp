/* ============================================================================
 * File: T231_Dsp_Pipeline_231.cpp
 * Summary: MFCC & DSP Pipeline Engine Implementation (SIMD Optimized)
 * ========================================================================== */

#include "T231_Dsp_Pipeline_231.h"

#include <math.h>
#include <string.h>

CL_T20_DspPipeline::CL_T20_DspPipeline() {

    // 필터 상태 초기화 (biquad_state 삭제 후 분리된 상태 초기화)
    memset(_hpf_state, 0, sizeof(_hpf_state));
    memset(_lpf_state, 0, sizeof(_lpf_state));
    memset(_notch_state, 0, sizeof(_notch_state));

    memset(_history_count, 0, sizeof(_history_count));
    memset(_noise_learned_frames, 0, sizeof(_noise_learned_frames));
    _current_fft_size = 0;

}

CL_T20_DspPipeline::~CL_T20_DspPipeline() {
    _freeBuffers();
}

// 기존 할당된 모든 동적 버퍼 해제
void CL_T20_DspPipeline::_freeBuffers() {

	auto safe_free = [](void* ptr) { if (ptr) heap_caps_free(ptr); };

	safe_free(_work_frame);     _work_frame     = nullptr;
    safe_free(_window);         _window         = nullptr;
    safe_free(_power);          _power          = nullptr;
    safe_free(_noise_spectrum); _noise_spectrum = nullptr;
    safe_free(_fft_io_buf);     _fft_io_buf     = nullptr;
    safe_free(_mel_bank_flat);  _mel_bank_flat  = nullptr;

	// 신규 행렬 버퍼 해제
    safe_free(_dct_matrix_flat); _dct_matrix_flat = nullptr;

}

/* ============================================================================
 * [v230.011] DSP 엔진 초기화 및 프리컴퓨팅 로직 (FIR 필터 추가 및 정합성 보완)
 * ========================================================================== */
bool CL_T20_DspPipeline::begin(const ST_T20_Config_t& cfg) {
    _cfg = cfg;

    const uint16_t N = (uint16_t)_cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;
    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;
    const uint16_t mfcc_dim = _cfg.feature.mfcc_coeffs;

    // [1] 동적 버퍼 재할당
    if (_current_fft_size != N) {
        Serial.printf("[DSP] Re-allocating internal buffers for FFT Size: %d\n", N);
        _freeBuffers();
        _current_fft_size = N;

        _work_frame      = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _window          = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _power           = (float*)heap_caps_aligned_alloc(16, bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _fft_io_buf      = (float*)heap_caps_aligned_alloc(16, N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        _noise_spectrum  = (float*)heap_caps_aligned_alloc(16, 3 * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _mel_bank_flat   = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _dct_matrix_flat = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * T20::C10_DSP::MFCC_COEFFS_MAX * sizeof(float), MALLOC_CAP_INTERNAL);

        if (!_work_frame || !_window || !_power || !_fft_io_buf || !_mel_bank_flat || !_noise_spectrum || !_dct_matrix_flat) {
            Serial.println(F("[DSP] Critical: Internal SRAM OOM!"));
            return false;
        }
        if (dsps_fft2r_init_fc32(NULL, N) != ESP_OK) return false;
    }

    _generateWindow(_window, N, _cfg.preprocess.window_type);

    // FIR 계수 생성
    // [2] : FIR 필터 계수 직접 생성 (Windowed-Sinc 알고리즘 연동)
    // HPF는 Spectral Inversion을 위해 무조건 홀수 탭(Odd Taps)이어야 완벽한 Linear Phase를 보장함
    if (_cfg.preprocess.fir_hpf.enabled) {
        uint16_t taps = _cfg.preprocess.fir_hpf.num_taps;
        if (taps % 2 == 0) taps--;   // 짝수일 경우 홀수로 1 감소 보정
        if (taps > 127) taps = 127;  // 버퍼 선언 크기(128) 오버플로우 한계 방어

        // 내부 구현된 Windowed-Sinc 기반 알고리즘 호출
        _generateFirHpfWindowedSinc(_fir_hpf_coeffs, taps, _cfg.preprocess.fir_hpf.cutoff_hz);
        
        for(uint8_t a = 0; a < (uint8_t)_cfg.feature.axis_count; a++) {
            dsps_fir_init_f32(&_fir_hpf_inst[a], _fir_hpf_coeffs, _fir_hpf_state[a], taps);
        }
    }
    
    if (_cfg.preprocess.fir_lpf.enabled) {
        uint16_t taps = _cfg.preprocess.fir_lpf.num_taps;
        if (taps % 2 == 0) taps--;
        if (taps > 127) taps = 127;

        _generateFirLpfWindowedSinc(_fir_lpf_coeffs, taps, _cfg.preprocess.fir_lpf.cutoff_hz);
        
        for(uint8_t a = 0; a < (uint8_t)_cfg.feature.axis_count; a++) {
            dsps_fir_init_f32(&_fir_lpf_inst[a], _fir_lpf_coeffs, _fir_lpf_state[a], taps);
        }
    }
    
    // IIR 및 Notch 계수 생성
    if (_cfg.preprocess.iir_hpf.enabled) dsps_biquad_gen_hpf_f32(_hpf_coeffs, _cfg.preprocess.iir_hpf.cutoff_hz / fs, _cfg.preprocess.iir_hpf.q_factor);
    if (_cfg.preprocess.iir_lpf.enabled) dsps_biquad_gen_lpf_f32(_lpf_coeffs, _cfg.preprocess.iir_lpf.cutoff_hz / fs, _cfg.preprocess.iir_lpf.q_factor);
    if (_cfg.preprocess.notch.enabled) {
        dsps_biquad_gen_notch_f32(_notch_coeffs, _cfg.preprocess.notch.target_freq_hz / fs, _cfg.preprocess.notch.gain, _cfg.preprocess.notch.q_factor);
    }

    // [3] Mel-Filterbank 평탄화 생성
    float min_mel = 0.0f;
    float max_mel = T20::C10_DSP::MEL_SCALE_CONST * log10f(1.0f + ((fs / 2.0f) / T20::C10_DSP::MEL_FREQ_CONST));
    float mel_step = (max_mel - min_mel) / (float)(T20::C10_DSP::MEL_FILTERS + 1);

    uint16_t bin_points[T20::C10_DSP::MEL_FILTERS + 2];
    for (int i = 0; i < T20::C10_DSP::MEL_FILTERS + 2; i++) {
        float hz = T20::C10_DSP::MEL_FREQ_CONST * (powf(10.0f, (min_mel + i * mel_step) / T20::C10_DSP::MEL_SCALE_CONST) - 1.0f);
        bin_points[i] = (uint16_t)roundf(N * hz / fs);
    }
    
    memset(_mel_bank_flat, 0, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float));
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        uint16_t left = bin_points[m], center = bin_points[m + 1], right = bin_points[m + 2];
        for (int k = left; k < center && k < bins; k++)
            _mel_bank_flat[k * T20::C10_DSP::MEL_FILTERS + m] = (float)(k - left) / (float)(center - left + 1e-12f);
        for (int k = center; k < right && k < bins; k++)
            _mel_bank_flat[k * T20::C10_DSP::MEL_FILTERS + m] = (float)(right - k) / (float)(right - center + 1e-12f);
    }

    // [4] DCT-II Matrix 생성
    for (int k = 0; k < T20::C10_DSP::MEL_FILTERS; k++) {
        for (int n = 0; n < mfcc_dim; n++) {
            _dct_matrix_flat[k * mfcc_dim + n] = cosf((M_PI / (float)T20::C10_DSP::MEL_FILTERS) * (k + 0.5f) * n);
        }
    }

    // 부팅 및 Watchdog 리셋 등 최초 진입 시, 확실한 초기화를 위해 노이즈 기록까지 삭제
    resetNoiseStats();
    memset(_noise_spectrum, 0, 3 * bins * sizeof(float));

    // [5] 🚨 모든 계수가 세팅되었으므로 최종 필터 상태(Instance) 초기화 일괄 실행
    resetFilterStates();

    Serial.printf("[DSP] Engine v230.011 Started (FFT:%d, Win:%d, MFCC:%d)\n", N, (int)_cfg.preprocess.window_type, mfcc_dim);
    return true;
}


/* ============================================================================
 * [v220.020] DSP 엔진 초기화 및 프리컴퓨팅 로직
 * 1. 하드웨어 가속: Matrix API 및 SIMD 사용을 위한 Internal SRAM 정렬 할당.
 * 2. 필터 뱅크: HPF, LPF 독립 계수 생성 및 상태 초기화.
 * 3. 가변 윈도우: 5종 윈도우 함수(Hann, Hamming, Blackman, Flat-top, Rect) 지원.
 * 4. 행렬 최적화: DCT 계수를 1차원 평탄화하여 dspm_mult_f32 가속 준비.
 * ========================================================================== */
/*
bool CL_T20_DspPipeline::begin(const ST_T20_Config_t& cfg) {
    _cfg = cfg;

    const uint16_t N = (uint16_t)_cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;
    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;
    const uint16_t mfcc_dim = _cfg.feature.mfcc_coeffs;

    // [1] 동적 버퍼 재할당 로직 (기존과 동일하여 생략. _freeBuffers() 및 할당 코드는 원본 유지)
    if (_current_fft_size != N) {
        Serial.printf("[DSP] Re-allocating internal buffers for FFT Size: %d\n", N);
        _freeBuffers();
        _current_fft_size = N;

        _work_frame      = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _window          = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _power           = (float*)heap_caps_aligned_alloc(16, bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _fft_io_buf      = (float*)heap_caps_aligned_alloc(16, N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        _noise_spectrum  = (float*)heap_caps_aligned_alloc(16, 3 * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _mel_bank_flat   = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _dct_matrix_flat = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * T20::C10_DSP::MFCC_COEFFS_MAX * sizeof(float), MALLOC_CAP_INTERNAL);

        if (!_work_frame || !_window || !_power || !_fft_io_buf || !_mel_bank_flat || !_noise_spectrum || !_dct_matrix_flat) {
            Serial.println(F("[DSP] Critical: Internal SRAM OOM!"));
            return false;
        }
        if (dsps_fft2r_init_fc32(NULL, N) != ESP_OK) return false;
    }

    _generateWindow(_window, N, _cfg.preprocess.window_type);
    
    // [2] 필터 상태 전체 초기화 (FSM 전이용 공용 함수 사용)
    resetFilterStates();

    // [3] 필터 계수 독립 생성 (FIR 추가)
    if (_cfg.preprocess.fir_hpf.enabled) {
        dsps_fir_gen_hpf_f32(_fir_hpf_coeffs, _cfg.preprocess.fir_hpf.num_taps, _cfg.preprocess.fir_hpf.cutoff_hz / fs);
        // 축 개수만큼 FIR 인스턴스 초기화 (esp-dsp 규격)
        for(uint8_t a=0; a < (uint8_t)_cfg.feature.axis_count; a++) {
            dsps_fir_init_f32(&_fir_hpf_inst[a], _fir_hpf_coeffs, _fir_hpf_state[a], _cfg.preprocess.fir_hpf.num_taps);
        }
    }
    if (_cfg.preprocess.fir_lpf.enabled) {
        dsps_fir_gen_lpf_f32(_fir_lpf_coeffs, _cfg.preprocess.fir_lpf.num_taps, _cfg.preprocess.fir_lpf.cutoff_hz / fs);
        for(uint8_t a=0; a < (uint8_t)_cfg.feature.axis_count; a++) {
            dsps_fir_init_f32(&_fir_lpf_inst[a], _fir_lpf_coeffs, _fir_lpf_state[a], _cfg.preprocess.fir_lpf.num_taps);
        }
    }

    if (_cfg.preprocess.iir_hpf.enabled) dsps_biquad_gen_hpf_f32(_hpf_coeffs, _cfg.preprocess.iir_hpf.cutoff_hz / fs, _cfg.preprocess.iir_hpf.q_factor);
    if (_cfg.preprocess.iir_lpf.enabled) dsps_biquad_gen_lpf_f32(_lpf_coeffs, _cfg.preprocess.iir_lpf.cutoff_hz / fs, _cfg.preprocess.iir_lpf.q_factor);

    if (_cfg.preprocess.notch.enabled) {
        dsps_biquad_gen_notch_f32(_notch_coeffs, _cfg.preprocess.notch.target_freq_hz / fs, _cfg.preprocess.notch.gain, _cfg.preprocess.notch.q_factor);
    }
    
    // [4] Mel-Filterbank 평탄화 생성
    float min_mel = 0.0f;
    float max_mel = T20::C10_DSP::MEL_SCALE_CONST * log10f(1.0f + ((fs / 2.0f) / T20::C10_DSP::MEL_FREQ_CONST));
    float mel_step = (max_mel - min_mel) / (float)(T20::C10_DSP::MEL_FILTERS + 1);

    uint16_t bin_points[T20::C10_DSP::MEL_FILTERS + 2];
    for (int i = 0; i < T20::C10_DSP::MEL_FILTERS + 2; i++) {
        float hz = T20::C10_DSP::MEL_FREQ_CONST * (powf(10.0f, (min_mel + i * mel_step) / T20::C10_DSP::MEL_SCALE_CONST) - 1.0f);
        bin_points[i] = (uint16_t)roundf(N * hz / fs);
    }
    
    memset(_mel_bank_flat, 0, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float));
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        uint16_t left = bin_points[m], center = bin_points[m + 1], right = bin_points[m + 2];
        
        // [성능 최적화] dspm_mult_f32 행렬 가속기를 사용하기 위해 전치(Transpose)된 형태로 메모리 적재
        // Power(1 x bins) * Mel(bins x 26) = Result(1 x 26) 을 위해 Column-major 방식으로 플랫화
        for (int k = left; k < center && k < bins; k++)
            _mel_bank_flat[k * T20::C10_DSP::MEL_FILTERS + m] = (float)(k - left) / (float)(center - left + 1e-12f);
        for (int k = center; k < right && k < bins; k++)
            _mel_bank_flat[k * T20::C10_DSP::MEL_FILTERS + m] = (float)(right - k) / (float)(right - center + 1e-12f);

    }

    // [5] 🚨 DCT-II Matrix API 가속용 조밀 평탄화 (Tightly Packed)
    // dspm_mult_f32 호환을 위해 설정된 mfcc_dim(열 크기)에 딱 맞춰 연속 메모리로 배치
    for (int k = 0; k < T20::C10_DSP::MEL_FILTERS; k++) {
        for (int n = 0; n < mfcc_dim; n++) {
            _dct_matrix_flat[k * mfcc_dim + n] = cosf((M_PI / (float)T20::C10_DSP::MEL_FILTERS) * (k + 0.5f) * n);
        }
    }

    Serial.printf("[DSP] Engine v220.020 Started (FFT:%d, Win:%d, MFCC:%d)\n", N, (int)_cfg.preprocess.window_type, mfcc_dim);
    return true;
}
*/

// 가변 윈도우 생성 헬퍼 함수
void CL_T20_DspPipeline::_generateWindow(float* p_out, int n, EM_T20_WindowType_t type) {
    switch (type) {
        case EN_T20_WINDOW_HAMMING:
			// [수정됨] 라이브러리에 없는 Hamming Window 수동 구현
            for (int i = 0; i < n; i++) {
                p_out[i] = 0.54f - 0.46f * cosf((2.0f * (float)M_PI * i) / (n - 1));
            }
            break;
        case EN_T20_WINDOW_BLACKMAN:    dsps_wind_blackman_f32(p_out, n); break;
        case EN_T20_WINDOW_FLATTOP:     dsps_wind_flat_top_f32(p_out, n); break;
        case EN_T20_WINDOW_RECTANGULAR: for (int i = 0; i < n; i++) p_out[i] = 1.0f; break;
        case EN_T20_WINDOW_HANN:
        default:                        dsps_wind_hann_f32(p_out, n); break;
    }
}


bool CL_T20_DspPipeline::processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx) {
    if (!p_time_in || !p_vec_out || axis_idx >= 3) return false;
    
    if (((uintptr_t)_work_frame & 15) != 0 || ((uintptr_t)_dct_matrix_flat & 15) != 0) {
        Serial.println(F("[Critical] DSP Buffers are NOT 16-byte aligned! Process Halted."));
        return false; 
    }
    
    memcpy(_work_frame, p_time_in, sizeof(float) * _current_fft_size);

    // [1] 비선형 스파이크 필터 (위상 왜곡 방지를 위해 맨 처음 실행)
    if (_cfg.preprocess.median.enabled) _applyMedianFilter(_work_frame, _cfg.preprocess.median.window_size);

    // [2] 선형 전처리 (DC 편향 제거를 통한 수치적 안정성 확보 및 NaN 방어)
    _removeDC(_work_frame);

    // [3-1] 선형 위상을 보장하는 고차원 FIR 필터 (IIR 보다 선행)
    if (_cfg.preprocess.fir_hpf.enabled) {
        dsps_fir_f32(&_fir_hpf_inst[axis_idx], _work_frame, _work_frame, _current_fft_size);
    }
    if (_cfg.preprocess.fir_lpf.enabled) {
        dsps_fir_f32(&_fir_lpf_inst[axis_idx], _work_frame, _work_frame, _current_fft_size);
    }

    // [3-2] 저연산량 IIR Filter (Cascade 방식)
    if (_cfg.preprocess.iir_hpf.enabled) dsps_biquad_f32(_work_frame, _work_frame, _current_fft_size, _hpf_coeffs, _hpf_state[axis_idx]);
    if (_cfg.preprocess.iir_lpf.enabled) dsps_biquad_f32(_work_frame, _work_frame, _current_fft_size, _lpf_coeffs, _lpf_state[axis_idx]);

    // [4] 특정 주파수 톤 제거 (Notch)
    if (_cfg.preprocess.notch.enabled) dsps_biquad_f32(_work_frame, _work_frame, _current_fft_size, _notch_coeffs, _notch_state[axis_idx]);

    // [5] 필터링이 끝난 깨끗한 파형에서 순수 진동 에너지(RMS) 도출
    _calcRMS(_work_frame, axis_idx);

    // [6] AI 분석용 고역 강조 (RMS 계산 후 파형 모양 변경)
    _applyPreEmphasis(_work_frame, axis_idx);

    // [7] 미세 묵음 처리 (Noise Gate)
    _applyNoiseGate(_work_frame);

    // [8] Windowing & FFT
    dsps_mul_f32(_work_frame, _window, _work_frame, _current_fft_size, 1, 1, 1);
    _computePowerSpectrum(_work_frame);

    // [9] 노이즈 감산 및 MFCC 조립 (기존과 동일하여 생략. 아래 함수들은 원본 유지)
    _learnNoiseSpectrum(axis_idx);
    _applySpectralSubtraction(axis_idx);
    _applyMelFilterbank(_log_mel);

    alignas(16) float current_mfcc[T20::C10_DSP::MFCC_COEFFS_MAX];
    _computeDCT2(_log_mel, current_mfcc);

    p_vec_out->rms[axis_idx] = _current_rms[axis_idx];
    _pushHistory(current_mfcc, axis_idx);

    if (_history_count[axis_idx] >= T20::C10_DSP::MFCC_HISTORY_LEN) {
        _build39DVector(p_vec_out, axis_idx);
        return true;
    }
    return false;
}


// === 1. DC 편향 제거 및 NaN 방어 (수치적 안정성 확보) ===
void CL_T20_DspPipeline::_removeDC(float* p_data) {
    uint16_t N = _current_fft_size;
    float sum = 0.0f;

    for (int i = 0; i < N; i++) {
        // [안정성 보장] 센서 노이즈로 인한 NaN 또는 Inf 값 유입 시 0.0f로 초기화 (필터 발산 원천 차단)
        if (isnan(p_data[i]) || isinf(p_data[i])) {
            p_data[i] = 0.0f;
        }
        sum += p_data[i];
    }
    
    if (!_cfg.preprocess.remove_dc) return;

    float mean = sum / (float)N;
    for (int i = 0; i < N; i++) {
        p_data[i] -= mean;
    }
}

// === 2. 순수 RMS 계산 (SIMD 내적 가속) ===
void CL_T20_DspPipeline::_calcRMS(const float* p_data, uint8_t axis_idx) {
    float sum_sq = 0.0f;
    uint16_t N = _current_fft_size;

    // [성능 최적화] for 루프를 통한 제곱 합산을 SIMD 하드웨어 내적(Dot Product) API로 대체
    // p_data 벡터 자기 자신과의 내적은 곧 각 성분 제곱의 합과 같음
    dsps_dotprod_f32(p_data, p_data, &sum_sq, N);

    // 1e-12f를 더해 0으로 나누기(Divide by Zero) 예외 방지
    _current_rms[axis_idx] = sqrtf(fmaxf(sum_sq / (float)N, 1e-12f));
}


// ===  2: 고역 강조 (Pre-emphasis) ===
void CL_T20_DspPipeline::_applyPreEmphasis(float* p_data, uint8_t axis_idx) {
    if (!_cfg.preprocess.preemphasis.enable) return;
    float alpha = _cfg.preprocess.preemphasis.alpha;
    for (int i = 0; i < _current_fft_size; i++) {
        float cur = p_data[i];
        p_data[i] = cur - (alpha * _prev_sample[axis_idx]);
        _prev_sample[axis_idx] = cur;
    }
}



void CL_T20_DspPipeline::_applyMedianFilter(float* p_data, int window_size) {
    if (window_size < 3) return;
    
    // CPU 부하 방지를 위해 최대 7-tap으로 제한 (홀수 보정)
    if (window_size > 7) window_size = 7;
    if (window_size % 2 == 0) window_size--; 
    
    uint16_t N = _current_fft_size;

    // ====================================================================
    // [성능 최적화] 3-Tap Fast Path: Branchless SIMD-friendly 연산
    // ====================================================================
    if (window_size == 3) {
        float prev2 = p_data[0];
        float prev1 = p_data[1];

        for (int i = 1; i < N - 1; i++) {
            float a = prev2;           
            float b = prev1;           
            float c = p_data[i + 1];   

            // 분기 예측 실패(Branch Misprediction)에 의한 CPU 지연을 원천 차단하기 위해
            // 1:1 하드웨어 매핑이 가능한 fmaxf, fminf 조합으로 중간값 도출
            float median = fmaxf(fminf(a, b), fminf(fmaxf(a, b), c));

            prev2 = prev1;
            prev1 = c;
            p_data[i] = median; // In-place 업데이트
        }
        return;
    }

    // ====================================================================
    // [가변 윈도우] 5-Tap / 7-Tap 처리: 고속 삽입 정렬(Insertion Sort)
    // ====================================================================
    int half = window_size / 2;
    
    // 1600Hz, 최대 7-tap 정렬이므로 스택 메모리(SRAM) 사용 (속도 극대화)
    float temp_buf[7];
    float history[7];
    
    // 이력 버퍼를 초기 데이터로 채움
    for(int i = 0; i < window_size; i++) history[i] = p_data[0]; 

    for (int i = 0; i < N; i++) {
        // 1. Sliding Window 갱신 (가장 오래된 데이터 밀어내기)
        for(int j = 0; j < window_size - 1; j++) history[j] = history[j + 1];
        
        // 2. 미래 데이터 로드 (배열 끝에 도달하면 마지막 값 복사)
        history[window_size - 1] = (i + half < N) ? p_data[i + half] : p_data[N - 1];
        
        // 3. 정렬을 위해 임시 버퍼에 복사
        memcpy(temp_buf, history, window_size * sizeof(float));
        
        // 4. 고속 삽입 정렬 (7개 이하 요소 정렬 시 퀵소트보다 빠름)
        for (int j = 1; j < window_size; j++) {
            float key = temp_buf[j];
            int k = j - 1;
            while (k >= 0 && temp_buf[k] > key) {
                temp_buf[k + 1] = temp_buf[k];
                k--;
            }
            temp_buf[k + 1] = key;
        }
        
        // 5. 원본 배열에 중간값 업데이트 (In-place)
        p_data[i] = temp_buf[half];
    }
}

void CL_T20_DspPipeline::_applyAdaptiveNotch(float* p_data, uint8_t axis_idx) {
    if (!_cfg.preprocess.notch.enabled) return;

    // dsps_biquad_f32를 사용하여 Notch 필터링 수행
    // _notch_coeffs는 begin()에서 dsps_biquad_gen_notch_f32로 생성됨
    dsps_biquad_f32(p_data, p_data, _current_fft_size, _notch_coeffs, _notch_state[axis_idx]);
}


void CL_T20_DspPipeline::_applyNoiseGate(float* p_data) {
    if (!_cfg.preprocess.noise.enable_gate) return;
    float threshold = _cfg.preprocess.noise.gate_threshold_abs;
    uint16_t N = _current_fft_size; // 추가
    for (int i = 0; i < N; i++) { // FFT_SIZE -> N 수정
        if (fabsf(p_data[i]) < threshold) p_data[i] = 0.0f;
    }
}


void CL_T20_DspPipeline::_computePowerSpectrum(const float* p_time) {
    // [수정] static fft_io 삭제 -> begin()에서 할당한 _fft_io_buf 사용
    uint16_t N = _current_fft_size;
    uint16_t bins = (N / 2) + 1;

    for (int i = 0; i < N; i++) {
        _fft_io_buf[i * 2]     = p_time[i];
        _fft_io_buf[i * 2 + 1] = 0.0f;
    }

    dsps_fft2r_fc32(_fft_io_buf, N);
    dsps_bit_rev2r_fc32(_fft_io_buf, N);

    for (int i = 0; i < bins; i++) {
        float re = _fft_io_buf[i * 2], im = _fft_io_buf[i * 2 + 1];
        _power[i] = fmaxf((re * re + im * im), 1e-12f);
    }
}



void CL_T20_DspPipeline::_learnNoiseSpectrum(uint8_t axis_idx) {
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;

    // [중요 수정] 축별로 학습 프레임을 개별 검사합니다.
    bool is_learning = _noise_learning_active || (_noise_learned_frames[axis_idx] < _cfg.preprocess.noise.noise_learn_frames);
    if (!is_learning) return;

    uint16_t bins = (_current_fft_size / 2) + 1;
    // 해당 축의 노이즈 버퍼 시작 위치 포인터 계산
    float* target_noise = _noise_spectrum + (axis_idx * bins);

    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_ADAPTIVE) {
        float alpha = _noise_learning_active ? 0.2f : _cfg.preprocess.noise.adaptive_alpha;
        for (int i = 0; i < bins; i++) {
            target_noise[i] = (1.0f - alpha) * target_noise[i] + alpha * _power[i];
        }
    } else {  // EN_T20_NOISE_FIXED
        float count = (float)_noise_learned_frames[axis_idx];
        for (int i = 0; i < bins; i++) {
            target_noise[i] = ((target_noise[i] * count) + _power[i]) / (count + 1.0f);
        }
    }

    if (_noise_learned_frames[axis_idx] < 0xFFFF) _noise_learned_frames[axis_idx]++;
}


void CL_T20_DspPipeline::_applySpectralSubtraction(uint8_t axis_idx) {
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;
    if (_cfg.preprocess.noise.mode == EN_T20_NOISE_FIXED && _noise_learned_frames[axis_idx] < _cfg.preprocess.noise.noise_learn_frames) return;

    uint16_t bins = (_current_fft_size / 2) + 1;
    float strength = _cfg.preprocess.noise.spectral_subtract_strength;

    // 해당 축의 노이즈 버퍼 시작 위치
    float* target_noise = _noise_spectrum + (axis_idx * bins);

    for (int i = 0; i < bins; i++) {
        _power[i] = fmaxf(_power[i] - (strength * target_noise[i]), 1e-12f);
    }
}


void CL_T20_DspPipeline::_applyMelFilterbank(float* p_log_mel_out) {
    uint16_t bins = (_current_fft_size / 2) + 1;
    
    // [성능 최적화] 개별 dotprod 루프를 삭제하고 1 x 26 행렬 곱셈 연산으로 통합 가속
    // Matrix 가속기의 Load/Store 에러를 방지하기 위해 16-byte 정렬 필수 적용
    alignas(16) float mel_energy[T20::C10_DSP::MEL_FILTERS] = {0};

    // 연산 규격: [1 x bins] * [bins x 26] = [1 x 26]
    // p_in(_power): 1행 bins열
    // _mel_bank_flat: bins행 26열 (begin()에서 Column-major로 전치 패킹되어 있어야 함)
    // mel_energy: 1행 26열 출력
    dspm_mult_f32(_power, _mel_bank_flat, mel_energy, 1, bins, T20::C10_DSP::MEL_FILTERS);

    // 변환된 에너지를 Log Scale 로 변환 (NaN 방지를 위해 하한값 1e-12f 보장)
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        p_log_mel_out[m] = logf(fmaxf(mel_energy[m], 1e-12f));
    }
}


void CL_T20_DspPipeline::_computeDCT2(const float* p_in, float* p_out) {
    // [v220.020] Matrix Multiplication 가속
    // p_in(Log-Mel): 1 x MEL_FILTERS
    // _dct_matrix_flat: MEL_FILTERS x mfcc_coeffs (begin에서 조밀하게 패킹됨)
    // p_out(MFCC): 1 x mfcc_coeffs

    const int m = 1;
    const int n = T20::C10_DSP::MEL_FILTERS;
    const int k = _cfg.feature.mfcc_coeffs;

    // SIMD 고속 연산기 작동
    dspm_mult_f32(p_in, _dct_matrix_flat, p_out, m, n, k);
}


void CL_T20_DspPipeline::_pushHistory(const float* p_mfcc, uint8_t axis_idx) {
    uint16_t dim = _cfg.feature.mfcc_coeffs;
    float (*hist)[T20::C10_DSP::MFCC_COEFFS_MAX] = _mfcc_history[axis_idx];

    if (_history_count[axis_idx] < T20::C10_DSP::MFCC_HISTORY_LEN) {
        memcpy(hist[_history_count[axis_idx]++], p_mfcc, sizeof(float) * dim);
    } else {
        // Sliding Window: 한 칸씩 밀고 마지막에 새 데이터 삽입
        memmove(hist[0], hist[1], sizeof(float) * dim * (T20::C10_DSP::MFCC_HISTORY_LEN - 1));
        memcpy(hist[T20::C10_DSP::MFCC_HISTORY_LEN - 1], p_mfcc, sizeof(float) * dim);
    }
}



void CL_T20_DspPipeline::_build39DVector(ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx) {
    uint16_t dim = _cfg.feature.mfcc_coeffs;
    float (*hist)[T20::C10_DSP::MFCC_COEFFS_MAX] = _mfcc_history[axis_idx];

    alignas(16) float delta[T20::C10_DSP::MFCC_COEFFS_MAX];
    alignas(16) float delta2[T20::C10_DSP::MFCC_COEFFS_MAX];

    // Delta: (h[t+1] - h[t-1]) / 2 (Center t=2)
    for (int i = 0; i < dim; i++) {
        delta[i] = (hist[3][i] - hist[1][i]) / 2.0f;
    }

    // Delta-Delta: h[t+1] - 2*h[t] + h[t-1]
    for (int i = 0; i < dim; i++) {
        delta2[i] = hist[3][i] - (2.0f * hist[2][i]) + hist[1][i];
    }

    // [v219] ST_T20_FeatureVector_t의 features[3][39] 구조에 맞춰 저장
    // 1축 모드일 때는 features[0]에만, 3축 모드일 때는 index a에 따라 각각 저장됨
    memcpy(&p_vec_out->features[axis_idx][0],       hist[2], sizeof(float) * dim); // Static
    memcpy(&p_vec_out->features[axis_idx][dim],     delta,   sizeof(float) * dim); // Delta
    memcpy(&p_vec_out->features[axis_idx][dim * 2], delta2,  sizeof(float) * dim); // Accel
}

/* ============================================================================
 * [FSM 연동] 필터 상태 초기화 (READY -> MONITORING 전이 시 호출)
 * ========================================================================== */
void CL_T20_DspPipeline::resetFilterStates() {
    memset(_fir_hpf_state, 0, sizeof(_fir_hpf_state));
    memset(_fir_lpf_state, 0, sizeof(_fir_lpf_state));
    
    // [정합성 보완] FIR 필터는 내부 링버퍼 포인터(pos)를 가지므로 반드시 init을 재호출해야 포인터가 0으로 초기화됨
    if (_cfg.preprocess.fir_hpf.enabled) {
        for(uint8_t a=0; a < (uint8_t)_cfg.feature.axis_count; a++)
            dsps_fir_init_f32(&_fir_hpf_inst[a], _fir_hpf_coeffs, _fir_hpf_state[a], _cfg.preprocess.fir_hpf.num_taps);
    }
    if (_cfg.preprocess.fir_lpf.enabled) {
        for(uint8_t a=0; a < (uint8_t)_cfg.feature.axis_count; a++)
            dsps_fir_init_f32(&_fir_lpf_inst[a], _fir_lpf_coeffs, _fir_lpf_state[a], _cfg.preprocess.fir_lpf.num_taps);
    }

    // IIR 및 기타 상태 변수 초기화
    memset(_hpf_state, 0, sizeof(_hpf_state));
    memset(_lpf_state, 0, sizeof(_lpf_state));
    memset(_notch_state, 0, sizeof(_notch_state));
    memset(_prev_sample, 0, sizeof(_prev_sample));
    memset(_history_count, 0, sizeof(_history_count));
    memset(_mfcc_history, 0, sizeof(_mfcc_history));

    // [정합성 보완] _noise_spectrum(학습된 노이즈 프로필)은 여기서 지우면 START 마다 프로필이 증발합니다.
    // 따라서 노이즈 관련 데이터는 건드리지 않고 영구 보존합니다.
}


/**
 * @brief 특정 주파수 범위(Hz)의 에너지를 계산하여 반환
 */
float CL_T20_DspPipeline::getBandEnergy(float start_hz, float end_hz) {
    if (!_power) return 0.0f;

    const uint16_t N = _current_fft_size;
    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;
    const float bin_res = fs / N;

    uint16_t start_bin = (uint16_t)(start_hz / bin_res);
    uint16_t end_bin = (uint16_t)(end_hz / bin_res);

    // 범위 제한
    if (end_bin >= (N / 2)) end_bin = (N / 2);

    float energy = 0.0f;
    for (uint16_t i = start_bin; i <= end_bin; i++) {
        energy += _power[i];
    }
    return energy;
}



/* ============================================================================
 * Windowed-Sinc 기반 FIR 필터 계수 생성기 (ESP-DSP Window 가속 결합)
 * ========================================================================== */
void CL_T20_DspPipeline::_generateFirLpfWindowedSinc(float* coeffs, uint16_t num_taps, float cutoff_hz) {
    float fs = T20::C10_DSP::SAMPLE_RATE_HZ;
    float normalized_cutoff = cutoff_hz / fs;
    
    // ESP-DSP 내장 윈도우 함수 가속기 활용을 위한 임시 버퍼 (Blackman 윈도우는 FIR에 최적)
    float* win = (float*)heap_caps_malloc(num_taps * sizeof(float), MALLOC_CAP_INTERNAL);
    if (!win) return;

    // ESP-DSP 함수로 블랙만 윈도우 배열 일괄 생성
    dsps_wind_blackman_f32(win, num_taps);

    float M = (float)(num_taps - 1);
    float sum = 0.0f;
    
    // Windowed-Sinc 알고리즘 연산
    for (int i = 0; i < num_taps; i++) {
        float x = (float)i - (M / 2.0f);
        if (x == 0.0f) {
            coeffs[i] = 2.0f * normalized_cutoff; // Sinc 중심점 (x->0 일때 극한값)
        } else {
            coeffs[i] = sinf(2.0f * (float)M_PI * normalized_cutoff * x) / ((float)M_PI * x);
        }
        coeffs[i] *= win[i]; // ESP-DSP에서 생성한 윈도우 씌우기
        sum += coeffs[i];
    }
    
    // Low-Pass Filter 정규화 (DC Gain = 1.0)
    for (int i = 0; i < num_taps; i++) {
        coeffs[i] /= sum;
    }
    
    heap_caps_free(win);
}

void CL_T20_DspPipeline::_generateFirHpfWindowedSinc(float* coeffs, uint16_t num_taps, float cutoff_hz) {
    // 1. 동일한 Cutoff를 가지는 LPF 계수 선행 생성
    _generateFirLpfWindowedSinc(coeffs, num_taps, cutoff_hz);
    
    // 2. Spectral Inversion (스펙트럼 반전)을 통한 HPF 변환
    // HPF[n] = DiracDelta[n - M/2] - LPF[n]
    uint16_t center = (num_taps - 1) / 2;
    for (int i = 0; i < num_taps; i++) {
        coeffs[i] = -coeffs[i];
    }
    // 중간 탭(센터)에 1.0(Dirac Delta) 더하기
    coeffs[center] += 1.0f;
}

