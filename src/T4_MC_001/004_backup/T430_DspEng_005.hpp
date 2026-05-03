/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. (실수) 필터 제자리(In-place) 연산 오염 
 * -> (방어) FIR/IIR 필터는 반드시 입출력 버퍼를 분리(핑퐁)하거나, 시계열이 
 * 유지되도록 역순 루프(Pre-emphasis)를 적용한다.
 * 2. (실수) C++ -ffast-math 최적화로 인한 예외처리 무력화 
 * -> (방어) 부동소수점 0 나누기 방어는 fabsf(val) < 1e-6f 검사를 쓰고, 
 * NaN/Inf 유입은 IEEE-754 비트 마스킹으로 하드웨어 컷을 수행한다.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * ============================================================================
 * File: T430_DspEng_005.hpp
 * Summary: SIMD Optimized Signal Processing Pipeline (Hybrid Mode)
 * * [AI 메모: 제공 기능 요약]
 * 1. Median -> DC 제거 -> FIR -> IIR -> Notch 파이프라인의 엄격한 순서 보장.
 * 2. 핑퐁(Ping-pong) 임시 버퍼를 활용한 ESP-DSP 필터 연산의 메모리 오염 원천 방어.
 * 3. 2채널 마이크의 시간차 및 빔포밍(Beamforming) 압축 지향성 추출.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. 상태 전이(READY -> MONITORING) 시 resetFilterStates() 호출을 절대 누락하지 말 것.
 * ========================================================================== */
#pragma once

#include "T410_Config_004.hpp"
#include "T420_Types_004.hpp"
#include "T415_ConfigMgr_006.hpp" // 동적 설정 매니저 추가
#include "dsps_fft2r.h"
#include "dsps_biquad.h"
#include "dsps_fir.h"

class T430_DspEngine {
private:
    alignas(16) float _notchCoeffs[5];
    alignas(16) float _notch2Coeffs[5]; // 120Hz 계수
    
    alignas(16) float _firLpfCoeffs[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3]; 
    
    alignas(16) float _wNotchL[2]; 
    alignas(16) float _wNotchR[2];
    alignas(16) float _wNotch2L[2];         // 120Hz 상태 딜레이
    alignas(16) float _wNotch2R[2];         // 120Hz 상태 딜레이
    
    alignas(16) float _firStateLpfL[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];
    alignas(16) float _firStateLpfR[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];

    // HPF 추가 (16바이트 정렬 엄수)
    alignas(16) float _firHpfCoeffs[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3]; 
    alignas(16) float _firStateHpfL[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];
    alignas(16) float _firStateHpfR[SmeaConfig::FeatureLimit::FIR_TAPS_CONST + 3];

    fir_f32_t _firInstHpfL;
    fir_f32_t _firInstHpfR;
    fir_f32_t _firInstLpfL;
    fir_f32_t _firInstLpfR; 

    // [In-place 연산 덮어쓰기 오염 방어] 핑퐁 교차 연산을 위한 임시 스크래치 버퍼
    alignas(16) float _workBufA[SmeaConfig::System::FFT_SIZE_CONST];
    alignas(16) float _workBufB[SmeaConfig::System::FFT_SIZE_CONST];

public:
    T430_DspEngine();
    ~T430_DspEngine();

    bool init();
    void resetFilterStates();
    void process(float* p_micL, float* p_micR, float* p_output, uint32_t p_len);

private:
    void _applyMedianFilter(float* p_data, int p_windowSize, uint32_t p_len);
    void _removeDC(float* p_data, uint32_t p_len);
    void _applyNoiseGate(float* p_data, uint32_t p_len, float p_gateThresh); // 임계치 파라미터 추가
    void _applyPreEmphasis(float* p_data, uint32_t p_len, float p_alpha);    // 알파 파라미터 추가
    void _applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len, float p_gain); // 게인 파라미터 추가
    
    void _generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);
    void _generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);
};

