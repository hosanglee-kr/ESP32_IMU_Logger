#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Types_008.h"
#include "T20_Mfcc_Config_008.h"

/*
===============================================================================
소스명: T20_Mfcc_008.h
버전: v008

[기능 스펙]
1. BMI270 SPI + Data Ready Interrupt 기반 고속 샘플 수집
2. FreeRTOS 기반 이중 프레임 버퍼 + Sensor / Process Task 분리
3. 전처리
   - DC 제거
   - Pre-Emphasis
   - Noise Gate
   - Spectral Subtraction
   - LPF / HPF / BPF biquad 필터
4. 특징 추출
   - Hamming Window
   - FFT / Power Spectrum
   - Mel Filterbank
   - Log Mel Energy
   - DCT-II -> MFCC
   - Delta / Delta-Delta
5. 출력 모드
   - Vector Mode : 39차원 단일 특징 벡터
   - Sequence Mode : 최근 N프레임 ring buffer 시퀀스
6. 운영/진단 API
   - printConfig() : 샘플링/필터/출력모드 출력
   - printStatus() : dropped frame, runtime 상태 출력
   - printLatest() : 최신 특징값 출력

[정책]
1. stop()
   - interrupt + task만 중지
   - begin() 상태 유지
   - start() 재호출 가능
2. begin()
   - 재호출 시 기존 runtime 자원 정리 후 재초기화
3. queue full
   - 가장 오래된 프레임을 버리고 최신 프레임 우선 반영
===============================================================================
*/

class CL_T20_Mfcc
{
public:
    struct ST_Impl;

    CL_T20_Mfcc();

    bool begin(const ST_T20_Config_t* p_cfg = nullptr);
    bool start(void);
    void stop(void);

    bool setConfig(const ST_T20_Config_t* p_cfg);
    void getConfig(ST_T20_Config_t* p_cfg_out) const;

    bool getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const;
    bool getLatestVector39(float* p_out_vec, uint16_t p_len) const;

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