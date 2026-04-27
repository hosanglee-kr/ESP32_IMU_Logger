/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. (실수) 필터 제자리(In-place) 연산 오염 
 * -> (방어) FIR/IIR 필터는 반드시 입출력 버퍼를 분리(핑퐁)하거나, 시계열이 
 * 유지되도록 역순 루프(Pre-emphasis)를 적용한다.
 * 2. (실수) C++ -ffast-math 최적화로 인한 예외처리 무력화 
 * -> (방어) 부동소수점 0 나누기 방어는 fabsf(val) < 1e-6f 검사를 쓰고, 
 * NaN/Inf 유입은 IEEE-754 비트 마스킹으로 하드웨어 컷을 수행한다.
 * ============================================================================
 * File: T430_DspEng_003.hpp
 * Summary: SIMD Optimized Signal Processing Pipeline (Hybrid Mode)
 * * [AI 메모: 제공 기능 요약]
 * 1. Median -> DC 제거 -> FIR -> IIR -> Notch 파이프라인의 엄격한 순서 보장.
 * 2. 핑퐁(Ping-pong) 임시 버퍼를 활용한 ESP-DSP 필터 연산의 메모리 오염 원천 방어.
 * 3. 2채널 마이크의 시간차 및 빔포밍(Beamforming) 압축 지향성 추출.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. 상태 전이(READY -> MONITORING) 시 resetFilterStates() 호출을 절대 누락하지 말 것.
 * ========================================================================== */
#pragma once

#include "T410_Config_003.hpp"
#include "T420_Types_003.hpp"
#include "dsps_fft2r.h"
#include "dsps_biquad.h"
#include "dsps_fir.h"

class T430_DspEngine {
private:
    // [SIMD 가속 방어] 16바이트 정렬 계수 및 상태(Delay line) 버퍼
    // FIR 계수는 Taps + 패딩(3)을 통해 128비트 단위 초과 읽기(Over-read) 패닉을 방지함
    alignas(16) float v_notchCoeffs[5];
	alignas(16) float v_notch2Coeffs[5]; // 120Hz 계수
	
    alignas(16) float v_firLpfCoeffs[SmeaConfig::Dsp::FIR_TAPS + 3]; 
    
    
    alignas(16) float v_wNotchL[2]; 
    alignas(16) float v_wNotchR[2];
	alignas(16) float v_wNotch2L[2]; 		// 120Hz 상태 딜레이
    alignas(16) float v_wNotch2R[2]; 		// 120Hz 상태 딜레이
	
    alignas(16) float v_firStateLpfL[SmeaConfig::Dsp::FIR_TAPS + 3];
    alignas(16) float v_firStateLpfR[SmeaConfig::Dsp::FIR_TAPS + 3];

    	// HPF 추가 (16바이트 정렬 엄수)
	alignas(16) float v_firHpfCoeffs[SmeaConfig::Dsp::FIR_TAPS + 3]; 
	alignas(16) float v_firStateHpfL[SmeaConfig::Dsp::FIR_TAPS + 3];
	alignas(16) float v_firStateHpfR[SmeaConfig::Dsp::FIR_TAPS + 3];

    fir_f32_t v_firInstHpfL;
    fir_f32_t v_firInstHpfR;
    fir_f32_t v_firInstLpfL;
    fir_f32_t v_firInstLpfR; 

    // [In-place 연산 덮어쓰기 오염 방어] 핑퐁 교차 연산을 위한 임시 스크래치 버퍼
    // IRAM(Internal SRAM) 병목을 막기 위해 반드시 MALLOC_CAP_INTERNAL 배치 권장
    alignas(16) float v_workBufA[SmeaConfig::FFT_SIZE];
    alignas(16) float v_workBufB[SmeaConfig::FFT_SIZE];

public:
    T430_DspEngine();
    ~T430_DspEngine();

    bool init();
    
    void resetFilterStates();
    
    /**
     * @brief T20 룰을 반영한 엄격한 파이프라인 순서의 신호 정제
     * @param p_micL 마이크 L 원본 버퍼
     * @param p_micR 마이크 R 원본 버퍼
     * @param p_output 최종 빔포밍 및 정제 완료된 출력 버퍼
     * @param p_len 처리할 샘플 길이 (보통 SmeaConfig::FFT_SIZE)
     */
    void process(float* p_micL, float* p_micR, float* p_output, uint32_t p_len);

private:
    // [서브루틴] DSP 수학적 파이프라인 컴포넌트
    void applyMedianFilter(float* p_data, int p_windowSize, uint32_t p_len);
    void removeDC(float* p_data, uint32_t p_len);
    void applyNoiseGate(float* p_data, uint32_t p_len);
    void applyPreEmphasis(float* p_data, uint32_t p_len);
    void applyBeamforming(const float* p_L, const float* p_R, float* p_out, uint32_t p_len);
    
    // Windowed-Sinc 기반 초정밀 Linear Phase FIR 계수 생성기
    void generateFirLpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);
    
    // HPF 생성 함수 
	void generateFirHpfWindowedSinc(float* p_coeffs, uint16_t p_taps, float p_cutoffHz);
	
};

