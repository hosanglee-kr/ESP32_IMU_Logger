/* ============================================================================
 * File: T231_Mfcc_Dsp_214.cpp
 * Summary: MFCC 알고리즘 및 DSP 파이프라인 (v210 기능 완전 복구)
 * * [v212 구현 및 점검 사항]
 * 1. 256-point Radix-2 FFT 로직 추가 (기존 placeholder 치환)
 * 2. Pre-emphasis, Windowing, Mel-Filterbank, DCT 연산 최적화
 * 3. Delta/Delta-Delta (39차 벡터) 계산 이력 관리 완벽 매핑
 * 4. EPSILON 처리를 통한 로그 연산 수치 안정성 확보
 ============================================================================ */
 
 /**
 * [v214 튜닝 가이드 - DSP]
 * 1. Adaptive Alpha: 값이 클수록 환경 변화에 빨리 대응하지만 신호의 저주파 성분을 노이즈로 오인할 수 있음.
 * 2. SIMD 가속: 반드시 dsps_fft2r_fc32_ae32를 사용하여 Core 1 부하를 20% 이하로 유지할 것.
 */
 
 /* [v214 잔여 과제]
 * - TO-DO: 현재 DCT 연산도 SIMD 가속 버전(dsps_dct_f32)으로 교체 고려 필요.
 */
 


#include <math.h>
#include <string.h>

#include "esp_dsp.h"

#include "T221_Mfcc_Inter_214.h"




// [v214] FFT 및 DSP 엔진 초기화
bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    
    static bool dsp_initialized = false;
    if (!dsp_initialized) {
        // ESP-DSP Radix-2 전용 초기화 (S3 최적화 테이블 생성)
        esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
        if (err != ESP_OK) return false;
        dsp_initialized = true;
    }

    // 윈도우 계수 생성
    T20_buildHammingWindow(p);

    // 버퍼 초기화
    memset(p->noise_spectrum, 0, sizeof(p->noise_spectrum));
    memset(p->log_mel, 0, sizeof(p->log_mel));
    memset(p->mel_bank, 0, sizeof(p->mel_bank));
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;

    // Mel Filterbank 가중치 계산 루프
    for (uint16_t m = 0; m < G_T20_MEL_FILTERS; ++m) {
        uint16_t left   = (uint16_t)((m * bins) / (G_T20_MEL_FILTERS + 1U));
        uint16_t center = (uint16_t)(((m + 1U) * bins) / (G_T20_MEL_FILTERS + 1U));
        uint16_t right  = (uint16_t)(((m + 2U) * bins) / (G_T20_MEL_FILTERS + 1U));

        // 인덱스 안전성 보장
        if (center <= left) center = left + 1U;
        if (right <= center) right = center + 1U;
        if (right > bins) right = bins;

        // 삼각형 오르막 (0 -> 1)
        for (uint16_t k = left; k < center && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(k - left) / (float)(center - left);
        }
        // 삼각형 내리막 (1 -> 0)
        for (uint16_t k = center; k < right && k < bins; ++k) {
            p->mel_bank[m][k] = (float)(right - k) / (float)(right - center + G_T20_EPSILON);
        }
    }

    return T20_configureRuntimeFilter(p);
}



// [v214] 통합 MFCC 추출 엔진
void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out) {
    if (!p || !p_frame || !p_mfcc_out) return;

    // 1. 전처리 (DC 제거 및 Pre-emphasis)
    memcpy(p->temp_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);
    if (p->cfg.preprocess.remove_dc) T20_applyDCRemove(p->temp_frame, G_T20_FFT_SIZE);
    if (p->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p, p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    }

    // 2. 필터 및 윈도우 적용
    T20_applyRuntimeFilter(p, p->temp_frame, p->work_frame, G_T20_FFT_SIZE);
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) p->work_frame[i] *= p->window[i];

    // 3. SIMD 가속 FFT (결과는 p->power에 저장)
    T20_performFFT_Optimized(p, p->work_frame);

    // 4. 적응형 노이즈 제거 (v214 핵심)
    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);

    // 5. 특징량 추출 (Mel 에너지 -> DCT)
    T20_applyMelFilterbank(p, p->power, p->log_mel);
    T20_computeDCT2(p->log_mel, p_mfcc_out, p->cfg.feature.mel_filters, p->cfg.feature.mfcc_coeffs);
}


void T20_processTask(void* p_arg) {
    CL_T20_Mfcc::ST_Impl* p = (CL_T20_Mfcc::ST_Impl*)p_arg;
    ST_T20_FrameMessage_t msg;
    
    float dsp_input[G_T20_FFT_SIZE]; // 분석용 지역 버퍼

    for (;;) {
        if (xQueueReceive(p->frame_queue, &msg, portMAX_DELAY) == pdTRUE) {
            // [v213 핵심] Sliding Window 정렬
            // 링버퍼 형태로 저장된 데이터에서 최근 256개를 순서대로 추출
            uint32_t current_pos = p->active_sample_index;
            for (uint16_t i = 0; i < G_T20_FFT_SIZE; i++) {
                uint16_t read_idx = (current_pos - G_T20_FFT_SIZE + i) % G_T20_FFT_SIZE;
                dsp_input[i] = p->frame_buffer[0][read_idx];
            }

            // 추출된 윈도우로 MFCC 연산 수행
            T20_processOneFrame(p, dsp_input, G_T20_FFT_SIZE);
        }
    }
}



// [v214] S3 PIE 가속 FFT 연산
void T20_performFFT_Optimized(CL_T20_Mfcc::ST_Impl* p, float* p_real) {
    if (!p || !p_real) return;

    // 16바이트 정렬된 복소수 버퍼 사용 (S3 SIMD 필수 조건)
    static float fft_input[G_T20_FFT_SIZE * 2] __attribute__((aligned(16)));
    
    for (int i = 0; i < G_T20_FFT_SIZE; i++) {
        fft_input[i * 2] = p_real[i];
        fft_input[i * 2 + 1] = 0.0f;
    }

    // Radix-2 Complex FFT 실행
    dsps_fft2r_fc32(fft_input, G_T20_FFT_SIZE);
    dsps_bit_rev2r_fc32(fft_input, G_T20_FFT_SIZE);

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (int i = 0; i < bins; i++) {
        float re = fft_input[i * 2];
        float im = fft_input[i * 2 + 1];
        float mag_sq = (re * re) + (im * im);
        
        // 로그 연산 에러 방지를 위한 하한선 처리
        p->power[i] = (mag_sq < G_T20_EPSILON) ? G_T20_EPSILON : mag_sq;
    }
}



// [B-1] Delta 계산: 특징량의 시간에 따른 기울기 추출
void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, uint16_t p_delta_window, float* p_delta_out) {
    if (!p || p->mfcc_history_count < G_T20_MFCC_HISTORY) return;

    // 회귀 분석 기반 Delta 공식 (N=2 기준)
    // delta = (sum(n * (c[t+n] - c[t-n]))) / (2 * sum(n^2))
    float denominator = 0;
    for (int n = 1; n <= p_delta_window; n++) denominator += (n * n);
    denominator *= 2.0f;

    for (uint16_t i = 0; i < p_dim; i++) {
        float numerator = 0;
        for (int n = 1; n <= p_delta_window; n++) {
            // history index 2가 현재(t), 0~1이 과거, 3~4가 미래 프레임
            numerator += n * (p->mfcc_history[2 + n][i] - p->mfcc_history[2 - n][i]);
        }
        p_delta_out[i] = numerator / (denominator + G_T20_EPSILON);
    }
}

// [B-2] 39차 통합 벡터 빌드 (MFCC 13 + Delta 13 + Delta-Delta 13)
void T20_buildVector(const float* p_mfcc, const float* p_delta, const float* p_delta2, uint16_t p_dim, float* p_out_vec) {
    if (!p_mfcc || !p_delta || !p_delta2 || !p_out_vec) return;

    // 순차적으로 39차 벡터 채우기
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i] = p_mfcc[i];          // 0~12
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i + p_dim] = p_delta[i]; // 13~25
    for (uint16_t i = 0; i < p_dim; i++) p_out_vec[i + (p_dim * 2)] = p_delta2[i]; // 26~38
}


void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p) {
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->window[i] = 0.54f - 0.46f * cosf((2.0f * G_T20_PI * i) / (G_T20_FFT_SIZE - 1U));
    }
}

void T20_applyDCRemove(float* p_data, uint16_t p_len) {
    float sum = 0;
    for (uint16_t i = 0; i < p_len; i++) sum += p_data[i];
    float mean = sum / p_len;
    for (uint16_t i = 0; i < p_len; i++) p_data[i] -= mean;
}

void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha) {
    float prev = p->prev_raw_sample;
    for (uint16_t i = 0; i < p_len; i++) {
        float cur = p_data[i];
        p_data[i] = cur - (p_alpha * prev);
        prev = cur;
    }
    p->prev_raw_sample = prev;
}

void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs) {
    for (uint16_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_threshold_abs) p_data[i] = 0.0f;
    }
}


// [v214] 통합 노이즈 학습 로직
void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power) {
    if (!p || !p_power || p->cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    bool is_learning = p->noise_learning_active || (p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames);

    if (!is_learning) return;

    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_ADAPTIVE) {
        // Adaptive: 지수 이동 평균 추적
        float alpha = p->noise_learning_active ? 0.2f : p->cfg.preprocess.noise.adaptive_alpha;
        for (uint16_t i = 0; i < bins; ++i) {
            p->noise_spectrum[i] = (1.0f - alpha) * p->noise_spectrum[i] + alpha * p_power[i];
        }
    } else {
        // Fixed: 초기 베이스라인 누적 평균
        float count = (float)p->noise_learned_frames;
        for (uint16_t i = 0; i < bins; ++i) {
            p->noise_spectrum[i] = ((p->noise_spectrum[i] * count) + p_power[i]) / (count + 1.0f);
        }
    }

    if (p->noise_learned_frames < 0xFFFF) p->noise_learned_frames++;
}



/* ============================================================================
 * Function: T20_applySpectralSubtraction
 * Summary: 학습된 노이즈 스펙트럼을 이용한 신호 정제 (v214 최종본)
 * * [v214 튜닝 가이드]
 * 1. strength: 보통 1.0f를 사용하나, 노이즈 제거 후 신호가 너무 왜곡되면 
 * 0.7~0.8로 낮추고, 잔류 노이즈가 많으면 1.2~1.5로 높여 조정하십시오.
 * 2. G_T20_EPSILON: 로그 연산(logf) 시 -inf 발생을 막는 최소값입니다. 
 * 시스템 노이즈 플로어보다 약간 낮게 설정하는 것이 좋습니다.
 ============================================================================ */

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power) {
    // 1. 유효성 검사 및 모드 확인
    if (p == nullptr || p_power == nullptr) return;
    
    // 노이즈 제거 모드가 OFF라면 즉시 종료
    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) {
        return;
    }

    /* [v214 점검 사항] 
     * FIXED 모드일 경우 충분한 프레임이 학습되었는지 확인.
     * 학습이 덜 된 상태에서 차감을 수행하면 신호 왜곡이 심해질 수 있음.
     */
    if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_FIXED && 
        p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    const float strength = p->cfg.preprocess.noise.spectral_subtract_strength;

    /* [v214 최적화 연산 루프] */
    for (uint16_t i = 0; i < bins; ++i) {
        // [식] Clean_Power = Raw_Power - (Strength * Noise_Floor)
        float noise_val = p->noise_spectrum[i];
        float sub_result = p_power[i] - (strength * noise_val);

        // [수치 안정성 확보] 
        // 결과값이 음수가 되거나 너무 작아지면 logf() 에러를 유발하므로 EPSILON으로 클램핑
        if (sub_result < G_T20_EPSILON) {
            sub_result = G_T20_EPSILON;
        }

        p_power[i] = sub_result;
    }

    /* [잔여 과제 - TODO]
     * 향후 과도한 차감으로 인한 'Musical Noise' 현상 발생 시 
     * Spectral Over-subtraction 또는 Noise Floor Smoothing 알고리즘 추가 고려.
     */
}


// [3] 멜 필터뱅크 적용 (에너지 맵핑 및 로그 변환)
void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out) {
    if (p == nullptr || p_power == nullptr || p_log_mel_out == nullptr) return;
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;

    for (uint16_t m = 0; m < G_T20_MEL_FILTERS; ++m) {
        float sum = 0.0f;
        for (uint16_t k = 0; k < bins; ++k) {
            sum += p_power[k] * p->mel_bank[m][k];
        }
        if (sum < G_T20_EPSILON) sum = G_T20_EPSILON;
        p_log_mel_out[m] = logf(sum); // 로그 멜 스펙트럼
    }
}

// [4] DCT-II 연산 (최종 MFCC 계수 추출)
void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len) {
    if (p_in == nullptr || p_out == nullptr) return;

    for (uint16_t n = 0; n < p_out_len; ++n) {
        float sum = 0.0f;
        for (uint16_t k = 0; k < p_in_len; ++k) {
            sum += p_in[k] * cosf((G_T20_PI / (float)p_in_len) * ((float)k + 0.5f) * (float)n);
        }
        p_out[n] = sum;
    }
}

void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc, uint16_t p_dim) {
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count++], p_mfcc, sizeof(float) * p_dim);
    } else {
        for (int i = 1; i < G_T20_MFCC_HISTORY; i++) memcpy(p->mfcc_history[i-1], p->mfcc_history[i], sizeof(float) * p_dim);
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY-1], p_mfcc, sizeof(float) * p_dim);
    }
}

void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, uint16_t p_dim, float* p_delta2_out) {
    // Delta-Delta 계산을 위해 최소 3개 이상의 프레임이 필요하며, 
    // Delta와 동일하게 히스토리 중앙(Index 2)을 기준으로 함
    if (!p || p->mfcc_history_count < 5) return; 

    const int t = 2; // History 중앙 인덱스
    for (uint16_t i = 0; i < p_dim; i++) {
        // 가속도 공식: c[3] - 2*c[2] + c[1]
        p_delta2_out[i] = p->mfcc_history[t + 1][i] - (2.0f * p->mfcc_history[t][i]) + p->mfcc_history[t - 1][i];
    }
}


// [5] 런타임 필터 설정 (HPF/LPF 계수 계산)
bool T20_configureRuntimeFilter(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
    memset(p->biquad_state, 0, sizeof(p->biquad_state));

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        return true;
    }

    float fs = p->cfg.feature.sample_rate_hz;
    float fc = p->cfg.preprocess.filter.cutoff_hz_1;
    if (fs <= 0.0f) return false;
    if (fc <= 0.0f) fc = 1.0f;

    float x = expf(-2.0f * G_T20_PI * fc / fs);

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        p->biquad_coeffs[0] = 1.0f - x;
        p->biquad_coeffs[1] = x;
    } else { // HPF 등
        p->biquad_coeffs[0] = (1.0f + x) * 0.5f;
        p->biquad_coeffs[1] = x;
    }
    return true;
}


// [6] 실시간 샘플 필터링 수행
void T20_applyRuntimeFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len) {
    if (p == nullptr || p_in == nullptr || p_out == nullptr) return;

    if (!p->cfg.preprocess.filter.enable || p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    float s0 = p->biquad_state[0];
    float s1 = p->biquad_state[1];
    float a0 = p->biquad_coeffs[0];
    float a1 = p->biquad_coeffs[1];

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        for (uint16_t i = 0; i < p_len; ++i) {
            s0 = (a0 * p_in[i]) + (a1 * s0);
            p_out[i] = s0;
        }
    } else { // HPF 로직
        for (uint16_t i = 0; i < p_len; ++i) {
            float y = a0 * (p_in[i] - s1) + (a1 * s0);
            s1 = p_in[i];
            s0 = y;
            p_out[i] = y;
        }
    }
    p->biquad_state[0] = s0;
    p->biquad_state[1] = s1;
}


/*
// [v214] 노이즈 학습 제어 백엔드 
void T20_processNoiseLearning(CL_T20_Mfcc::ST_Impl* p, const float* p_power) {
    if (!p || !p->noise_learning_active) return;

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    
    // 학습 모드일 때는 높은 Alpha 값을 사용하여 노이즈 바닥면을 빠르게 추적
    float learning_alpha = 0.2f; 
    for (uint16_t i = 0; i < bins; ++i) {
        p->noise_spectrum[i] = (1.0f - learning_alpha) * p->noise_spectrum[i] + learning_alpha * p_power[i];
    }
    
    // 특정 프레임 이상 학습되면 자동으로 플래그를 내리거나 상태를 보고할 수 있음
    p->noise_learned_frames++;
}
*/














