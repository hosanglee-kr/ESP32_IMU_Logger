/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. (실수) 거대한 버퍼를 지역 변수로 선언하여 RTOS Task Stack Overflow 패닉 유발.
 * -> (방어) FFT_SIZE 단위가 넘어가는 배열은 반드시 클래스 멤버 포인터로 선언하고 
 * init() 시점에 MALLOC_CAP_SPIRAM으로 힙에 할당한다.
 * 2. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 3. [동적/정적 상수 승격]: MFCC 상수, 윈도우 사이즈, 켑스트럼 타겟 주파수 등 
 * 추론에 영향을 미치는 모든 매직 넘버를 제거하고 T410/T415 중앙 설정으로 위임한다.
 * ============================================================================
 * File: T440_FeatExtra_007.hpp
 * Summary: 39D MFCC, Cepstrum & Spatial Feature Extraction
 * ========================================================================== */
#pragma once

#include "T410_Def_009.hpp"
#include "T420_Types_008.hpp"
#include "T415_ConfigMgr_009.hpp" // 동적 설정 매니저 추가

class T440_FeatureExtractor {
private:
    static constexpr uint16_t BINS_PADDED = ((SmeaConfig::System::FFT_SIZE_CONST / 2) + 1 + 3) & ~3;
    static constexpr uint16_t MEL_PADDED  = (SmeaConfig::System::MEL_BANDS_CONST + 3) & ~3;
    static constexpr uint16_t MFCC_PADDED = (SmeaConfig::System::MFCC_COEFFS_CONST + 3) & ~3;

    alignas(16) float         _mfccHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST];
    alignas(16) float         _rmsHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST];                           
    uint8_t                   _historyCount;

    // PSRAM 동적 할당 버퍼 (스택 오버플로우 방어)
    float* _melBankFlat;
    float* _dctMatrixFlat;
    float* _fftSpatialL;
    float* _fftSpatialR;
    
    alignas(16) float         _deltaHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST]; // N=2 연산용

    // Internal SRAM 할당 (고속 접근용)
    alignas(16) float         _fftWorkBuf[SmeaConfig::System::FFT_SIZE_CONST * 2];
    alignas(16) float         _powerSpectrum[BINS_PADDED];
    alignas(16) float         _window[SmeaConfig::System::FFT_SIZE_CONST];
    
    alignas(16) float         _noiseProfile[BINS_PADDED]; 
    uint16_t                  _noiseLearnedFrames = 0;
    bool                      _isNoiseLearning = false;

public:
    T440_FeatureExtractor();
    ~T440_FeatureExtractor();

    bool init();

    void extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                 uint32_t p_len, SmeaType::FeatureSlot& p_outSlot);
    
    void setNoiseLearning(bool p_active) { 
        _isNoiseLearning = p_active; 
        if(p_active) _noiseLearnedFrames = 0; // 켤 때 프레임 카운트 리셋
    }

private:
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

