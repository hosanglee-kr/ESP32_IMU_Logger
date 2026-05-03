/* ============================================================================
 * File: T430_DspEng_011.cpp
 * Summary: SIMD Optimized Signal Processing Pipeline (Mono-Reduced Hybrid Mode)
 * ============================================================================
 * * [AI 메모: 제공 기능 및 마이그레이션 적용 완료 사항]
 * 1. [LTI 최적화] Beamforming을 파이프라인 맨 앞으로 이동시켜 L/R 필터 연산 통폐합 (CPU 50% 절감).
 * 2. [단절 방어] Pre-emphasis와 Median 필터에 이전 프레임 히스토리(State) 변수를
 * 적용하여 경계선 팝핑(Popping/Click) 스파이크 노이즈 원천 차단.
 * 3. [왜곡 방어] Pre-emphasis -> Noise Gate 순서로 배치하여 델타 스파이크 생성 차단.
 * 4. [포인터 보호] 입력 원본 파형(p_micL, p_micR)은 const로 보호하고 p_output에만 기록.
 * 5. [수학적 맹점] 파괴적인 계단 노이즈를 유발하던 블록 평균 _removeDC() 완전 삭제.
 * 6. [자료형 최적화] int 대신 uint32_t, uint16_t, uint8_t 등 임베디드 고정 정수형 적용.
 * ========================================================================== */

#include "T430_DspEng_011.hpp"
#include "esp_log.h"
#include "dsps_math.h"
#include "dsps_wind.h"
#include <cmath>
#include <cstring>

static const char* G_T430_TAG = "T430_DSP";

T430_DspEngine::T430_DspEngine() {
    _prevPreEmpSample = 0.0f;
    memset(_medianHistory, 0, sizeof(_medianHistory));
}

T430_DspEngine::~T430_DspEngine() {}

bool T430_DspEngine::init() {
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // 1. [기능] FIR 필터 초기화 (Mono 채널 1개로 통폐합)
    _generateFirLpfWindowedSinc(_firLpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_lpf_cutoff);
    dsps_fir_init_f32(&_firInstLpf, _firLpfCoeffs, _firStateLpf, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);

    _generateFirHpfWindowedSinc(_firHpfCoeffs, SmeaConfig::FeatureLimit::FIR_TAPS_CONST, v_cfg.dsp.fir_hpf_cutoff);
    dsps_fir_init_f32(&_firInstHpf, _firHpfCoeffs, _firStateHpf, SmeaConfig::FeatureLimit::FIR_TAPS_CONST);

    // 2. [기능] IIR Notch 필터 계수 계산 (60Hz)
    float v_omega = 2.0f * (float)M_PI * v_cfg.dsp.notch_freq_hz / SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_alpha = sinf(v_omega / v_cfg.dsp.notch_q_factor) / 2.0f;
    float v_a0 = 1.0f + v_alpha;
    _notchCoeffs[0] = 1.0f / v_a0;
    _notchCoeffs[1] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[2] = 1.0f / v_a0;
    _notchCoeffs[3] = (-2.0f * cosf(v_omega)) / v_a0;
    _notchCoeffs[4] = (1.0f - v_alpha) / v_a0;

    // 3. [기능] IIR Notch 필터 2 계수 계산 (120Hz)
    float v_omega2 = 2.0f * (float)M_PI * v_cfg.dsp.notch_freq_2_hz / SmeaConfig::System::SAMPLING_RATE_CONST;
    float v_alpha2 = sinf(v_omega2 / v_cfg.dsp.notch_q_factor) / 2.0f;
    float v_a0_2 = 1.0f + v_alpha2;
    _notch2Coeffs[0] = 1.0f / v_a0_2;
    _notch2Coeffs[1] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    _notch2Coeffs[2] = 1.0f / v_a0_2;
    _notch2Coeffs[3] = (-2.0f * cosf(v_omega2)) / v_a0_2;
    _notch2Coeffs[4] = (1.0f - v_alpha2) / v_a0_2;

    resetFilterStates();
    return true;
}

void T430_DspEngine::resetFilterStates() {
    memset(_wNotch, 0, sizeof(_wNotch));
    memset(_wNotch2, 0, sizeof(_wNotch2));
    memset(_firStateLpf, 0, sizeof(_firStateLpf));
    memset(_firStateHpf, 0, sizeof(_firStateHpf));
    memset(_medianHistory, 0, sizeof(_medianHistory));
    _prevPreEmpSample = 0.0f;

    ESP_LOGI(G_T430_TAG, "DSP Mono states & History reset.");
}

// [주의/방어] p_micL과 p_micR은 원본 훼손을 막기 위해 const로 진입합니다.
void T430_DspEngine::process(const float* p_micL, const float* p_micR, float* p_output, uint32_t p_len) {
    if (p_len > SmeaConfig::System::FFT_SIZE_CONST) return;
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();

    // ------------------------------------------------------------------------
    // [Step 1] Broadside Beamforming (가장 먼저 수행하여 L/R을 Mono로 병합. 연산량 50% 절감)
    // ------------------------------------------------------------------------
    _applyBeamforming(p_micL, p_micR, _workBufA, p_len, v_cfg.dsp.beamforming_gain);

    // ------------------------------------------------------------------------
    // [Step 2] Median Filter (스파이크 1차 제거)
    // ------------------------------------------------------------------------
    _applyMedianFilter(_workBufA, v_cfg.dsp.median_window, p_len);

    // ------------------------------------------------------------------------
    // [Step 3] IIR Notch 필터 (60/120Hz 전기 노이즈 제거) -> FIR 필터링 (위상 보정)
    // ------------------------------------------------------------------------
    dsps_biquad_f32(_workBufA, _workBufB, p_len, _notchCoeffs, _wNotch);    // Notch 1
    dsps_biquad_f32(_workBufB, _workBufA, p_len, _notch2Coeffs, _wNotch2);  // Notch 2
    dsps_fir_f32(&_firInstHpf, _workBufA, _workBufB, p_len);                // FIR HPF (DC 완벽 제거)
    dsps_fir_f32(&_firInstLpf, _workBufB, p_output, p_len);                 // FIR LPF -> 최종 p_output 기록

    // ------------------------------------------------------------------------
    // [Step 4] Pre-Emphasis (프레임 찢김 방어 적용된 미분기)
    // ------------------------------------------------------------------------
    _applyPreEmphasis(p_output, p_len, v_cfg.dsp.pre_emphasis_alpha);

    // ------------------------------------------------------------------------
    // [Step 5] Noise Gate (가장 마지막에 배치하여 스파이크 노이즈 생성 차단)
    // ------------------------------------------------------------------------
    _applyNoiseGate(p_output, p_len, v_cfg.dsp.noise_gate_thresh);
}

void T430_DspEngine::_applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len, float p_gain) {
    dsps_add_f32(p_L, p_R, p_out, p_len, 1, 1, 1);
    dsps_mulc_f32(p_out, p_out, p_len, p_gain, 1, 1);
}

void T430_DspEngine::_applyMedianFilter(float* p_data, uint8_t p_windowSize, uint32_t p_len) {
    if (p_windowSize < 3) return;

    if (p_windowSize > SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST) {
        p_windowSize = SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST;
    }

    uint8_t v_half = p_windowSize / 2;
    float v_tempBuf[SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST];

    // [방어/교정] 이전 프레임에서 보존해둔 꼬리 데이터를 활용해 이음매 없는 윈도우 스캔
    for (uint32_t i = 0; i < p_len; i++) {
        for (uint8_t j = 0; j < p_windowSize; j++) {
            int32_t v_idx = (int32_t)i + j - v_half;
            if (v_idx < 0) {
                // 이전 프레임의 데이터 참조 (음수 인덱싱 오염 방어)
                v_tempBuf[j] = _medianHistory[(v_half + v_idx) % v_half];
            } else if (v_idx >= (int32_t)p_len) {
                v_tempBuf[j] = p_data[p_len - 1];
            } else {
                v_tempBuf[j] = p_data[v_idx];
            }
        }

        // 정렬 수행
        for (uint8_t j = 1; j < p_windowSize; j++) {
            float v_key = v_tempBuf[j];
            int32_t k = j - 1;
            while (k >= 0 && v_tempBuf[k] > v_key) {
                v_tempBuf[k + 1] = v_tempBuf[k];
                k--;
            }
            v_tempBuf[k + 1] = v_key;
        }
        p_data[i] = v_tempBuf[v_half];
    }

    // [방어/교정] 다음 프레임을 위해 현재 프레임의 맨 끝단 데이터 히스토리 보존
    for (uint8_t i = 0; i < v_half; i++) {
        _medianHistory[i] = p_data[p_len - v_half + i];
    }
}

void T430_DspEngine::_applyPreEmphasis(float* p_data, uint32_t p_len, float p_alpha) {
    if (p_len == 0) return;

    // 배열 끝에서부터 시작하여 덮어쓰기 오염을 방지
    for (uint32_t i = p_len - 1; i > 0; i--) {
        p_data[i] = p_data[i] - p_alpha * p_data[i - 1];
    }

    // [방어/교정] 프레임 찢김(Tearing)을 막기 위해 이전 프레임의 마지막 샘플 적용
    p_data[0] = p_data[0] - p_alpha * _prevPreEmpSample;

    // 다음 프레임을 위해 현재 프레임의 최종 샘플 저장
    _prevPreEmpSample = p_data[p_len - 1];
}

void T430_DspEngine::_applyNoiseGate(float* p_data, uint32_t p_len, float p_gateThresh) {
    for (uint32_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_gateThresh) p_data[i] = 0.0f;
    }
}

void T430_DspEngine::_generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    float v_normalizedCutoff = p_cutoffHz / SmeaConfig::System::SAMPLING_RATE_CONST;
    alignas(16) float v_win[SmeaConfig::FeatureLimit::FIR_TAPS_CONST] = {0};

    dsps_wind_blackman_f32(v_win, p_taps);
    float v_M = (float)(p_taps - 1);
    float v_sum = 0.0f;

    for (uint16_t i = 0; i < p_taps; i++) {
        float v_x = (float)i - (v_M / 2.0f);
        if (fabsf(v_x) < SmeaConfig::System::MATH_EPSILON_CONST) p_coeffs[i] = 2.0f * v_normalizedCutoff;
        else p_coeffs[i] = sinf(2.0f * (float)M_PI * v_normalizedCutoff * v_x) / ((float)M_PI * v_x);
        p_coeffs[i] *= v_win[i];
        v_sum += p_coeffs[i];
    }
    for (uint16_t i = 0; i < p_taps; i++) p_coeffs[i] /= v_sum;
}

void T430_DspEngine::_generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz) {
    _generateFirLpfWindowedSinc(p_coeffs, p_taps, p_cutoffHz);
    uint16_t v_center = (p_taps - 1) / 2; // (static_assert로 인해 중앙값 대칭성 확정 보장됨)
    for (uint16_t i = 0; i < p_taps; i++) {
        p_coeffs[i] = -p_coeffs[i];
    }
    p_coeffs[v_center] += 1.0f;
}
