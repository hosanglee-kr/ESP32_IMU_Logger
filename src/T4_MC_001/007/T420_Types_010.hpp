/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블]
 * 1. 매직넘버 철폐: 모든 하드웨어 및 버퍼 크기는 SmeaConfig 의존.
 * 2. 16-Byte 정렬(SIMD 최적화): esp-dsp 가속기의 128-bit 처리 특성을 극대화하기 위해
 * 모든 연산 버퍼 및 필터 계수는 반드시 alignas(16)으로 메모리를 정렬한다.
 * 3. [명시적 패딩 방어]: 컴파일러 암묵적 패딩으로 인한 SD카드 바이너리 로깅
 * 정합성 파괴를 막기 위해 구조체 끝에 _reserved 바이트를 강제 할당한다.
 * 4. [네트워크 대역폭 방어]: 6Mbps에 달하는 무거운 데이터를 고속(100Hz) 일괄
 * 전송 시 발생하는 OOM을 막기 위해 WsHeader 기반 다중화(Multiplexing) 규격을
 * 도입하고, packed 속성으로 브라우저의 Float32Array 파싱 완벽 일치를 보장한다.
 * ============================================================================
 * File: T420_Types_010.hpp
 * Summary: Core Data Structures, Feature Slots & Multiplexed Web Packets
 * ========================================================================== */
#pragma once

#include "T410_Def_009.hpp"
#include <cstdint>

namespace SmeaType {

    // N-top Peak 정보 구조체 (스펙트럼 상의 핵심 주파수 추출용)
	// TODO : SpectralPeak --> FreqComp (FrequencyComponent)로 전체 명칭 변경
    struct SpectralPeak {
        float frequency;
        float amplitude;
    };

    // Raw 파형 전용 슬롯 (총 8KB, PSRAM 할당 필수)
    struct alignas(16) RawDataSlot {
        alignas(16) float raw_L[SmeaConfig::System::FFT_SIZE_CONST];
        alignas(16) float raw_R[SmeaConfig::System::FFT_SIZE_CONST];
    };

    // 통합 특징량 슬롯 (Zero-copy 메모리 풀 및 SD 고속 로깅용 바이너리 규격)
    struct alignas(16) FeatureSlot {
        alignas(16) float mfcc[SmeaConfig::System::MFCC_TOTAL_DIM_CONST];

        float             band_rms[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST];
        float             cpsr_max[SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST];
        float             cpsr_mxrms[SmeaConfig::FeatureLimit::CEPS_TARGET_COUNT_CONST];

        float             rms;
        float             energy;
        float             kurtosis;
        float             crest_factor;
        float             pooling_stddev_min;
        float             spectral_centroid;
        float             sta_lta_ratio;

        float             phase_coherence;
        float             mean_ipd;
        float             delta_rms;
        float             delta_delta_rms;

		SpectralPeak      top_peaks[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];

        uint64_t          timestamp;
        uint8_t           trial_no;

        // 명시적 16바이트 정렬 패딩 방어 (7 Bytes)
        uint8_t           _reserved[7];
    };

    // ========================================================================
    // 웹 브로드캐스트 멀티플렉싱 프로토콜 규격 (Packed 속성 부여)
    // ========================================================================
    #pragma pack(push, 1)

    enum class StreamType : uint8_t {
        TELEMETRY = 0x01,
        SPECTRUM  = 0x02,
        WAVEFORM  = 0x03
    };

    struct WsHeader {
        uint8_t  magic;    // 항상 0xA5 유지
        uint8_t  type;     // StreamType 열거형
        uint16_t length;   // Payload 바이트 길이
    };

    // [Type 0x01] 초고속 텔레메트리 패킷 (대시보드 렌더링용)
    struct PktTelemetry {
        WsHeader header;

        uint8_t  sys_state;
        uint8_t  trial_no;
        uint8_t  _pad[2]; // 32bit 정렬 수동 패딩

        float    rms;
        float    sta_lta_ratio;
        float    kurtosis;
        float    spectral_centroid;

        float    band_rms[SmeaConfig::FeatureLimit::MAX_BAND_RMS_COUNT_CONST];

		// [방어/교정] 브라우저 JS 파싱(Float32Array) 정합성을 위해 구조체 배열 대신 Float 배열 2개로 분리(평탄화)
		float    peak_freqs[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];
		float    peak_amps[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];
        // 주파수와 진폭 모두 포함된 SpectralPeak 구조체 5개 배열 할당
        // SpectralPeak top_peaks[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];

        float    mfcc[SmeaConfig::System::MFCC_TOTAL_DIM_CONST];
    };

    // [Type 0x02] 스펙트럼 패킷 (저속 전송용)
    struct PktSpectrum {
        WsHeader header;
        float    bins[(SmeaConfig::System::FFT_SIZE_CONST / 2) + 1];
    };

    // [Type 0x03] 파형 패킷 (저속 전송용)
    struct PktWaveform {
        WsHeader header;
        float    samples[SmeaConfig::System::FFT_SIZE_CONST];
    };

    #pragma pack(pop)
}

