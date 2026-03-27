#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Def_021.h"

/*
===============================================================================
소스명: T20_Mfcc_021.h
버전: v021

[기능 스펙]
1. BMI270 SPI + Data Ready Interrupt 기반 고속 샘플 수집
2. FreeRTOS 기반 Sensor / Process Task 분리
3. sample ring buffer + frame_size / hop_size 기반 sliding window
4. 전처리 stage 배열 기반 파이프라인
5. MFCC / Delta / Delta-Delta 특징 추출
6. Vector / Sequence 출력 모드
7. 세션 상태기계
8. 버튼 기반 측정 시작/종료 토글 지원
9. SD_MMC recorder batching/flush 최적화
10. AsyncWeb 제어/다운로드 대묶음 통합

[향후 단계 TODO]
- multi-config FFT / mel cache
- zero-copy / DMA / cache aligned 고도화
- websocket 인증 / 권한 제어
- live decimation / chart aggregation
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

    /*
     * AsyncWeb 서버 등록
     * ------------------------------------------------------------------------
     * [입력]
     * - p_server    : AsyncWebServer 인스턴스 포인터를 void* 형태로 전달
     * - p_base_path : API base path, nullptr이면 기본값 사용
     *
     * [설명]
     * - ESPAsyncWebServer 헤더를 공개 헤더에서 직접 노출하지 않기 위해
     *   void* 인터페이스를 사용한다.
     * - 내부적으로 status/config/latest/start/stop/download endpoint를 등록한다.
     */
    bool attachWebServer(void* p_server, const char* p_base_path = nullptr);
    bool attachWebStaticFiles(void* p_server);

    /*
     * AsyncWeb endpoint 해제
     * ------------------------------------------------------------------------
     * - 현재 단계에서는 등록 상태만 해제한다.
     * - 개별 route remove는 라이브러리 제약으로 TODO 항목으로 남긴다.
     */
    void detachWebServer(void);


    bool getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const;
    bool getLatestVector(float* p_out_vec, uint16_t p_len) const;

    bool isSequenceReady(void) const;
    bool getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const;
    bool getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const;

    /*
     * 최신 time-domain frame 조회
     * ------------------------------------------------------------------------
     * [설명]
     * - 가장 최근 처리된 frame의 time-domain 샘플을 복사한다.
     * - live waveform 시각화용 API
     */
    bool getLatestWaveFrame(float* p_out_frame, uint16_t p_len, uint16_t* p_out_valid_len = nullptr) const;

    /*
     * 최신 sequence 상태 조회
     * ------------------------------------------------------------------------
     * [설명]
     * - 현재 sequence ring buffer의 frame 수와 feature dimension을 조회한다.
     */
    bool getSequenceShape(uint16_t* p_out_frames, uint16_t* p_out_feature_dim) const;


    /*
     * JSON 문자열 기반 설정 적용
     * ------------------------------------------------------------------------
     * [설명]
     * - AsyncWeb POST /config 에서 전달된 JSON 문자열을 파싱/검증하여 적용한다.
     * - 현재 단계에서는 필수 항목만 부분 파싱하는 안정성 우선 방식으로 구현한다.
     * - 측정 중에는 false를 반환하여 보수적으로 동작한다.
     */
    bool applyConfigJson(const char* p_json);

    void printConfig(Stream& p_out) const;
    void printStatus(Stream& p_out) const;
    void printLatest(Stream& p_out) const;

    /*
     * webPushNow()
     * ------------------------------------------------------------------------
     * [설명]
     * - 현재 최신 상태를 websocket/SSE 등 실시간 채널로 즉시 push 시도한다.
     * - web 계층이 연결되지 않은 경우 false 반환 가능
     */
    bool webPushNow(void);


private:
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;

    ST_Impl* _impl;

    friend void IRAM_ATTR T20_onBmiDrdyISR();
    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
};
