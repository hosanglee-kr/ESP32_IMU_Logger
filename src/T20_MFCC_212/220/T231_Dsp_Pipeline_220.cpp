/* ============================================================================
 * File: T231_Dsp_Pipeline_220.cpp
 * Summary: MFCC & DSP Pipeline Engine Implementation (SIMD Optimized)
 * ========================================================================== */

#include "T231_Dsp_Pipeline_220.h"

#include <math.h>
#include <string.h>

CL_T20_DspPipeline::CL_T20_DspPipeline() {
    // 초기화 리스트 또는 생성자에서 포인터 초기화

	memset(_biquad_state, 0, sizeof(_biquad_state));
    memset(_history_count, 0, sizeof(_history_count));
    memset(_noise_learned_frames, 0, sizeof(_noise_learned_frames));

	_current_fft_size = 0;

    // _work_frame = nullptr;
    // _window = nullptr;
    // _power = nullptr;
    // _noise_spectrum = nullptr;
    // _fft_io_buf = nullptr;
    // _mel_bank_flat = nullptr;
    // _current_fft_size = 0;

    // memset(_mfcc_history, 0, sizeof(_mfcc_history));
    // memset(_biquad_state, 0, sizeof(_biquad_state));
    // memset(_biquad_coeffs, 0, sizeof(_biquad_coeffs));
}

CL_T20_DspPipeline::~CL_T20_DspPipeline() {
    _freeBuffers();
}

// 기존 할당된 모든 동적 버퍼 해제
void CL_T20_DspPipeline::_freeBuffers() {

	auto safe_free = [](void* ptr) { if (ptr) heap_caps_free(ptr); };

	safe_free(_work_frame);     _work_frame 	= nullptr;
    safe_free(_window);         _window 		= nullptr;
    safe_free(_power);          _power 			= nullptr;
    safe_free(_noise_spectrum); _noise_spectrum = nullptr;
    safe_free(_fft_io_buf);     _fft_io_buf 	= nullptr;
    safe_free(_mel_bank_flat);  _mel_bank_flat 	= nullptr;

	// if (_work_frame)     { heap_caps_free(_work_frame);     _work_frame = nullptr; }
    // if (_window)         { heap_caps_free(_window);         _window = nullptr; }
    // if (_power)          { heap_caps_free(_power);          _power = nullptr; }
    // if (_noise_spectrum) { heap_caps_free(_noise_spectrum); _noise_spectrum = nullptr; }
    // if (_fft_io_buf)     { heap_caps_free(_fft_io_buf);     _fft_io_buf = nullptr; }
    // if (_mel_bank_flat)  { heap_caps_free(_mel_bank_flat);  _mel_bank_flat = nullptr; }
}




bool CL_T20_DspPipeline::begin(const ST_T20_Config_t& cfg) {
    _cfg = cfg;

	// _noise_learning_active = false;

	const uint16_t N = (uint16_t)_cfg.feature.fft_size;
    const uint16_t bins = (N / 2) + 1;

    const float fs = T20::C10_DSP::SAMPLE_RATE_HZ;

    if (_current_fft_size != N) {
        Serial.printf("[DSP] Re-allocating buffers for FFT Size: %d\n", N);

        _freeBuffers();
        _current_fft_size = N;

		// SIMD 성능을 위해 Internal SRAM 할당 (PSRAM 사용 안 함)
        _work_frame     = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _window         = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        _power          = (float*)heap_caps_aligned_alloc(16, bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _noise_spectrum = (float*)heap_caps_aligned_alloc(16, 3 * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        _fft_io_buf     = (float*)heap_caps_aligned_alloc(16, N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        _mel_bank_flat  = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float), MALLOC_CAP_INTERNAL);

        // _work_frame = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        // _window     = (float*)heap_caps_aligned_alloc(16, N * sizeof(float), MALLOC_CAP_INTERNAL);
        // _power      = (float*)heap_caps_aligned_alloc(16, bins * sizeof(float), MALLOC_CAP_INTERNAL);

        // [중요 수정] 3축(X,Y,Z) 독립 노이즈 프로필을 위해 3 * bins 크기 할당
        // _noise_spectrum = (float*)heap_caps_aligned_alloc(16, 3 * bins * sizeof(float), MALLOC_CAP_INTERNAL);
        // _fft_io_buf = (float*)heap_caps_aligned_alloc(16, N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
        // _mel_bank_flat = (float*)heap_caps_aligned_alloc(16, T20::C10_DSP::MEL_FILTERS * bins * sizeof(float), MALLOC_CAP_INTERNAL);

        if (!_work_frame || !_window || !_power || !_mel_bank_flat || !_fft_io_buf || !_noise_spectrum) {
            Serial.println(F("[DSP] Critical: Memory Allocation Failed!"));
            return false;
        }

        esp_err_t err = dsps_fft2r_init_fc32(NULL, N);
        if (err != ESP_OK) return false;
        dsps_wind_hann_f32(_window, N);
    }

    // [중요 수정] 모든 축별 상태 변수 안전하게 초기화
    memset(_noise_spectrum, 0, 3 * bins * sizeof(float));
    memset(_noise_learned_frames, 0, sizeof(_noise_learned_frames));
    memset(_history_count, 0, sizeof(_history_count));
    memset(_prev_sample, 0, sizeof(_prev_sample));
    memset(_mfcc_history, 0, sizeof(_mfcc_history));
    memset(_biquad_state, 0, sizeof(_biquad_state));

    float min_mel = T20::C10_DSP::MEL_SCALE_CONST * log10f(1.0f + (0.0f / T20::C10_DSP::MEL_FREQ_CONST));
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
            _mel_bank_flat[m * bins + k] = (float)(k - left) / (float)(center - left);
        for (int k = center; k < right && k < bins; k++)
            _mel_bank_flat[m * bins + k] = (float)(right - k) / (float)(right - center + 1e-12f);
    }

    for (int n = 0; n < T20::C10_DSP::MFCC_COEFFS_MAX; n++) {
        for (int k = 0; k < T20::C10_DSP::MEL_FILTERS; k++) {
            _dct_matrix[n][k] = cosf((M_PI / (float)T20::C10_DSP::MEL_FILTERS) * (k + 0.5f) * n);
        }
    }

    if (_cfg.preprocess.filter.enable && _cfg.preprocess.filter.type != EN_T20_FILTER_OFF) {
        float fc = _cfg.preprocess.filter.cutoff_hz_1;
        float f_norm = fmaxf(fc, 1.0f) / fs;
        float q = _cfg.preprocess.filter.q_factor;

        if (_cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
            dsps_biquad_gen_lpf_f32(_biquad_coeffs, f_norm, q);
        } else {
            dsps_biquad_gen_hpf_f32(_biquad_coeffs, f_norm, q);
        }
    }

    Serial.printf("[DSP] Engine Initialized (FFT:%d, Axis:%d)\n", N, (int)_cfg.feature.axis_count);
    return true;
}


bool CL_T20_DspPipeline::processFrame(const float* p_time_in, ST_T20_FeatureVector_t* p_vec_out, uint8_t axis_idx) {
    if (!p_time_in || !p_vec_out || axis_idx >= 3) return false;

	// PSRAM의 raw_buffer 데이터를 연산 전용 SRAM 버퍼로 복사 (SIMD 가속 준비)
    memcpy(_work_frame, p_time_in, sizeof(float) * _current_fft_size);

    _applyPreprocess(_work_frame, axis_idx);
    _applyNoiseGate(_work_frame);
    _applyRuntimeFilter(_work_frame, axis_idx);

	// 벡터 곱셈 (Windowing) - SIMD 활용
    dsps_mul_f32(_work_frame, _window, _work_frame, _current_fft_size, 1, 1, 1);
    _computePowerSpectrum(_work_frame);

    // [추가] 파워 스펙트럼이 계산된 직후, 현재 축의 대역 에너지를 추출하여 구조체에 저장
    // (예: 500Hz ~ 800Hz 대역 감시 시)
    p_vec_out->band_energy[axis_idx] = getBandEnergy(500.0f, 800.0f);


    // [중요 수정] axis_idx 전달
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


void CL_T20_DspPipeline::_applyPreprocess(float* p_data, uint8_t axis_idx) {
    float sum = 0.0f;
    uint16_t N = _current_fft_size;

    // 1. 평균(Mean) 계산
    for (int i = 0; i < N; i++) {
        sum += p_data[i];
    }
    float mean = sum / (float)N;

    // 2. DC 제거 및 순수 진동(AC) RMS 계산
    float sum_sq = 0.0f;
    for (int i = 0; i < N; i++) {
        if (_cfg.preprocess.remove_dc) {
            p_data[i] -= mean;
        }
        sum_sq += (p_data[i] * p_data[i]); // DC가 제거된 값의 제곱
    }

    _current_rms[axis_idx] = sqrtf(sum_sq / (float)N);

    // 3. 축별 독립 Pre-emphasis 적용
    if (_cfg.preprocess.preemphasis.enable) {
        float alpha = _cfg.preprocess.preemphasis.alpha;
        for (int i = 0; i < N; i++) {
            float cur = p_data[i];
            p_data[i] = cur - (alpha * _prev_sample[axis_idx]);
            _prev_sample[axis_idx] = cur;
        }
    }
}


void CL_T20_DspPipeline::_applyNoiseGate(float* p_data) {
    if (!_cfg.preprocess.noise.enable_gate) return;
    float threshold = _cfg.preprocess.noise.gate_threshold_abs;
    uint16_t N = _current_fft_size; // 추가
    for (int i = 0; i < N; i++) { // FFT_SIZE -> N 수정
        if (fabsf(p_data[i]) < threshold) p_data[i] = 0.0f;
    }
}

void CL_T20_DspPipeline::_applyRuntimeFilter(float* p_data, uint8_t axis_idx) {
    if (!_cfg.preprocess.filter.enable || _cfg.preprocess.filter.type == EN_T20_FILTER_OFF) return;

    // [수정] _biquad_state[axis_idx]를 사용하여 축별 필터 상태 유지
    dsps_biquad_f32(p_data, p_data, _current_fft_size, _biquad_coeffs, _biquad_state[axis_idx]);
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
    for (int m = 0; m < T20::C10_DSP::MEL_FILTERS; m++) {
        float sum = 0.0f;
        // [수정] _mel_bank[m] -> _mel_bank_flat + (m * bins)
        dsps_dotprod_f32(_power, _mel_bank_flat + (m * bins), &sum, bins);
        p_log_mel_out[m] = logf(fmaxf(sum, 1e-12f));
    }
}


void CL_T20_DspPipeline::_computeDCT2(const float* p_in, float* p_out) {
	uint16_t dim = _cfg.feature.mfcc_coeffs;
	for (int i = 0; i < dim; i++) {
		float sum = 0.0f;
		dsps_dotprod_f32(p_in, _dct_matrix[i], &sum, T20::C10_DSP::MEL_FILTERS);
		p_out[i] = sum;
	}
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


void CL_T20_DspPipeline::_computeDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta_out) {
	// Window = 2 기준 공식: (2*h[t+2] + h[t+1] - h[t-1] - 2*h[t-2]) / 10
	// 여기서는 v216의 Window=2 간소화 공식을 적용합니다.
	const int t = 2;
	for (int i = 0; i < dim; i++) {
		delta_out[i] = (history[t + 1][i] - history[t - 1][i]) / 2.0f;
	}
}

void CL_T20_DspPipeline::_computeDeltaDelta(const float history[][T20::C10_DSP::MFCC_COEFFS_MAX], uint16_t dim, float* delta2_out) {
	const int t = 2;
	for (int i = 0; i < dim; i++) {
		delta2_out[i] = history[t + 1][i] - (2.0f * history[t][i]) + history[t - 1][i];
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


