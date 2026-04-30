/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. 매직넘버 철폐: 모든 하드웨어 및 버퍼 크기는 SmeaConfig 의존.
 * 2. 16-Byte 정렬(SIMD 최적화): esp-dsp 가속기의 128-bit 처리 특성을 극대화하기 위해 
 * 모든 연산 버퍼 및 필터 계수는 반드시 alignas(16)으로 메모리를 정렬한다.
 * 3. [구조체 크기 고정 방어]: FeatureSlot 내 배열(ex. band_rms)은 런타임 가변 
 * 설정(_DEF)이 아닌, 반드시 최대 허용 상수(_CONST)를 통해 할당하여 메모리 
 * 단편화와 SIMD 정렬 파괴를 원천 차단한다.
 * 4. [명시적 패딩 방어]: 컴파일러의 암묵적인 패딩(Hidden Padding)으로 인해 
 * SD카드 바이너리 로깅 시 초기화되지 않은 메모리가 유출되는 것을 막기 위해 
 * 구조체의 남는 공간을 _reserved 바이트로 강제 채운다.
 * 5. [네트워크 대역폭 방어]: 6Mbps에 달하는 무거운 데이터(파형/스펙트럼)를 
 * 고속(100Hz)으로 일괄 전송 시 ESP32의 WiFi OOM(Out of Memory)이 발생하므로, 
 * WsHeader 기반의 스트림 타입(Telemetry/Spectrum/Waveform) 멀티플렉싱 규격을 
 * 도입하여 전송 주기를 분리(Decoupling) 제어한다.
 * ============================================================================
 * File: T420_Types_009.hpp
 * Summary: Core Data Structures, Feature Slots & Multiplexed Web Packets
 * ========================================================================== */
#pragma once

#include "T410_Def_008.hpp" 
#include <cstdint>

namespace SmeaType {

    // ========================================================================
    // 1. N-top Peak 정보 구조체 (스펙트럼 상의 핵심 주파수 추출용)
    // ========================================================================
    struct SpectralPeak {
        float frequency;    // 검출된 피크의 주파수 (Hz)
        float amplitude;    // 해당 주파수 대역의 파워 진폭 에너지
    };
    
    // ========================================================================
    // 2. Raw 파형 전용 슬롯 (총 8KB, PSRAM 할당 필수)
    // DMA 버퍼에서 분리되어 슬라이딩 오버랩 처리가 끝난 순수 원본 샘플 보존용
    // ========================================================================
    struct alignas(16) RawDataSlot {
        alignas(16) float raw_L[SmeaConfig::System::FFT_SIZE_CONST]; 
        alignas(16) float raw_R[SmeaConfig::System::FFT_SIZE_CONST]; 
    };

    // ========================================================================
    // 3. 통합 특징량 슬롯 (Zero-copy 메모리 풀 및 SD 고속 로깅용 바이너리 규격)
    // ========================================================================
    struct alignas(16) FeatureSlot {
    
        // [1] AI 모델 추론용 메인 텐서 (39 * 4 = 156 Bytes)
        alignas(16) float mfcc[SmeaConfig::System::MFCC_TOTAL_DIM_CONST];		
        
        // [2] 주파수 도메인 대역별 에너지 (8 * 4 = 32 Bytes)
        float             band_rms[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST]; 
        
        // [3] 기계 결함(RPM) 주파수 역추적용 켑스트럼 특징 (8 * 4 = 32 Bytes)
        float             cpsr_max[SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST];   
        float             cpsr_mxrms[SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST]; 

        // [4] 시간/주파수 기본 통계 지표 (7 * 4 = 28 Bytes)
        float             rms;                  
        float             energy;               
        float             kurtosis;             
        float             crest_factor;         
        float             pooling_stddev_min;   
        float             spectral_centroid;    
        float             sta_lta_ratio; 		

        // [5] 공간 위상 및 시계열 변화율 특징 (4 * 4 = 16 Bytes)
        float             phase_coherence;      
        float             mean_ipd;      		
        float             delta_rms;            
        float             delta_delta_rms;      

        // [6] 핵심 주파수 피크 리스트 (5 * 8 = 40 Bytes)
        SpectralPeak      top_peaks[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];

        // [7] 메타 데이터 (8 + 1 = 9 Bytes)
        uint64_t          timestamp;            
        uint8_t           trial_no;             

        // [8] 명시적 16바이트 정렬 패딩 방어 (7 Bytes)
        // 위 변수들의 합(313 Bytes)을 SIMD 규격 320 Bytes로 강제 정렬
        uint8_t           _reserved[7];
    };

    // [제거됨] RawDataBlock 구조체는 최신 아키텍처에서 사용되지 않으므로 삭제됨.


    // ========================================================================
    // 4. 웹 브로드캐스트 멀티플렉싱 프로토콜 규격 (Packed 속성 부여)
    // 컴파일러 패딩을 무시하고 자바스크립트 DataView/Float32Array와 1:1 매핑됨
    // ========================================================================
    #pragma pack(push, 1)

    // 통신 스트림 패킷 유형 (데이터 성격별 분리)
    enum class StreamType : uint8_t {
        TELEMETRY = 0x01,  // 고속 전송 (100Hz) - 상태, 지표, MFCC
        SPECTRUM  = 0x02,  // 저속 전송 (10~20Hz) - 주파수 스펙트럼 배열
        WAVEFORM  = 0x03   // 저속/온디맨드 전송 - 원본 오디오 샘플
    };

    // 공통 헤더 규격 (총 4 Bytes)
    struct WsHeader {
        uint8_t  magic;    // 패킷 무결성 식별자 (항상 0xA5 유지)
        uint8_t  type;     // StreamType 열거형
        uint16_t length;   // 헤더를 제외한 Payload 바이트 길이
    };

    // [Type 0x01] 초고속 텔레메트리 패킷 (대시보드 렌더링용 핵심 요약본)
    struct PktTelemetry {
        WsHeader header;
        
        // 상태 및 메타 (2 Bytes)
        uint8_t  sys_state;
        uint8_t  trial_no;
        uint8_t  _pad[2]; // 32bit(4 Byte) 정렬을 위한 수동 패딩
        
        // 핵심 단일 지표 (4 * 4 = 16 Bytes)
        float    rms;
        float    sta_lta_ratio;
        float    kurtosis;           // 대시보드 펄스성/타격 노이즈 확인용
        float    spectral_centroid;  // 소리의 날카로움 추세선 확인용
        
        // 멀티 밴드 및 주파수 피크 (UI 막대 차트용)
        float    band_rms[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST]; 
        SpectralPeak top_peaks[3];   // 대역폭 절약을 위해 상위 3개만 전송
        
        // 39차원 히트맵 렌더링용 배열 (156 Bytes)
        float    mfcc[SmeaConfig::System::MFCC_TOTAL_DIM_CONST]; 
    };

    // [Type 0x02] 스펙트럼 패킷 (513 Float = 2052 Bytes)
    struct PktSpectrum {
        WsHeader header;
        // FFT_SIZE/2 + 1 개의 켤레 복소수 파워 성분
        float    bins[(SmeaConfig::System::FFT_SIZE_CONST / 2) + 1];
    };

    // [Type 0x03] 파형 패킷 (1024 Float = 4096 Bytes)
    struct PktWaveform {
        WsHeader header;
        // 네트워크 부담 완화를 위해 빔포밍(L/R 합산)이 완료된 단일 채널 파형만 전송
        float    samples[SmeaConfig::System::FFT_SIZE_CONST]; 
    };

    #pragma pack(pop)

}
