#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Def_010.h"

/*
===============================================================================
소스명: T20_Mfcc_009.h
버전: v009

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
   - Vector Mode : MFCC 계수 수에 따라 가변 길이 단일 특징 벡터
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

[설계 의도]
- 공개 API는 상위 애플리케이션이 센서 수집 / 특징 추출 / 최신 특징 조회를
  단순한 객체 인터페이스로 사용할 수 있도록 구성한다.
- 내부 구현(ST_Impl)은 cpp에서 은닉하여 외부 코드가 구현 상세에 직접 의존하지 않도록 한다.
- 설정 구조체(ST_T20_Config_t)를 통해 필터, 출력 모드, MFCC 계수 수, sequence 길이 등을
  런타임에 변경 가능하도록 설계한다.
- 출력 구조체(ST_T20_FeatureVector_t)는 최대 버퍼 + 실제 길이 필드를 함께 가져,
  임베디드 환경에서 동적 할당 없이도 가변 길이 출력을 안전하게 다룰 수 있도록 한다.
===============================================================================
*/

/*
===============================================================================
[클래스 개요]
-------------------------------------------------------------------------------
CL_T20_Mfcc는 T20 진동 특징 추출 모듈의 공개 진입점이다.

외부 사용자는 보통 아래 순서로 사용한다.

1. 객체 생성
2. begin() 호출로 설정 및 하드웨어 초기화
3. start() 호출로 Sensor / Process task 시작
4. 주기적으로 getLatestFeatureVector(), getLatestVector(),
   getLatestSequenceFlat(), printLatest() 등으로 결과 조회
5. 필요 시 setConfig()로 설정 변경
6. 정지 시 stop() 호출

[동시성 관련]
- 내부적으로 FreeRTOS task, queue, mutex를 사용한다.
- 최신 결과 조회 API는 mutex 보호를 통해 thread-safe하게 동작한다.
- setConfig() 호출 시 내부 설정과 일부 runtime 상태가 갱신될 수 있으므로,
  고빈도 실시간 처리 중에는 설정 변경 시점을 신중히 선택하는 것이 좋다.
===============================================================================
*/
class CL_T20_Mfcc
{
public:
    /*
     * 내부 구현체 전방 선언
     * - 실제 멤버 구성은 cpp/internal 헤더에서 정의
     * - 외부에서는 opaque pointer처럼 사용
     */
    struct ST_Impl;

    /*
     * 생성자
     * - 내부 구현 객체를 생성
     * - 실제 하드웨어 초기화는 begin()에서 수행
     */
    CL_T20_Mfcc();

    /*
     * begin()
     * ------------------------------------------------------------------------
     * 모듈 초기화 함수
     *
     * [역할]
     * - 설정 적용
     * - runtime 자원 초기화
     * - SPI 및 GPIO 초기화
     * - DSP 초기화
     * - BMI270 초기화 및 interrupt 설정
     *
     * [입력]
     * - p_cfg != nullptr : 사용자 설정 적용
     * - p_cfg == nullptr : 기본 설정(T20_makeDefaultConfig) 사용
     *
     * [정책]
     * - begin() 재호출 시 기존 runtime 자원을 먼저 정리한 뒤 다시 초기화
     *
     * [반환]
     * - true  : 초기화 성공
     * - false : 초기화 실패
     */
    bool begin(const ST_T20_Config_t* p_cfg = nullptr);

    /*
     * start()
     * ------------------------------------------------------------------------
     * 실시간 처리 시작 함수
     *
     * [역할]
     * - Sensor task 생성
     * - Process task 생성
     * - BMI270 DRDY interrupt 연결
     *
     * [조건]
     * - begin() 성공 후 호출해야 함
     * - 이미 running 상태면 false 반환
     *
     * [반환]
     * - true  : task 시작 성공
     * - false : 시작 실패
     */
    bool start(void);

    /*
     * stop()
     * ------------------------------------------------------------------------
     * 실시간 처리 중지 함수
     *
     * [역할]
     * - interrupt 해제
     * - Sensor / Process task 중지
     *
     * [주의]
     * - begin() 상태 자체를 무효화하지는 않음
     * - 즉, stop() 후 start() 재호출 가능
     */
    void stop(void);

    /*
     * setConfig()
     * ------------------------------------------------------------------------
     * 런타임 설정 변경 함수
     *
     * [역할]
     * - 입력 설정 검증
     * - 내부 설정 구조체 갱신
     * - 필터 재설정
     * - sequence ring buffer 재초기화
     *
     * [반환]
     * - true  : 설정 적용 성공
     * - false : 설정 검증 실패 또는 mutex 획득 실패
     *
     * [주의]
     * - 실시간 처리 중 호출 가능하도록 설계되었지만,
     *   필터/출력 차원 변경 시 직전 결과와 연속성이 달라질 수 있음
     */
    bool setConfig(const ST_T20_Config_t* p_cfg);

    /*
     * getConfig()
     * ------------------------------------------------------------------------
     * 현재 설정 조회 함수
     *
     * [출력]
     * - p_cfg_out에 현재 설정 복사
     *
     * [주의]
     * - p_cfg_out == nullptr 이면 아무 동작도 하지 않음
     */
    void getConfig(ST_T20_Config_t* p_cfg_out) const;

    /*
     * getLatestFeatureVector()
     * ------------------------------------------------------------------------
     * 최신 특징 구조체 전체를 가져오는 함수
     *
     * [출력]
     * - log_mel / mfcc / delta / delta2 / vector와 각 길이 정보 포함
     *
     * [반환]
     * - true  : 최신 결과 복사 성공
     * - false : 아직 결과가 없거나 mutex 획득 실패
     */
    bool getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const;

    /*
     * getLatestVector()
     * ------------------------------------------------------------------------
     * 최신 분류기 입력 벡터(flat vector)만 가져오는 함수
     *
     * [입력]
     * - p_out_vec : 출력 버퍼
     * - p_len     : 출력 버퍼 길이(float 원소 수 기준)
     *
     * [반환]
     * - true  : 최신 vector 복사 성공
     * - false : 결과 미존재 / 버퍼 길이 부족 / mutex 획득 실패
     *
     * [비고]
     * - 길이는 고정 39가 아니라 latest_feature.vector_len 기준의 가변 길이
     */
    bool getLatestVector(float* p_out_vec, uint16_t p_len) const;

    /*
     * isSequenceReady()
     * ------------------------------------------------------------------------
     * sequence 출력이 유효한 상태인지 확인
     *
     * [의미]
     * - ring buffer가 한 바퀴 이상 차서 oldest -> newest 순으로
     *   유효 시퀀스를 export할 수 있는 상태인지 반환
     */
    bool isSequenceReady(void) const;

    /*
     * getLatestSequenceFlat()
     * ------------------------------------------------------------------------
     * 최신 sequence를 1차원 flat 배열로 가져오는 함수
     *
     * [출력 형식]
     * - frame-major flatten
     * - 길이 = sequence_frames * feature_dim
     *
     * [반환]
     * - true  : sequence 복사 성공
     * - false : 아직 sequence 준비 안 됨 / 길이 부족 / mutex 실패
     */
    bool getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const;

    /*
     * getLatestSequenceFrameMajor()
     * ------------------------------------------------------------------------
     * 현재 구현에서는 getLatestSequenceFlat()과 동일 동작
     *
     * [의도]
     * - 향후 frame-major / time-major / 2차원 export 정책 확장 대비
     */
    bool getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const;

    /*
     * printConfig()
     * ------------------------------------------------------------------------
     * 현재 설정 정보를 사람이 읽기 쉬운 형태로 출력
     *
     * [출력 내용]
     * - sample rate
     * - fft size
     * - mel filter 수
     * - mfcc 계수 수
     * - feature dimension
     * - output mode
     * - sequence frame 수
     * - 전처리 옵션 등
     */
    void printConfig(Stream& p_out) const;

    /*
     * printStatus()
     * ------------------------------------------------------------------------
     * 현재 runtime 상태를 사람이 읽기 쉬운 형태로 출력
     *
     * [출력 내용]
     * - initialized / running
     * - dropped frame 수
     * - history count
     * - noise learned frame 수
     * - sequence 상태
     * - valid flag 등
     */
    void printStatus(Stream& p_out) const;

    /*
     * printLatest()
     * ------------------------------------------------------------------------
     * 최신 특징값을 사람이 읽기 쉬운 형태로 출력
     *
     * [출력 내용]
     * - log mel
     * - mfcc
     * - delta
     * - delta-delta
     * - vector length
     *
     * [주의]
     * - 최신 결과가 아직 없으면 안내 메시지 출력
     */
    void printLatest(Stream& p_out) const;

private:
    /*
     * 복사 금지
     * - 내부적으로 task/queue/mutex/하드웨어 상태를 가지므로
     *   복사 생성/대입을 허용하지 않음
     */
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;

    /*
     * 내부 구현 객체 포인터
     * - PImpl 스타일로 구현 상세 은닉
     */
    ST_Impl* _impl;

    /*
     * 내부 ISR / task 함수는 구현 접근을 위해 friend 선언
     * - 외부 API로 공개하려는 목적이 아니라 내부 구현 연결용
     */
    friend void IRAM_ATTR T20_onBmiDrdyISR();
    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
};
