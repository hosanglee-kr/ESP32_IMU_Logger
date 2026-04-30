/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. [방어] 거대 버퍼 스택 할당 금지: RTOS Stack Overflow 방지를 위해 수 KB에 달하는 
 * 텐서 버퍼는 init() 시 MALLOC_CAP_SPIRAM으로 힙에 할당한다.
 * 2. [방어] SIMD 정렬 위반 차단: ESP-DSP의 행렬 곱셈 수행 전 모든 입출력 텐서의 
 * 16-Byte(alignas) 정렬 및 배수(PADDED) 크기를 컴파일 타임에 보장한다.
 * 3. [방어] 수학적/통계적 결함 교정 (v009 적용 사항): 
 * - Parseval 정리 보정: 스펙트럼 에너지에 단방향 및 윈도우 감쇠(2.666) 손실 복원.
 * - NMS(Non-Maximum Suppression): 스펙트럼 누출 가짜 피크 강제 탈락.
 * - EMA(지수이동평균): 능동 노이즈 학습 시 부동소수점 흡수(Absorption) 원천 차단.
 * - 위상 보존 윈도우: 공간(IPD) 분석 시 누출 차단을 위한 L/R 채널 Hanning Window 적용.
 * - 슬라이딩 STA/LTA: 프레임 내 돌발 충격음 누락 방지를 위한 전 구간 Max 스캔.
 * - DC 바이어스 차단: 무게 중심 및 켑스트럼 탐색 시 0Hz(DC) 성분 명시적 회피.
 * 4. [네이밍 및 자료형 엄수]: private(_), 매개변수(p_), 로컬변수(v_) / <cstdint> 사용
 * ============================================================================
 * File: T440_FeatExtra_009.hpp
 * Summary: 39D MFCC, Cepstrum & Spatial Feature Extraction (Math-Corrected Version)
 * ========================================================================== */
#pragma once

#include "T410_Config_009.hpp"     
#include "T420_Types_011.hpp"      
#include "T415_ConfigMgr_009.hpp"  
#include <cstdint>

class T440_FeatureExtractor {
private:
    // ========================================================================
    // [1] SIMD 메모리 정렬(Alignment) 패딩 상수 (+3 & ~3 비트 연산으로 4의 배수 강제)
    // ========================================================================
    static constexpr uint16_t BINS_PADDED = ((SmeaConfig::System::FFT_SIZE_CONST / 2) + 1 + 3) & ~3;
    static constexpr uint16_t MEL_PADDED  = (SmeaConfig::System::MEL_BANDS_CONST + 3) & ~3;
    static constexpr uint16_t MFCC_PADDED = (SmeaConfig::System::MFCC_COEFFS_CONST + 3) & ~3;

    // ========================================================================
    // [2] 링버퍼 (Ring Buffers) - 시간 미분(Delta) 연산용
    // ========================================================================
    alignas(16) float         _mfccHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST];
    alignas(16) float         _rmsHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST];                           
    alignas(16) float         _deltaHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST]; 
    
    uint8_t                   _historyCount;

    // ========================================================================
    // [3] PSRAM 동적 할당 버퍼 (RTOS Stack Overflow 방어)
    // ========================================================================
    float* _melBankFlat;
    float* _dctMatrixFlat;
    float* _fftSpatialL;
    float* _fftSpatialR;

    // ========================================================================
    // [4] Internal SRAM 워크 버퍼 (극초고속 접근 및 In-place 연산용)
    // ========================================================================
    alignas(16) float         _fftWorkBuf[SmeaConfig::System::FFT_SIZE_CONST * 2];
    alignas(16) float         _powerSpectrum[BINS_PADDED];
    alignas(16) float         _window[SmeaConfig::System::FFT_SIZE_CONST];
    
    // ========================================================================
    // [5] 능동 배경소음 제거 (ANC - 부동소수점 흡수를 막기 위해 EMA 수식 적용)
    // ========================================================================
    alignas(16) float         _noiseProfile[BINS_PADDED]; 
    uint32_t                  _noiseLearnedFrames = 0;      // 최대치를 위해 uint32_t 승격
    bool                      _isNoiseLearning = false;

public:
    T440_FeatureExtractor();
    ~T440_FeatureExtractor();

    bool init();

    // 1채널로 빔포밍 완료된 신호(p_cleanSignal)와 위상 분석용 원본 L/R 신호를 받아 피처 추출
    void extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                 uint32_t p_len, SmeaType::FeatureSlot& p_outSlot);
    
    void setNoiseLearning(bool p_active) { 
        _isNoiseLearning = p_active; 
        if(p_active) _noiseLearnedFrames = 0; 
    }

private:
    // [수학적 결함 교정 적용 완료된 서브 루틴들]
    void _computeBasicFeatures(const float* p_signal, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    void _computePowerSpectrum(const float* p_signal, uint32_t p_len);
    void _computeSpectralCentroid(SmeaType::FeatureSlot& p_slot); 
    void _computeBandRMS(SmeaType::FeatureSlot& p_slot);
    void _computeNtopPeaks(SmeaType::FeatureSlot& p_slot);
    void _computeMfcc39(SmeaType::FeatureSlot& p_slot);
    void _computeCepstrum(SmeaType::FeatureSlot& p_slot);
    void _computeSpatialFeatures(const float* p_L, const float* p_R, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    void _applyTemporalDerivatives(SmeaType::FeatureSlot& p_slot);
};

