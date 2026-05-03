/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. [방어] LTI 법칙 최적화: FIR/IIR 필터는 선형 시불변(LTI)이므로, L/R 채널을
 * 각각 연산하지 않고 Beamforming(L+R)을 먼저 수행하여 1채널로 만든 뒤 필터링을
 * 1회만 수행한다. (CPU 및 필터 상태 메모리 50% 절감)
 * 2. [방어] 프레임 단절(Tearing) 방어: 블록 단위 스트리밍의 한계를 극복하기 위해,
 * Pre-emphasis 및 Median 필터는 반드시 이전 프레임의 꼬리(History) 데이터를
 * 보존하는 상태 변수를 유지하여 팝핑(Popping) 노이즈를 원천 차단한다.
 * 3. [방어] FIR 짝수 탭 붕괴 트랩: 스펙트럼 반전 기반 HPF 생성 시 탭 수가 짝수면
 * 대칭성이 파괴되므로, static_assert를 통해 컴파일 타임에 이를 원천 차단한다.
 * 4. [방어] 포인터 오염 방지: 입력 p_micL, p_micR은 반드시 const로 선언하여,
 * T440이 위상(IPD) 분석 시 사용할 원본 파형이 훼손(In-place 덮어쓰기)되지 않도록 한다.
 * 5. [네이밍 및 자료형 엄수]: private(_), 매개변수(p_), 로컬변수(v_) / <cstdint> 사용
 * ============================================================================
 * File: T430_DspEng_011.hpp
 * Summary: SIMD Optimized Signal Processing Pipeline (Mono-Reduced Hybrid Mode)
 * * [AI 메모: 제공 기능 요약]
 * 1. Beamforming -> Median -> IIR Notch -> FIR HPF/LPF -> Pre-emphasis -> Noise Gate 순서 보장.
 * 2. 핑퐁(Ping-pong) 임시 버퍼를 활용한 ESP-DSP 필터 연산의 메모리 오염 원천 방어.
 * 3. 블록 단위 DC Removal 삭제 및 FIR HPF 전면 의존으로 계단(Step) 노이즈 주입 차단.
 * ========================================================================== */
#pragma once

#include "T410_Def_011.hpp"     // 동적/정적 설정 상수
#include "T420_Types_011.hpp"      // 통신 패킷 및 데이터 슬롯 규격
#include "T415_ConfigMgr_011.hpp"  // 런타임 JSON 설정 매니저
#include "dsps_fft2r.h"
#include "dsps_biquad.h"
#include "dsps_fir.h"
#include <cstdint>                 // 임베디드 표준 고정 길이 정수형 적용

// [컴파일 타임 방어벽] FIR 하이패스 필터의 스펙트럼 반전을 위해 탭 수는 반드시 홀수여야 함
static_assert(SmeaConfig::FeatureLimit::FIR_TAPS_CONST % 2 == 1, "[CRITICAL ERROR] FIR_TAPS_CONST MUST be an odd number for HPF Spectral Inversion!");

class T430_DspEngine {
private:
    // ========================================================================
    // [1] 필터 계수 (Filter Coefficients) - 16바이트 정렬 엄수
    // ========================================================================
    alignas(16) float _notchCoeffs[5];                                              // 60Hz 1차 노치 필터 계수
    alignas(16) float _notch2Coeffs[5];                                             // 120Hz 2차 하모닉 노치 필터 계수
    alignas(16) float _firLpfCoeffs[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];  // Low-Pass FIR 필터 계수 (+3은 SIMD 패딩)
    alignas(16) float _firHpfCoeffs[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];  // High-Pass FIR 필터 계수

    // ========================================================================
    // [2] 필터 상태 보존 (Filter States) - LTI 최적화로 1채널(Mono)로 50% 통폐합
    // ========================================================================
    alignas(16) float _wNotch[2];                                                   // 60Hz IIR 상태 딜레이
    alignas(16) float _wNotch2[2];                                                  // 120Hz IIR 상태 딜레이

    alignas(16) float _firStateLpf[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];   // LPF 지연 상태 버퍼
    alignas(16) float _firStateHpf[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];   // HPF 지연 상태 버퍼

    fir_f32_t _firInstHpf;                                                          // ESP-DSP HPF 인스턴스
    fir_f32_t _firInstLpf;                                                          // ESP-DSP LPF 인스턴스

    // ========================================================================
    // [3] 프레임 경계 보존 (Frame Boundary History) - 단절(Tearing) 노이즈 방어용
    // ========================================================================
    float _prevPreEmpSample;                                                        // 프리엠파시스 연산 시 x[-1] 참조를 위한 이전 프레임 마지막 샘플

    // Median 필터 경계 이음매 처리를 위한 N/2 히스토리 버퍼 (최대 상한선 기준 정적 할당)
    alignas(16) float _medianHistory[SmeaConfig::DspLimit::MAX_MEDIAN_WINDOW_CONST / 2];

    // ========================================================================
    // [4] 인플레이스(In-place) 연산 오염 방어용 핑퐁 스크래치 버퍼
    // ========================================================================
    alignas(16) float _workBufA[SmeaConfig::System::FFT_SIZE_CONST];
    alignas(16) float _workBufB[SmeaConfig::System::FFT_SIZE_CONST];

public:
    T430_DspEngine();
    ~T430_DspEngine();

    // 동적 설정(Config)을 반영하여 계수를 생성하고 구조체를 초기화함
    bool init();

    // 상태 전이(READY -> MONITORING 등) 시 묵은 데이터로 인한 발산을 막기 위해 호출
    void resetFilterStates();

    // [방어] 입력 p_micL/R은 const로 막아 원본 훼손을 차단하고, 최종 결과는 p_output에 담아 반환
    void process(const float* p_micL, const float* p_micR, float* p_output, uint32_t p_len);

private:
    void _applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len, float p_gain);
    void _applyMedianFilter(float* p_data, uint8_t p_windowSize, uint32_t p_len);   // 자료형 uint8_t로 최적화
    void _applyPreEmphasis(float* p_data, uint32_t p_len, float p_alpha);
    void _applyNoiseGate(float* p_data, uint32_t p_len, float p_gateThresh);

    // FIR Sinc 함수 생성 로직
    void _generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);
    void _generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);

    // [주의] 파괴적인 블록 평균 방식의 _removeDC() 함수는 영구적으로 삭제되었습니다. (FIR HPF에 전면 위임)
};
