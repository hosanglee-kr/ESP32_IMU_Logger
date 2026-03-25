#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "T20_Mfcc_009.h"

/*
===============================================================================
소스명: T20_Mfcc_Inter_009.h
버전: v009

[기능 스펙]
- 외부 공개하지 않을 내부 구현 구조체 정의
- Core / DSP 분리 컴파일을 위한 공용 내부 선언 제공

[설계 목적]
1. 공개 헤더(T20_Mfcc_009.h)에는 최소 API만 노출
2. 실제 구현에 필요한 내부 상태는 이 헤더에 모아 관리
3. Core 계층과 DSP 계층이 같은 내부 타입/함수를 공유할 수 있게 구성
4. 상위 사용자 코드가 내부 멤버에 직접 의존하지 않도록 구현 세부를 은닉

[계층 구분]
- Core
  : 생명주기(begin/start/stop), task, queue, 상태 관리, 출력 관리
- DSP
  : 필터, FFT, Mel filterbank, MFCC, delta/delta-delta 계산
===============================================================================
*/

/*
===============================================================================
[프레임 메시지]
-------------------------------------------------------------------------------
Sensor task가 raw frame 하나를 모두 채운 뒤 Process task에 전달할 때 사용하는
queue 메시지 구조체이다.

frame_index
- 완료된 raw frame buffer의 index
- Process task는 이 index를 보고 해당 버퍼를 읽어 처리한다
===============================================================================
*/
typedef struct
{
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

/*
===============================================================================
[내부 구현체]
-------------------------------------------------------------------------------
CL_T20_Mfcc의 실제 내부 상태를 보관하는 구현체이다.
외부에서는 ST_Impl의 정의를 볼 필요가 없으며, cpp 계층에서만 직접 접근한다.

[구성 요소]
1. 하드웨어 객체
   - BMI270 sensor object
   - SPI object

2. RTOS 객체
   - Sensor / Process task handle
   - frame queue
   - mutex

3. 설정 / 상태
   - 현재 설정(cfg)
   - initialized / running 상태
   - active buffer index / sample index / drop count

4. DSP 버퍼
   - raw frame
   - work/temp frame
   - window / fft / power / mel / noise spectrum

5. 특징값 저장
   - mfcc history
   - latest feature vector
   - latest sequence flat buffer
   - sequence ring buffer

6. 필터 상태
   - biquad coeffs
   - biquad state

7. 기타 runtime 값
   - pre-emphasis 이전 샘플
   - noise learning frame 수
===============================================================================
*/
struct CL_T20_Mfcc::ST_Impl
{
    /*
     * BMI270 센서 객체
     * - SPI 기반 센서 초기화 및 데이터 읽기에 사용
     */
    BMI270   imu;

    /*
     * SPI 객체
     * - ESP32-S3 FSPI 사용
     */
    SPIClass spi;

    /*
     * Sensor task handle
     * - DRDY interrupt를 받아 raw sample을 수집하는 task
     */
    TaskHandle_t sensor_task_handle;

    /*
     * Process task handle
     * - frame 단위 DSP 처리 및 특징 추출을 수행하는 task
     */
    TaskHandle_t process_task_handle;

    /*
     * frame queue
     * - Sensor task -> Process task 프레임 전달용
     */
    QueueHandle_t frame_queue;

    /*
     * mutex
     * - 최신 특징값, 설정값, 상태값 보호용
     */
    SemaphoreHandle_t mutex;

    /*
     * initialized
     * - begin()이 성공적으로 완료되었는지 여부
     */
    bool initialized;

    /*
     * running
     * - task 및 interrupt 기반 실시간 동작이 현재 활성화 상태인지 여부
     */
    bool running;

    /*
     * 현재 모듈 설정
     * - begin()/setConfig()를 통해 갱신됨
     */
    ST_T20_Config_t cfg;

    /*
     * raw frame double/multi buffer
     * - Sensor task가 샘플을 채우는 버퍼
     * - 버퍼 수는 G_T20_RAW_FRAME_BUFFERS로 정의
     * - Process task는 완료된 frame만 queue를 통해 전달받아 읽음
     */
    float __attribute__((aligned(16))) frame_buffer[G_T20_RAW_FRAME_BUFFERS][G_T20_FFT_SIZE];

    /*
     * active_fill_buffer
     * - 현재 Sensor task가 채우고 있는 raw frame buffer index
     */
    volatile uint8_t active_fill_buffer;

    /*
     * active_sample_index
     * - 현재 frame buffer 내에 몇 개의 샘플이 쌓였는지 나타냄
     */
    volatile uint16_t active_sample_index;

    /*
     * dropped_frames
     * - queue full 등으로 인해 버려진 프레임 누적 카운트
     * - "오래된 프레임을 버린 경우"도 drop으로 포함 가능
     */
    volatile uint32_t dropped_frames;

    /*
     * work_frame
     * - 전처리 및 window/fft 입력용 작업 버퍼
     */
    float __attribute__((aligned(16))) work_frame[G_T20_FFT_SIZE];

    /*
     * temp_frame
     * - DC 제거 / pre-emphasis / gate 전처리 중간 버퍼
     */
    float __attribute__((aligned(16))) temp_frame[G_T20_FFT_SIZE];

    /*
     * window
     * - Hamming window 계수 저장
     */
    float __attribute__((aligned(16))) window[G_T20_FFT_SIZE];

    /*
     * fft_buffer
     * - complex FFT 입력/출력 버퍼
     * - real/imag interleaved 구조
     */
    float __attribute__((aligned(16))) fft_buffer[G_T20_FFT_SIZE * 2];

    /*
     * power
     * - power spectrum 저장 버퍼
     */
    float __attribute__((aligned(16))) power[(G_T20_FFT_SIZE / 2) + 1];

    /*
     * noise_spectrum
     * - spectral subtraction용 noise profile
     */
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];

    /*
     * log_mel
     * - latest frame에 대한 log mel energy
     */
    float __attribute__((aligned(16))) log_mel[G_T20_MEL_FILTERS];

    /*
     * mel_bank
     * - mel filterbank 계수 테이블
     * - [mel filter index][fft bin]
     */
    float __attribute__((aligned(16))) mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

    /*
     * mfcc_history
     * - delta / delta-delta 계산용 최근 MFCC 이력
     * - 최대 MFCC 계수 수 기준으로 버퍼를 확보
     */
    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS_MAX];

    /*
     * mfcc_history_count
     * - 현재 유효하게 쌓인 history frame 수
     */
    uint16_t mfcc_history_count;

    /*
     * latest_feature
     * - 최신 frame 처리 결과 전체
     * - log_mel / mfcc / delta / delta2 / vector 포함
     */
    ST_T20_FeatureVector_t latest_feature;

    /*
     * seq_rb
     * - 최근 N프레임 feature vector 저장용 ring buffer
     */
    ST_T20_FeatureRingBuffer_t seq_rb;

    /*
     * latest_sequence_flat
     * - seq_rb 내용을 1차원 flat 배열로 export한 최신 결과 저장 버퍼
     */
    float latest_sequence_flat[G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX];

    /*
     * latest_vector_valid
     * - 최신 단일 feature vector가 유효한지 여부
     */
    bool latest_vector_valid;

    /*
     * latest_sequence_valid
     * - 최신 sequence output이 유효한지 여부
     */
    bool latest_sequence_valid;

    /*
     * biquad_coeffs
     * - 현재 필터 계수 저장용
     * - begin()/setConfig() 또는 runtime filter 구성 시 사용
     */
    float biquad_coeffs[5];
    

    /*
     * biquad_state
     * - biquad filter state
     * - 필터 연속 처리 시 내부 상태 보존용
     */
    float biquad_state[2]; // 사용 안 하거나 legacy 용도
    
    float process_biquad_state[2];   // 실제 frame processing 전용 상태


    /*
     * prev_raw_sample
     * - pre-emphasis 계산 시 이전 샘플 저장
     */
    float prev_raw_sample;

    /*
     * noise_learned_frames
     * - noise_spectrum 학습에 사용된 frame 수
     */
    uint16_t noise_learned_frames;

    /*
     * 생성자
     * ------------------------------------------------------------------------
     * 내부 상태를 안전한 기본값으로 초기화한다.
     *
     * [초기화 정책]
     * - 포인터/핸들류는 nullptr
     * - bool 상태는 false
     * - cfg는 기본 설정으로 초기화
     * - DSP/출력/history 버퍼는 memset 0 초기화
     */
    ST_Impl()
    : spi(FSPI)
    {
        sensor_task_handle = nullptr;
        process_task_handle = nullptr;
        frame_queue = nullptr;
        mutex = nullptr;

        initialized = false;
        running = false;

        cfg = T20_makeDefaultConfig();

        active_fill_buffer = 0;
        active_sample_index = 0;
        dropped_frames = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;
        latest_vector_valid = false;
        latest_sequence_valid = false;

        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(work_frame, 0, sizeof(work_frame));
        memset(temp_frame, 0, sizeof(temp_frame));
        memset(window, 0, sizeof(window));
        memset(fft_buffer, 0, sizeof(fft_buffer));
        memset(power, 0, sizeof(power));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(log_mel, 0, sizeof(log_mel));
        memset(mel_bank, 0, sizeof(mel_bank));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        memset(latest_sequence_flat, 0, sizeof(latest_sequence_flat));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));
        memset(process_biquad_state, 0, sizeof(process_biquad_state));
    }
};

/*
 * 전역 현재 인스턴스 포인터
 * - ISR에서 현재 객체 내부 구현체에 접근하기 위한 용도
 * - 단일 인스턴스 사용 전제를 가진 설계
 */
extern CL_T20_Mfcc* g_t20_instance;

/* ============================================================================
 * Core 계층 함수
 * ========================================================================== */

/*
 * 설정 검증
 * - 입력 config가 시스템 제약 조건을 만족하는지 확인
 */
bool T20_validateConfig(const ST_T20_Config_t* p_cfg);

/*
 * task 중지
 * - interrupt 해제
 * - sensor/process task 삭제
 * - running=false 처리
 */
void T20_stopTasks(CL_T20_Mfcc::ST_Impl* p);

/*
 * 동기화 객체 해제
 * - queue, mutex 삭제
 */
void T20_releaseSyncObjects(CL_T20_Mfcc::ST_Impl* p);

/*
 * runtime 상태 초기화
 * - 내부 버퍼, 상태값, latest 결과, history 등을 초기화
 */
void T20_clearRuntimeState(CL_T20_Mfcc::ST_Impl* p);

/*
 * runtime 자원 전체 초기화
 * - stopTasks + releaseSyncObjects + clearRuntimeState
 */
void T20_resetRuntimeResources(CL_T20_Mfcc::ST_Impl* p);

/*
 * 현재 설정 축에 따라 accelX/Y/Z 중 하나 선택
 */
float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);

/*
 * MFCC history push
 * - 최신 MFCC를 history 버퍼에 추가
 * - history가 가득 차면 oldest를 밀어내고 newest 유지
 */
void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p,
                         const float* p_mfcc,
                         uint16_t p_dim);

/*
 * delta 계산
 * - MFCC history 기반 1차 시간 미분 특징 계산
 */
void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                 uint16_t p_dim,
                                 uint16_t p_delta_window,
                                 float* p_delta_out);

/*
 * delta-delta 계산
 * - MFCC history 기반 2차 시간 미분 특징 계산
 */
void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p,
                                      uint16_t p_dim,
                                      float* p_delta2_out);

/*
 * 단일 vector 조립
 * - [mfcc | delta | delta2] 순서로 1차원 분류기 입력 벡터 생성
 */
void T20_buildVector(const float* p_mfcc,
                     const float* p_delta,
                     const float* p_delta2,
                     uint16_t p_dim,
                     float* p_out_vec);

/*
 * sequence ring buffer 초기화
 * - frame 수와 feature dimension을 함께 지정
 */
void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb,
                 uint16_t p_frames,
                 uint16_t p_feature_dim);

/*
 * sequence ring buffer에 최신 feature vector push
 */
void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);

/*
 * sequence ready 여부 확인
 * - full=true 인지 기반으로 유효 시퀀스 여부 판단
 */
bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);

/*
 * sequence ring buffer를 1차원 flat 배열로 export
 * - oldest -> newest 순서
 */
void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);

/*
 * 최신 output 상태 갱신
 * - vector mode / sequence mode에 따라 latest 결과 반영
 */
void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);

/* ============================================================================
 * DSP 계층 함수
 * ========================================================================== */

/*
 * DSP 초기화
 * - FFT 라이브러리 초기화
 * - window / mel filterbank 준비
 */
bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p);

/*
 * BMI270 SPI 초기화
 */
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);

/*
 * BMI270 accel/gyro 1600Hz + DRDY interrupt 설정
 */
bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);

/*
 * biquad filter 설정 반영
 * - 현재 cfg 기준 coeff/state 초기화
 */
bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);

/*
 * Hz -> Mel 변환
 */
float T20_hzToMel(float p_hz);

/*
 * Mel -> Hz 변환
 */
float T20_melToHz(float p_mel);

/*
 * Hamming window 생성
 */
void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);

/*
 * Mel filterbank 생성
 */
void T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p);

/*
 * DC 제거
 * - 평균값을 빼서 0 centered 신호로 변환
 */
void T20_applyDCRemove(float* p_data, uint16_t p_len);

/*
 * Pre-emphasis 적용
 * - 이전 샘플 대비 차분 형태로 고주파 강조
 */
void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);

/*
 * Noise gate 적용
 * - 절대값 임계값 이하 신호를 0으로 클리핑
 */
void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);

/*
 * biquad filter 적용
 * - LPF / HPF / BPF 처리
 */
bool  T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);

bool  T20_makeFilterCoeffs(const ST_T20_Config_t* p_cfg, float* p_coeffs_out);

void  T20_applyBiquadFilter(const ST_T20_Config_t* p_cfg,
                            const float* p_coeffs,
                            float* p_state,
                            const float* p_in,
                            float* p_out,
                            uint16_t p_len);
                            
/*
 * Window 적용
 * - spectral leakage 감소 목적
 */
void T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len);

/*
 * Power spectrum 계산
 * - time domain -> FFT -> magnitude^2
 */
void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);

/*
 * Noise spectrum 학습
 * - 초기 noise frame 기반 평균 spectrum 구축
 */
void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);

/*
 * Spectral subtraction 적용
 * - 학습된 noise spectrum 제거
 */
void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);

/*
 * Mel filterbank 적용
 * - power spectrum -> log mel energy
 */
void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);

/*
 * DCT-II 계산
 * - log mel energy -> MFCC
 */
void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);

/*
 * 전체 MFCC 추출 파이프라인
 * - 전처리 -> filter -> window -> FFT -> power -> mel -> log -> DCT
 */

void  T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p,
                      const ST_T20_Config_t* p_cfg,
                      const float* p_filter_coeffs,
                      float* p_filter_state,
                      const float* p_frame,
                      float* p_mfcc_out);
                      
