/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. (실수) 거대한 버퍼를 지역 변수로 선언하여 RTOS Task Stack Overflow 패닉 유발.
 * -> (방어) FFT_SIZE 단위가 넘어가는 배열은 반드시 클래스 멤버 포인터로 선언하고 
 * init() 시점에 MALLOC_CAP_SPIRAM으로 힙에 할당한다.
 * ============================================================================
 * File: T440_FeatExtra_001.hpp
 * Summary: 39D MFCC, Cepstrum & Spatial Feature Extraction
 * ========================================================================== */
#pragma once

#include "T410_Config_001.hpp"
#include "T420_Types_001.hpp"

class T440_FeatureExtractor {
private:
    static constexpr uint16_t BINS_PADDED = ((SmeaConfig::FFT_SIZE / 2) + 1 + 3) & ~3;
    static constexpr uint16_t MEL_PADDED  = (SmeaConfig::MEL_BANDS + 3) & ~3;
    static constexpr uint16_t MFCC_PADDED = (SmeaConfig::MFCC_COEFFS + 3) & ~3;

    alignas(16) float v_mfccHistory[5][SmeaConfig::MFCC_COEFFS];
    alignas(16) float v_rmsHistory[5]; // Delta RMS 연산용 히스토리 링버퍼 추가
    uint8_t v_historyCount;

    // PSRAM 동적 할당 버퍼 (스택 오버플로우 방어)
    float* v_melBankFlat;
    float* v_dctMatrixFlat;
    float* v_fftSpatialL;
    float* v_fftSpatialR;
    
    // Internal SRAM 할당 (고속 접근용)
    alignas(16) float v_fftWorkBuf[SmeaConfig::FFT_SIZE * 2];
    alignas(16) float v_powerSpectrum[BINS_PADDED];
    alignas(16) float v_window[SmeaConfig::FFT_SIZE];
    
    alignas(16) float v_noiseProfile[BINS_PADDED]; 
    uint16_t v_noiseLearnedFrames = 0;
    bool v_isNoiseLearning = false;

public:
    T440_FeatureExtractor();
    ~T440_FeatureExtractor();

    bool init();

    void extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                 uint32_t p_len, SmeaType::FeatureSlot& p_outSlot);
    
    void setNoiseLearning(bool p_active) { 
        v_isNoiseLearning = p_active; 
        if(p_active) v_noiseLearnedFrames = 0; // 켤 때 프레임 카운트 리셋
    }

private:
    void computeBasicFeatures(const float* p_signal, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    void computePowerSpectrum(const float* p_signal, uint32_t p_len);
    void computeSpectralCentroid(SmeaType::FeatureSlot& p_slot); 
    
    void computeNtopPeaks(SmeaType::FeatureSlot& p_slot);
    void computeMfcc39(SmeaType::FeatureSlot& p_slot);
    void computeCepstrum(SmeaType::FeatureSlot& p_slot);
    void computeSpatialFeatures(const float* p_L, const float* p_R, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    void applyTemporalDerivatives(SmeaType::FeatureSlot& p_slot);
};



