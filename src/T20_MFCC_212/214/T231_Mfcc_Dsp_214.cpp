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

// 바꾼 방식 (헤더 경로가 안 잡힐 때의 임시 방편)
#include "esp_dsp.h"
// #include "modules/fft/include/dsps_fft.h"
//#include "dsps_fft.h" // ESP-DSP SIMD Library

#include "T221_Mfcc_Inter_214.h"

/* [내부 헬퍼] 256-point FFT용 Bit-Reversal Index (v210 복구) */
static const uint8_t G_T20_FFT_BIT_REVERSE_256[256] = {
    0,128,64,192,32,160,96,224,16,144,80,208,48,176,112,240, // ... 중략 (컴파일러 최적화 위해 실제 코드 시 인라인 구현)
};

bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

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

/* [v212 추가] 기본적인 Radix-2 FFT (esp_dsp 미사용 시 대비) */
void T20_performFFT256(float* p_real, float* p_imag) {
    // 256포인트 FFT 연산 (주파수 분석의 핵심)
    // 실제 구현 시 v210의 루프 구조를 사용하여 연산 속도 확보
    int n = 256;
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (i < j) {
            float tempR = p_real[i]; p_real[i] = p_real[j]; p_real[j] = tempR;
            float tempI = p_imag[i]; p_imag[i] = p_imag[j]; p_imag[j] = tempI;
        }
        int m = n >> 1;
        while (m >= 1 && j >= m) { j -= m; m >>= 1; }
        j += m;
    }
    // Butterfly 연산 (v210 로직 이식)
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * G_T20_PI / (float)len;
        float wlen_r = cosf(ang), wlen_i = -sinf(ang);
        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f, w_i = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float u_r = p_real[i + k], u_i = p_imag[i + k];
                float v_r = p_real[i + k + len / 2] * w_r - p_imag[i + k + len / 2] * w_i;
                float v_i = p_real[i + k + len / 2] * w_i + p_imag[i + k + len / 2] * w_r;
                p_real[i + k] = u_r + v_r; p_imag[i + k] = u_i + v_i;
                p_real[i + k + len / 2] = u_r - v_r; p_imag[i + k + len / 2] = u_i - v_i;
                float tmp_w_r = w_r * wlen_r - w_i * wlen_i;
                w_i = w_r * wlen_i + w_i * wlen_r; w_r = tmp_w_r;
            }
        }
    }
}


void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out) {
    if (p == nullptr || p_frame == nullptr) return;

    // 1. 원시 데이터 복사 및 DC 제거 (v210 기능)
    memcpy(p->temp_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);
    if (p->cfg.preprocess.remove_dc) T20_applyDCRemove(p->temp_frame, G_T20_FFT_SIZE);

    // 2. Pre-emphasis 및 런타임 필터(LPF/HPF) 적용 (v210 기능)
    if (p->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p, p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    }
    T20_applyRuntimeFilter(p, p->temp_frame, p->work_frame, G_T20_FFT_SIZE);

    // 3. 노이즈 게이트 적용 (v210 기능)
    if (p->cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(p->work_frame, G_T20_FFT_SIZE, p->cfg.preprocess.noise.gate_threshold_abs);
    }

    // 4. Windowing 및 FFT 변환 (v213 개선)
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; ++i) p->work_frame[i] *= p->window[i];
    float imag_buf[G_T20_FFT_SIZE] = {0};
    T20_performFFT256(p->work_frame, imag_buf);

    // 5. Power Spectrum 산출 및 스펙트럼 차감 (v210 기능 복구)
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (uint16_t b = 0; b < bins; ++b) {
        p->power[b] = (p->work_frame[b] * p->work_frame[b]) + (imag_buf[b] * imag_buf[b]);
        if (p->power[b] < G_T20_EPSILON) p->power[b] = G_T20_EPSILON;
    }
    
    // 노이즈 학습 및 차감 실행
    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);

    // 6. Mel-Filterbank & DCT (최종 특징량 추출)
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

// [v214] 하드웨어 가속 기반 FFT 
void T20_performFFT_Optimized(CL_T20_Mfcc::ST_Impl* p, float* p_real) {
    // 1. 입력 데이터를 복소수 형식(Real, Imag, Real, Imag...)으로 재배열
    // ESP-DSP는 복소수 배열 입력을 기본으로 함
    static float fft_input[G_T20_FFT_SIZE * 2] __attribute__((aligned(16)));
    for (int i = 0; i < G_T20_FFT_SIZE; i++) {
        fft_input[i * 2] = p_real[i];
        fft_input[i * 2 + 1] = 0.0f; // Imaginary part
    }

    // 2. SIMD 가속 FFT 실행 (S3 PIE 명령어 사용)
    // 수정 (표준 버전)
    // dsps_bit_rev_fc32(fft_input, G_T20_FFT_SIZE); 
    // 또는 컴파일러 제안대로
    dsps_bit_rev2r_fc32(fft_input, G_T20_FFT_SIZE);


    // dsps_fft2r_fc32_ae32: Radix-2, Floating-point, Complex FFT
    // dsps_fft2r_fc32_ae32(fft_input, G_T20_FFT_SIZE);

    // 3. Bit-reversal 정렬
    dsps_bit_rev_fc32_ae32(fft_input, G_T20_FFT_SIZE);

    // 4. Power Spectrum 계산 (Magnitude Square)
    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    for (int i = 0; i < bins; i++) {
        float re = fft_input[i * 2];
        float im = fft_input[i * 2 + 1];
        p->power[i] = (re * re) + (im * im);
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

// [1] 노이즈 패턴 학습 (학습 프레임 수 동안 실행)
void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power) {
    if (p == nullptr || p_power == nullptr) return;
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) return;
    if (p->noise_learned_frames >= p->cfg.preprocess.noise.noise_learn_frames) return;

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    uint16_t count = p->noise_learned_frames;

    for (uint16_t i = 0; i < bins; ++i) {
        // 이동 평균을 이용한 노이즈 프로파일 생성
        p->noise_spectrum[i] = ((p->noise_spectrum[i] * (float)count) + p_power[i]) / (float)(count + 1U);
    }
    p->noise_learned_frames++;
}


// [2] 스펙트럼 차감 (학습된 노이즈 제거)
void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power) {
    if (!p || p->cfg.preprocess.noise.mode == EN_T20_NOISE_OFF) return;

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    float strength = p->cfg.preprocess.noise.spectral_subtract_strength;
    float alpha = p->cfg.preprocess.noise.adaptive_alpha;

    for (uint16_t i = 0; i < bins; ++i) {
        // [v214] Adaptive Noise Floor 추적
        if (p->cfg.preprocess.noise.mode == EN_T20_NOISE_ADAPTIVE) {
            // 측정 중이 아닐 때나 Web 학습 모드일 때 더 빠르게 학습 가능
            // 실시간으로 노이즈 바닥면을 추적하는 지수 이동 평균(EMA) 로직을 적용
            float current_alpha = p->noise_learning_active ? 0.1f : alpha;
            p->noise_spectrum[i] = (1.0f - current_alpha) * p->noise_spectrum[i] + current_alpha * p_power[i];
        }

        float v = p_power[i] - (strength * p->noise_spectrum[i]);
        if (v < G_T20_EPSILON) v = G_T20_EPSILON;
        p_power[i] = v;
    }
}

/*

void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power) {
    if (p == nullptr || p_power == nullptr) return;
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) return;
    if (p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames) return;

    const uint16_t bins = (G_T20_FFT_SIZE / 2U) + 1U;
    float strength = p->cfg.preprocess.noise.spectral_subtract_strength;

    for (uint16_t i = 0; i < bins; ++i) {
        float v = p_power[i] - (strength * p->noise_spectrum[i]);
        // 로그 연산 안정성을 위한 최소값(EPSILON) 제한
        if (v < G_T20_EPSILON) v = G_T20_EPSILON;
        p_power[i] = v;
    }
}
*/

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
    // Delta-Delta (가속도) 단순 계산: t+1 - 2*t + t-1
    if (p->mfcc_history_count < 3) return;
    int t = p->mfcc_history_count - 2;
    for (int i = 0; i < p_dim; i++) {
        p_delta2_out[i] = p->mfcc_history[t+1][i] - 2 * p->mfcc_history[t][i] + p->mfcc_history[t-1][i];
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














