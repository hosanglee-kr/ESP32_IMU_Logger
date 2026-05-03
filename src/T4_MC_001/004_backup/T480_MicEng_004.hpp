/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. 매직넘버 철폐: 모든 하드웨어 및 버퍼 크기는 SmeaConfig 의존.
 * 2. 16-Byte 정렬(SIMD 최적화): esp-dsp 가속기의 128-bit 처리 특성을 극대화하기 위해 
 * 모든 연산 버퍼 및 필터 계수는 반드시 alignas(16)으로 메모리를 정렬한다.
 * 3. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * ============================================================================
 * * File: T480_MicEng_004.hpp
 * Summary: I2S DMA Microphone Acquisition Engine (ICS43434)
 * * [AI 메모: 제공 기능 요약]
 * 1. ESP32-S3 I2S 하드웨어를 제어하여 2채널 42kHz 32bit 데이터를 DMA로 수집.
 * 2. 수집된 교차(Interleaved) 데이터를 L/R 독립 버퍼로 분리(De-interleave).
 * 3. FSM 상태 동기화 전력 제어 (Pause/Resume) 및 DMA 버퍼 클리어 지원.
 *
 * * [AI 메모: 구현 및 유지보수 주의사항]
 * 1. FSM 상태가 READY(대기) 모드일 때 CPU와 I2S 버스가 낭비되는 것을 막기 위해 
 * pause() 메서드를 호출하여 I2S 클럭을 정지하고 마이크를 절전 모드로 전환합니다.
 * 2. resume() 호출 시 반드시 clearBuffer()가 선행되어야 과거의 쓰레기 데이터로 인한
 * 오탐지를 방지할 수 있습니다.
 * ========================================================================== */
#pragma once

#include "T410_Config_004.hpp"
#include <driver/i2s.h>
#include <cstring>

class T480_MicEngine {
private:
    bool _isInitialized = false;
    bool _isPaused = false;
    char _statusText[32];
    
    // DMA에서 읽어올 임시 교차(Interleaved) 버퍼 (SIMD 가속용 16바이트 정렬)
    alignas(16) int32_t _dmaBuffer[SmeaConfig::System::FFT_SIZE_CONST * 2];

public:
    T480_MicEngine();
    ~T480_MicEngine() = default;

    // I2S 하드웨어 및 DMA 초기화
    bool init();

    // FSM 대기 상태 진입 시 호출: I2S DMA 정지 및 마이크 절전
    void pause();

    // FSM 가동 상태 진입 시 호출: I2S DMA 재가동 및 클럭 복귀
    void resume();

    // I2S DMA 버퍼에 쌓인 쓰레기값을 강제로 비움
    void clearBuffer();

    /**
     * @brief DMA로부터 데이터를 읽어 L/R 채널을 분리 후 반환
     * @param p_outL 마이크 L 출력 버퍼
     * @param p_outR 마이크 R 출력 버퍼
     * @param p_reqSamples 요청 샘플 수
     * @return 실제 읽어들인 샘플 수
     */
    uint32_t readData(float* p_outL, float* p_outR, uint32_t p_reqSamples);

    // 현재 마이크 수집 엔진의 상태 텍스트 반환
    const char* getStatusText() const { return _statusText; }
};
