// T20_Mfcc_007.h

#pragma once


#include <Arduino.h>
#include "T20_Mfcc_Types_005.h"
#include "T20_Mfcc_Config_005.h"

/*
===============================================================================
소스명: T20_Mfcc_007.h
버전: v007

[기능 명세 (Specification)]
1. BMI270 SPI + Data Ready Interrupt 기반 고속 샘플 수집
2. FreeRTOS 이중 버퍼 구조
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
   - Vector Mode : 39차원 단일 분류기 입력 벡터
   - Sequence Mode : 최근 N프레임 ring buffer 시퀀스
6. 설정 구조체 기반 동작
   - 출력 모드, 필터, noise 제거, pre-emphasis 등 변경 가능

[튜닝 가이드 (Tuning Guide)]
1. Vector Mode
   - 메모리 사용이 적고 반응이 빠름
   - classical ML 분류기에 적합

2. Sequence Mode
   - 시간 패턴을 반영 가능
   - 1D CNN / TinyML temporal 모델에 적합
   - sequence_frames가 커질수록 메모리와 지연 증가

3. HPF / LPF / BPF
   - HPF: 중력/저주파 드리프트 제거
   - LPF: 고주파 잡음 제거
   - BPF: 특정 진동 대역 분석

4. Noise 제거
   - gate_threshold_abs가 너무 크면 미세 진동 소실
   - spectral_subtract_strength가 너무 크면 왜곡 증가

5. Pre-Emphasis
   - 고주파 성분 강조용
   - 진동 분석 특성상 항상 유리하지는 않으므로 실험 권장
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

    // opaque internal storage는 cpp에서 구현
    
    ST_Impl* _impl;
    
    friend void IRAM_ATTR T20_onBmiDrdyISR();
    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
};



