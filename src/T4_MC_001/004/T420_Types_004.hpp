/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. 매직넘버 철폐: 모든 하드웨어 및 버퍼 크기는 SmeaConfig 의존.
 * 2. 16-Byte 정렬(SIMD 최적화): esp-dsp 가속기의 128-bit 처리 특성을 극대화하기 위해 
 * 모든 연산 버퍼 및 필터 계수는 반드시 alignas(16)으로 메모리를 정렬한다.
 * 3. [구조체 크기 고정 방어]: FeatureSlot 내 배열(ex. band_rms)은 런타임 가변 
 * 설정(_DEF)이 아닌, 반드시 최대 허용 상수(_CONST)를 통해 할당하여 메모리 
 * 단편화와 SIMD 정렬 파괴를 원천 차단한다.
 * ============================================================================
 * File: T420_Types_004.hpp
 * Summary: Core Data Structures & Feature Slots
 * * [AI 메모: 제공 기능 요약]
 * 1. 39차원 MFCC, Cepstrum, 위상 피처를 포함한 통합 특징량 구조체 정의.
 * 2. 슬라이딩 오버랩 처리를 완료한 1024 샘플 분량의 Raw 파형(L/R) 보존 영역 추가.
 * 3. 16바이트 정렬을 강제하여 ESP-DSP(SIMD) 연산 정합성 완벽 보장.
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. FeatureSlot 내부에 Raw 데이터가 포함되면서 구조체 1개당 약 8.5KB의 
 * 메모리를 차지합니다. 따라서 인스턴스는 반드시 PSRAM 메모리 풀에 할당해야 합니다.
 * ========================================================================== */
#pragma once

#include "T410_Config_004.hpp" 
#include <cstdint>

namespace SmeaType {

    // N-top Peak 정보 구조체
    struct SpectralPeak {
        float frequency;
        float amplitude;
    };
    
    // Raw 파형 전용 슬롯 (16바이트 정렬, 총 8KB)
    // [패치] System::FFT_SIZE_CONST 네임스페이스 및 접미사 현행화
    struct alignas(16) RawDataSlot {
        alignas(16) float raw_L[SmeaConfig::System::FFT_SIZE_CONST];
        alignas(16) float raw_R[SmeaConfig::System::FFT_SIZE_CONST];
    };


    // 통합 특징량 슬롯 (Zero-copy 메모리 풀 및 SD 고속 로깅용)
    struct alignas(16) FeatureSlot {
    
        // [MFCC 39D 텐서 배열] - System::MFCC_TOTAL_DIM_CONST 적용
        alignas(16) float mfcc[SmeaConfig::System::MFCC_TOTAL_DIM_CONST];		
        
        // [패치] 런타임 가변이 아닌 최대 크기(MAX) 정적 상수를 적용하여 16바이트 정렬 보존
        float             band_rms[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST]; 
        
        // [Cepstrum 타겟 특징]
        float             cpsr_max[4];          // N1 ~ N4
        float             cpsr_mxrms[4];        // N1 ~ N4

        // [시간 및 주파수 도메인 기본 특징]
        float             rms;
        float             energy;
        float             kurtosis;
        float             crest_factor;
        float             pooling_stddev_min;
        float             spectral_centroid;
        float             sta_lta_ratio; 		// 임펄스 충격음 진단용

        
        // [공간(위상) 및 동적 특징]
        float             phase_coherence;
        float             mean_ipd;      		// Inter-channel Phase Difference (마이크 간 위상차)
        float             delta_rms;
        float             delta_delta_rms;

        // [N-top Peaks]
        SpectralPeak      top_peaks[5];

        // [메타 데이터]
        uint64_t          timestamp;
        uint8_t           trial_no;
    };

    // I2S 데이터 수집용 버퍼 기술자 (필요 시 콜백 등에서 포인터 전달용)
    struct RawDataBlock {
        float* p_buffer_L;
        float* p_buffer_R;
        uint32_t         length;
    };
}
