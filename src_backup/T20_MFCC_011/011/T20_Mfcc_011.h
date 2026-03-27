#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Def_011.h"

/*
===============================================================================
소스명: T20_Mfcc_011.h
버전: v011

[기능 스펙]
1. BMI270 SPI + Data Ready Interrupt 기반 고속 샘플 수집
2. FreeRTOS 기반 Sensor / Process Task 분리
3. Sample Ring Buffer 기반 프레임 조립
4. Sliding Window / Hop Size 지원
   - hop == frame_size : overlap 없음
   - hop < frame_size  : overlap 적용
5. 전처리 Stage 배열 기반 파이프라인
   - DC Remove
   - Pre-emphasis
   - Noise Gate
   - Biquad LPF / HPF / BPF
   - 향후 stage 확장 대비
6. 특징 추출
   - Hamming Window
   - FFT / Power Spectrum
   - Mel Filterbank
   - Log Mel
   - DCT-II -> MFCC
   - Delta / Delta-Delta
7. 출력 모드
   - Vector Mode
   - Sequence Mode
8. 운영/진단 API
   - printConfig()
   - printStatus()
   - printLatest()

[Bundle-A 구현 범위]
- 설정 구조체 확정본
- 내부 구조체 확정본
- 전체 골격 코드 정리
- Sliding Window / Stage Pipeline 구조 반영

[향후 단계 구현 예정 사항]
- 버튼 기반 측정 시작/종료/마커
- 측정 세션 상태기계
- SD_MMC 저장 계층
- AsyncWeb 설정/제어/시각화
- multi-config FFT / mel-bank 캐시 풀
- zero-copy / DMA / cache-aware 세부 최적화
===============================================================================
*/

class CL_T20_Mfcc
{
public:
    struct ST_Impl;

    CL_T20_Mfcc();
    ~CL_T20_Mfcc();

    bool begin(const ST_T20_Config_t* p_cfg = nullptr);
    bool start(void);
    void stop(void);

    bool setConfig(const ST_T20_Config_t* p_cfg);
    void getConfig(ST_T20_Config_t* p_cfg_out) const;

    bool getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const;
    bool getLatestVector(float* p_out_vec, uint16_t p_len) const;

    bool isSequenceReady(void) const;
    bool getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const;
    bool getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const;

    void printConfig(Stream& p_out) const;
    void printStatus(Stream& p_out) const;
    void printLatest(Stream& p_out) const;

private:
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;

    ST_Impl* _impl;

    friend void IRAM_ATTR T20_onBmiDrdyISR();
    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
};
