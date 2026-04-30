/* ============================================================================
 * File: T410_Def_009.hpp
 * Summary: SMEA-100 Global Configuration & Constants (JSON Dynamic Config Ready)
 * * [AI 메모: 리팩토링 및 방어 원칙 적용]
 * 1. 런타임 변경 불가 메모리/하드웨어 규격 -> _CONST 접미사 (정적 상수)
 * 2. 웹/JSON을 통해 변경될 동적 설정의 초기값 -> _DEF 접미사 (기본값)
 * 3. [보완] 밴드 개수 동적 할당 방어: 구조체 크기 고정을 위해 MAX 상수를
 * 도입하고, 실제 연산 개수는 _DEF 변수로 분리하여 OOM 및 정렬 파괴 방지.
 * 4. [계산식 연동 방어]: 의존성을 가지는 파라미터(MFCC 차원 수, 샘플 수,
 * 단위 변환 상수 등)를 하드코딩하지 않고 수식을 통해 자동 계산되도록 묶음 처리.
 ========================================================================== */
#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"

namespace SmeaConfig {

    // ========================================================================
    // [1] 정적 상수 (Static Constants - _CONST 접미사)
    // 런타임 변경 불가. 메모리 할당(Array Size, PSRAM Pool) 및 하드웨어 핀맵 규격.
    // ========================================================================

    namespace System {
        inline constexpr uint32_t   SAMPLING_RATE_CONST      = 42000;                    // I2S 마이크 데이터 수집 주파수 (Hz)
        inline constexpr uint32_t   CHANNELS_CONST           = 2;                        // 수집 채널 수 (L/R 스테레오 모드)
        inline constexpr uint32_t   BITS_PER_SAMPLE_CONST    = 32;                       // 샘플당 비트 수 (ICS43434 32bit PCM 데이터)
        inline constexpr uint32_t   ALIGNMENT_BYTE_CONST     = 16;                       // ESP-DSP 가속기 SIMD 최적화를 위한 강제 메모리 정렬 크기 (Bytes)

        inline constexpr uint32_t   FFT_SIZE_CONST           = 1024;                     // 고속 푸리에 변환(FFT) 윈도우 샘플 크기
        inline constexpr uint32_t   MFCC_COEFFS_CONST        = 13;                       // 추출할 기본 MFCC 계수(Static) 개수

        // [계산식 연동] 총 차원 수 = 기본 계수 * 3 (Static + Delta + Delta-Delta)
        inline constexpr uint32_t   MFCC_TOTAL_DIM_CONST     = MFCC_COEFFS_CONST * 3;    // MFCC 1D 텐서의 최종 차원 수
        inline constexpr uint32_t   MEL_BANDS_CONST          = 26;                       // 멜 필터뱅크(Mel-filterbank) 밴드 개수

        inline constexpr uint32_t   FEATURE_POOL_SIZE_CONST  = 100;                      // FSM 이중 큐 통신용 구조체 풀(Pool) 개수 (프레임 유실 방지용 여유분)

        // [계산식 연동] 32비트 정수 PCM을 -1.0 ~ 1.0 Float으로 정규화 (1.0 / 2^31)
        inline constexpr float      PCM_32BIT_SCALE_CONST    = 1.0f / (float)(1ULL << 31);
        inline constexpr float      MATH_EPSILON_CONST       = 1e-6f;                    // 일반 부동소수점 0 나누기(Divide by Zero) 방어용 Epsilon
        inline constexpr float      MATH_EPSILON_12_CONST    = 1e-12f;                   // 로그(Log) 및 제곱근(Sqrt) 0 진입 방어용 고정밀 Epsilon
        inline constexpr float      MS_PER_SEC_CONST         = 1000.0f;                  // 초(Sec) 단위를 밀리초(ms)로 역산하기 위한 곱셈 상수
    }

    namespace Hardware {
        inline constexpr uint8_t    PIN_TRIGGER_CONST        = 21;                       // 외부 DC 제어용 하드웨어 트리거 입력 핀
        inline constexpr uint8_t    PIN_I2S_BCLK_CONST       = 4;                        // I2S Bit Clock (BCLK) 핀
        inline constexpr uint8_t    PIN_I2S_WS_CONST         = 5;                        // I2S Word Select (LRCLK) 핀
        inline constexpr uint8_t    PIN_I2S_DIN_CONST        = 6;                        // I2S Data In (마이크 수신) 핀
        inline constexpr int        I2S_PORT_NUM_CONST       = 0;                        // ESP32 하드웨어 I2S 할당 포트 번호 (0 또는 1)
        inline constexpr uint32_t   SERIAL_BAUD_CONST        = 115200;                   // 디버깅용 PC 시리얼 통신 보드레이트
        inline constexpr int        I2S_DMA_BUF_COUNT_CONST  = 8;                        // I2S 드라이버가 할당할 DMA 수신 버퍼 링 개수
    }

    namespace Task {
        inline constexpr uint32_t   CAPTURE_STACK_SIZE_CONST = 8192;                     // 오디오 수집 태스크(Core 0) 할당 스택 크기 (Bytes)
        inline constexpr uint32_t   PROCESS_STACK_SIZE_CONST = 16384;                    // AI 및 신호 처리 태스크(Core 1) 할당 스택 크기 (Bytes)
        inline constexpr uint8_t    CAPTURE_PRIORITY_CONST   = 10;                       // 오디오 수집 태스크 우선순위 (프레임 유실 방지를 위해 가장 높게 설정)
        inline constexpr uint8_t    PROCESS_PRIORITY_CONST   = 5;                        // 신호 처리 태스크 우선순위
        inline constexpr BaseType_t CORE_CAPTURE_CONST       = 0;                        // 오디오 수집 코어 락업 (PRO_CPU)
        inline constexpr BaseType_t CORE_PROCESS_CONST       = 1;                        // 신호 처리 코어 락업 (APP_CPU)

        inline constexpr uint32_t   ALIVE_CHECK_MS_CONST     = 5000;                     // 메인 루프 시스템 헬스체크 및 로그 출력 주기 (ms)
        inline constexpr uint32_t   QUEUE_BLOCK_MS_CONST     = 100;                      // FSM 큐(Queue) 대기 시 무한 블로킹을 막는 타임아웃 타임 (ms)
        inline constexpr uint32_t   WDG_YIELD_MS_CONST       = 1;                        // 태스크 와치독(Task Watchdog) 해제를 위한 최소 양보 시간 (ms)
        inline constexpr uint32_t   REBOOT_DELAY_MS_CONST    = 500;                      // 소프트웨어 재부팅 전 데이터 저장을 보장하기 위한 대기 시간 (ms)
        inline constexpr uint32_t   BOOT_DELAY_MS_CONST      = 1000;                     // 시스템 부팅 직후 센서 및 전원 안정화를 위한 초기 대기 시간 (ms)
        inline constexpr uint32_t   MAIN_LOOP_DELAY_MS_CONST = 10;                       // 메인 런프(Core 1)가 백그라운드 네트워크 처리를 위해 양보하는 시간 (ms)
    }

    namespace DspLimit {
        inline constexpr int        MAX_MEDIAN_WINDOW_CONST  = 15;                       // 노이즈 제거용 Median 필터 배열의 최대 허용 크기 (스택 파괴 방어 상한선)
    }

    namespace StorageLimit {
        inline constexpr uint8_t    DMA_SLOT_COUNT_CONST     = 3;                        // SD카드 고속 저장을 위한 DMA 핑퐁 버퍼 덩어리 개수
        inline constexpr uint32_t   DMA_SLOT_BYTES_CONST     = 16384;                    // DMA 슬롯 1개당 바이트 크기 (16KB)
        inline constexpr uint16_t   MAX_ROTATE_LIST_CONST    = 16;                       // 파일 로테이션 기록을 보관할 인덱스 배열 최대 개수
        inline constexpr uint16_t   ROTATE_KEEP_MAX_CONST    = 8;                        // SD카드 용량 관리를 위해 유지할 최근 파일의 최대 개수
        inline constexpr uint16_t   WATERMARK_HIGH_CONST     = 8;                        // SD카드 강제 Flush를 유발하는 캐시 임계 프레임 수

        inline constexpr uint16_t   MAX_PATH_LEN_CONST       = 128;                      // 리눅스 파일 시스템 절대 경로 문자열 버퍼 최대 길이
        inline constexpr uint16_t   MAX_PREFIX_LEN_CONST     = 16;                       // 파일명 생성을 위한 이벤트 접두어(Prefix) 버퍼 최대 길이

        // [계산식 연동] 저장소 단위 변환
        inline constexpr uint32_t   BYTES_PER_MB_CONST       = 1024 * 1024;              // 로테이션 용량 연산용 (MB -> Bytes 변환)
        inline constexpr uint32_t   MS_PER_MIN_CONST         = 60 * 1000;                // 로테이션 시간 연산용 (Min -> ms 변환)
    }

    namespace MlLimit {
        inline constexpr uint16_t   MAX_SEQUENCE_FRAMES_CONST = 128;                     // TinyML 추론 시퀀스 텐서를 조립하기 위한 2D 배열의 최대 프레임 상한선
    }

    namespace FeatureLimit {
        inline constexpr uint8_t    MAX_BAND_RMS_COUNT_CONST   = 8;                      // 멀티 밴드 에너지를 담을 구조체 내부 배열의 최대 고정 크기 (OOM 방어)
        inline constexpr uint16_t   FIR_TAPS_CONST             = 63;                     // FIR 필터 차수(Taps) (Odd Number 권장, 계수 배열 크기 결정용)
        inline constexpr uint16_t   DELTA_HISTORY_FRAMES_CONST = 5;                      // MFCC 시간 미분(Delta) 연산을 위해 N=2(총 5프레임)를 담을 링버퍼 크기

        // [계산식 연동] 충격음 판정 윈도우. 샘플링 레이트 변동 시 자동 대응.
        inline constexpr uint32_t   STA_SAMPLES_CONST          = System::SAMPLING_RATE_CONST / 1000; // 단기 에너지 윈도우 (1ms 분량 샘플 수: 42)
        inline constexpr uint32_t   LTA_SAMPLES_CONST          = System::SAMPLING_RATE_CONST / 100;  // 장기 에너지 윈도우 (10ms 분량 샘플 수: 420)

        inline constexpr float      MEL_SCALE_2595_CONST       = 2595.0f;                // Mel 변환 공식 수학 상수 1
        inline constexpr float      MEL_SCALE_700_CONST        = 700.0f;                 // Mel 변환 공식 수학 상수 2
        inline constexpr uint16_t   MAX_PEAK_CANDIDATES_CONST  = 128;                    // 1차 주파수 피크 검출 대상 배열 임시 크기
        inline constexpr uint8_t    TOP_PEAKS_COUNT_CONST      = 5;                      // 최종적으로 구조체에 저장할 핵심 주파수 피크 개수
        inline constexpr uint8_t    CEPS_TARGET_COUNT_CONST    = 4;                      // 결함 주기(RPM) 검출용 켑스트럼 타겟(Target) 개수
        inline constexpr float      CEPS_TOLERANCE_CONST       = 0.0003f;                // 켑스트럼 주파수 검색 시 허용할 오차 범위 Epsilon

    }

    namespace DecisionLimit {
        inline constexpr uint8_t    MAX_TRIAL_COUNT_CONST      = 3;                      // 1회 트리거 시 노이즈 학습 및 본 검사를 진행할 시나리오 횟수
    }

    namespace NetworkLimit {
        inline constexpr uint16_t   MAX_SSID_LEN_CONST         = 32;                     // WiFi SSID 문자열 버퍼 상한 (WPA 규격)
        inline constexpr uint16_t   MAX_PW_LEN_CONST           = 64;                     // WiFi 암호 문자열 버퍼 상한 (WPA 규격)
        inline constexpr uint16_t   MAX_IP_LEN_CONST           = 16;                     // IP 주소(IPv4) 문자열 버퍼 상한

        inline constexpr uint8_t    MAX_MULTI_AP_CONST              = 3;                 // Auto-Fallback 등 다중 접속 지원 AP 리스트 최대 개수
        inline constexpr uint16_t   MAX_BROKER_LEN_CONST            = 64;                // MQTT 브로커 도메인/IP 버퍼 상한 길이
        inline constexpr uint32_t   WIFI_MODE_SWITCH_DELAY_MS_CONST = 50;                // WiFi 하드웨어 모드 전환 시 안테나 리셋 대기 시간 (ms)
        inline constexpr uint32_t   WIFI_DISCONNECT_DELAY_MS_CONST  = 100;               // 이전 WiFi 연결 강제 해제 후 소켓 소멸 대기 시간 (ms)
    }


    // ========================================================================
    // [2] 동적 설정 기본값 (Dynamic Defaults - _DEF 접미사)
    // Web API 및 JSON 파일을 통해 런타임에 변경 가능한 설정의 초기(공장)값.
    // ========================================================================

    namespace Dsp {
        inline constexpr float      WINDOW_MS_DEF                  = 25.0f;              // 1회 연산을 위해 가져올 오디오 윈도우 시간 (ms)
        inline constexpr float      HOP_MS_DEF                     = 10.0f;              // 슬라이딩 윈도우(오버랩) 이동 간격 (ms)

        inline constexpr float      NOTCH_FREQ_HZ_DEF              = 60.0f;              // 노이즈 제거용 1차 노치 필터 타겟 주파수 (전원 노이즈 등)
        inline constexpr float      NOTCH_FREQ_2_HZ_DEF            = 120.0f;             // 노이즈 제거용 2차 노치 필터 타겟 주파수 (하모닉스)
        inline constexpr float      NOTCH_Q_FACTOR_DEF             = 30.0f;              // 노치 필터의 뾰족함(Q-Factor) 정도
        inline constexpr float      PRE_EMPHASIS_ALPHA_DEF         = 0.97f;              // 고주파 성분 강화를 위한 프리엠파시스 알파 값
        inline constexpr float      BEAMFORMING_GAIN_DEF           = 0.5f;               // L/R 2채널 마이크 병합(Broadside) 시의 증폭 게인

        inline constexpr float      FIR_LPF_CUTOFF_DEF             = 20000.0f;           // 저주파 통과 필터(LPF) 컷오프 주파수
        inline constexpr float      FIR_HPF_CUTOFF_DEF             = 100.0f;             // 고주파 통과 필터(HPF) 컷오프 주파수
        inline constexpr int        MEDIAN_WINDOW_DEF              = 5;                  // 돌발 스파이크 노이즈 제거용 미디언 윈도우 크기
        inline constexpr float      NOISE_GATE_THRESH_DEF          = 0.001f;             // 일정 진폭 이하는 0으로 깎아버리는 노이즈 게이트 임계값
        inline constexpr int        NOISE_LEARN_FRAMES_DEF         = 100;                // 스펙트럼 감산을 위해 배경 소음을 수집할 초기 프레임 수
        inline constexpr float      SPECTRAL_SUB_GAIN_DEF          = 1.2f;               // 배경 소음 감산 강도(가중치)
    }

    namespace Feature {
        inline constexpr uint8_t   BAND_RMS_COUNT_DEF = 4;                               // 런타임에 순회 및 추출할 활성 멀티 밴드(RMS) 개수

        // 멀티 밴드 에너지를 추출할 각 대역별 주파수 범위 (Start, End Hz)
        inline constexpr float BAND_RANGES_DEF[FeatureLimit::MAX_BAND_RMS_COUNT_CONST][2] = {
            {10.0f, 150.0f},     // 0: 저주파 대역
            {150.0f, 1000.0f},   // 1: 중저주파 대역
            {1000.0f, 5000.0f},  // 2: 중고주파 대역
            {5000.0f, 20000.0f}, // 3: 고주파 대역
            {0.0f, 0.0f},        // 4: (Reserved)
            {0.0f, 0.0f},        // 5: (Reserved)
            {0.0f, 0.0f},        // 6: (Reserved)
            {0.0f, 0.0f}         // 7: (Reserved)
        };

		inline constexpr float      PEAK_AMPLITUDE_MIN_DEF 	   = 0.5f;                   // 0.5 이하 노이즈 피크 무시
	    inline constexpr float      PEAK_FREQ_GAP_HZ_MIN_DEF   = 10.0f;                  // 10Hz 반경 내 중복 피크 무시

    }

    namespace Decision {
        inline constexpr float RULE_ENRG_THRESHOLD_DEF   = 0.00003f;                     // 불량(NG) 판정을 위한 1차 에너지 임계값
        inline constexpr float RULE_STDDEV_THRESHOLD_DEF = 0.12f;                        // 파형 분산(편차) 기반 2차 불량 판정 임계값
        inline constexpr float TEST_NG_MIN_ENERGY_DEF    = 1e-8f;                        // 마이크 하드웨어 고장/단선을 판단하기 위한 최소 에너지 하한선

        inline constexpr float STA_LTA_THRESHOLD_DEF     = 3.0f;						// 임펄스(충격음) NG 판정을 위한 STA/LTA 비율 임계값 (3.0배 이상 시 NG 판정 기본값NG)

        inline constexpr int   MIN_TRIGGER_COUNT_DEF     = 1;                            // NG 판정을 확정 짓기 위해 요구되는 연속 트리거 프레임 수

        inline constexpr float NOISE_PROFILE_SEC_DEF     = 0.15f;                        // 노이즈 프로파일 수집에 소요할 시간 (초)
        inline constexpr float VALID_START_SEC_DEF       = 0.30f;                        // 녹음 시작 후 데이터가 안정화되는 검사 시작 스킵 시간 (초)
        inline constexpr float VALID_END_SEC_DEF         = 0.50f;                        // 유효한 검사를 종료하고 다음 시나리오로 넘길 시간 (초)
    }

    namespace Storage {
        inline constexpr uint8_t  PRE_TRIGGER_SEC_DEF   = 3;                               // 트리거 이전 상황을 블랙박스처럼 남기기 위한 과거 보존 시간 (초)
        inline constexpr uint32_t ROTATE_MB_DEF         = 100;                             // 파일 크기가 이 값을 넘으면 분할하여 저장 (MB)
        inline constexpr uint32_t ROTATE_MIN_DEF        = 60;                              // 파일 기록 시간이 이 값을 넘으면 분할하여 저장 (Min)
        inline constexpr uint32_t IDLE_FLUSH_MS_DEF     = 250;                             // 데이터가 적게 들어올 때 강제로 SD카드에 플러시하는 대기 시간 (ms)
    }

    namespace Path {
        inline constexpr char DIR_DATA_DEF[]            = "/t20_data";                       // 특징량(.bin) 파일이 저장될 SD카드 기본 경로
        inline constexpr char DIR_RAW_DEF[]             = "/t20_data/raw";                   // 원본 파형(.pcm) 파일이 저장될 SD카드 기본 경로
        inline constexpr char SYS_CFG_JSON_DEF[]        = "/sys/config_004_01.json";         // 런타임 설정이 보관되는 내부 플래시 파일 경로
        inline constexpr char FILE_INDEX_JSON_DEF[]     = "/sys/recorder_index.json";        // SD카드 녹음 파일의 메타/인덱스 관리 경로
        inline constexpr char FILE_INDEX_TMP_DEF[]      = "/sys/recorder_index.tmp";         // 인덱스 원자적 저장을 위한 임시 경로
        inline constexpr char WEB_INDEX_DEF[]           = "T4_009_003.html";                 // 웹서버 메인 진입점 HTML 파일명
        inline constexpr char WWW_ROOT_DEF[]            = "/www";                            // 웹서버 정적 파일 배포 기본 라우트
    }

    namespace Network {
        inline constexpr uint16_t HTTP_PORT_DEF         = 80;                            // 비동기 웹서버(Web UI) 서비스 포트
        inline constexpr char     WS_URI_DEF[]          = "/ws";                         // 차트 데이터 브로드캐스트용 웹소켓 엔드포인트
        inline constexpr uint32_t WIFI_CONN_TIMEOUT_DEF = 4000;                          // 공유기 연결 시도 타임아웃 제한 (ms)
        inline constexpr uint32_t WIFI_RETRY_MS_DEF     = 10000;                         // 연결 끊김 시 재접속을 시도할 백오프 인터벌 (ms)
        inline constexpr uint32_t LARGE_BUF_SIZE_DEF    = 8192;                          // REST API Payload 파싱용 힙 할당 상한선 방어 (Bytes)

        inline constexpr char     NTP_SERVER_1_DEF[]    = "pool.ntp.org";                // 메인 시간 동기화 서버
        inline constexpr char     NTP_SERVER_2_DEF[]    = "time.nist.gov";               // 보조 시간 동기화 서버
        inline constexpr char     TZ_INFO_DEF[]         = "KST-9";                       // 대한민국 표준시(KST) 타임존 룰
        inline constexpr uint32_t NTP_TIMEOUT_MS_DEF    = 5000;                          // 시간 동기화 실패 타임아웃 제한 (ms)

        // 공장 초기화용 WiFi 기본 설정값
        inline constexpr uint8_t  WIFI_MODE_DEF         = 3;                             // 기본 통신 모드 (3: Auto-Fallback 방어 모드)
        inline constexpr const char* AP_SSID_DEF        = "SMEA_100_AP";                 // 자체 공유기(SoftAP) 가동 시 기본 SSID
        inline constexpr const char* AP_PW_DEF          = "12345678";                    // 자체 공유기(SoftAP) 가동 시 기본 패스워드
        inline constexpr const char* AP_IP_DEF          = "192.168.4.1";                 // 자체 공유기(SoftAP) 가동 시 기본 할당 IP
    }

    namespace Mqtt {
        inline constexpr uint32_t RETRY_INTERVAL_MS_DEF = 5000;                          // 브로커 끊김 시 재연결 대기 시간 (ms)
        inline constexpr uint16_t DEFAULT_PORT_DEF      = 1883;                          // MQTT 기본 프로토콜 포트
        inline constexpr char     TOPIC_RESULT_DEF[]    = "smea100/inspection/result";   // 판정 결과를 전송할 MQTT Pub 토픽
    }
}

// ========================================================================
// [3] 시스템 열거형 (Enum Classes)
// ========================================================================

enum class SystemState : uint8_t {
    INIT = 0,       // 시스템 부팅 및 메모리 초기화 상태
    READY,          // 외부 트리거 대기 (마이크 및 통신 유휴 상태)
    MONITORING,     // 상시 모니터링 (파형 분석 중이나 저장하지 않음)
    RECORDING,      // 이상 감지 또는 수동 개입에 의한 파일 로깅 모드
    ERROR           // 하드웨어 고장 패닉 상태
};

enum class DetectionResult : uint8_t {
    PASS = 0,       // 정상 제품 및 환경
    RULE_NG = 1,    // 임계치/에너지 기반 NG(불량) 적발
    TEST_NG = 2,    // 센서 고장(에너지 0) 에러 발생
    ML_NG = 3       // TinyML 추론 모델 기반 NG(불량) 적발
};

enum class SystemCommand : uint8_t {
    CMD_MANUAL_RECORD_START, // 웹 UI를 통한 강제 녹음 시작 지시
    CMD_MANUAL_RECORD_STOP,  // 웹 UI를 통한 강제 녹음 종료 지시
    CMD_LEARN_NOISE,         // 능동적 배경 소음(ANC) 재학습 1회 스케줄링
    CMD_REBOOT               // 안전한 세션 종료 후 장비 소프트 리셋 지시
};
