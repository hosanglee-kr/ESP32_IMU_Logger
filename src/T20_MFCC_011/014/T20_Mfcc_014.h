#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Def_014.h"

/*
===============================================================================
소스명: T20_Mfcc_014.h
버전: v014

[기능 스펙]
1. BMI270 SPI + Data Ready Interrupt 기반 고속 샘플 수집
2. FreeRTOS 기반 Sensor / Process Task 분리
3. sample ring buffer + frame_size / hop_size 기반 sliding window
4. 전처리 stage 배열 기반 파이프라인
5. MFCC / Delta / Delta-Delta 특징 추출
6. Vector / Sequence 출력 모드
7. 세션 상태기계
8. 버튼 기반 측정 시작/종료 토글 지원
9. SD_MMC recorder 대묶음 C 골격 통합

[향후 단계 TODO]
- recorder 실제 비동기 쓰기 큐 고도화
- AsyncWeb 설정 변경 / 측정 시작·종료 / 시각화
- live waveform / live feature API
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

    bool measurementStart(void);
    bool measurementStop(void);
    EM_T20_SessionState_t getSessionState(void) const;

    void pollButton(void);

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
