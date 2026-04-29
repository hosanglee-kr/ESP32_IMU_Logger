/* ============================================================================
 * File: T410_Config_007.hpp
 * Summary: SMEA-100 Global Configuration & Constants (JSON Dynamic Config Ready)
 * * [AI 메모: 리팩토링 및 방어 원칙 적용]
 * 1. 런타임 변경 불가 메모리/하드웨어 규격 -> _CONST 접미사 (정적 상수)
 * 2. 웹/JSON을 통해 변경될 동적 설정의 초기값 -> _DEF 접미사 (기본값)
 * 3. [보완] 밴드 개수 동적 할당 방어: 구조체 크기 고정을 위해 MAX 상수를
 * 도입하고, 실제 연산 개수는 _DEF 변수로 분리하여 OOM 및 정렬 파괴 방지.
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
        inline constexpr uint32_t SAMPLING_RATE_CONST   = 42000;
        inline constexpr uint32_t CHANNELS_CONST        = 2;
        inline constexpr uint32_t BITS_PER_SAMPLE_CONST = 32;
        inline constexpr uint32_t ALIGNMENT_BYTE_CONST  = 16;
        
        inline constexpr uint32_t FFT_SIZE_CONST        = 1024;
        inline constexpr uint32_t MFCC_COEFFS_CONST     = 13;
        inline constexpr uint32_t MFCC_TOTAL_DIM_CONST  = 39;
        inline constexpr uint32_t MEL_BANDS_CONST       = 26;
        
        inline constexpr uint32_t FEATURE_POOL_SIZE_CONST = 100;
        
        constexpr float PCM_32BIT_SCALE_CONST = 1.0f / 2147483648.0f;   // 32bit PCM -> Float 변환 스케일 상수 (1.0 / 2^31)
        constexpr float MATH_EPSILON_CONST = 1e-6f;   // 부동소수점 0 나누기 연산 방어용 Epsilon
        constexpr float MATH_EPSILON_12_CONST = 1e-12f; // 0 나누기 방어 정밀도 
        constexpr float MS_PER_SEC_CONST = 1000.0f;  // 밀리초 단위를 초 단위로 변환하기 위한 상수
        
 
    }

    namespace Hardware {
        inline constexpr uint8_t  PIN_TRIGGER_CONST  = 21;
        inline constexpr uint8_t  PIN_I2S_BCLK_CONST = 4;
        inline constexpr uint8_t  PIN_I2S_WS_CONST   = 5;
        inline constexpr uint8_t  PIN_I2S_DIN_CONST  = 6;
        inline constexpr int      I2S_PORT_NUM_CONST = 0;
        inline constexpr uint32_t SERIAL_BAUD_CONST  = 115200;
        constexpr int I2S_DMA_BUF_COUNT_CONST = 8;   // I2S DMA 버퍼 개수 
    }

    namespace Task {
        inline constexpr uint32_t CAPTURE_STACK_SIZE_CONST = 8192;
        inline constexpr uint32_t PROCESS_STACK_SIZE_CONST = 16384;
        inline constexpr uint8_t  CAPTURE_PRIORITY_CONST   = 10;
        inline constexpr uint8_t  PROCESS_PRIORITY_CONST   = 5;
        inline constexpr BaseType_t CORE_CAPTURE_CONST     = 0;
        inline constexpr BaseType_t CORE_PROCESS_CONST     = 1;
        inline constexpr uint32_t ALIVE_CHECK_MS_CONST     = 5000;
        
        // FSM 태스크 제어용 지연 시간 상수
        constexpr uint32_t QUEUE_BLOCK_MS_CONST = 100;
        constexpr uint32_t WDG_YIELD_MS_CONST = 1;
        constexpr uint32_t REBOOT_DELAY_MS_CONST = 500;
        
        // 시스템 부팅 시 시리얼 포트 등 하드웨어 안정화 대기 시간
        constexpr uint32_t BOOT_DELAY_MS_CONST = 1000;
        // 메인 루프(Core 1)의 백그라운드 태스크 양보 지연 시간
        constexpr uint32_t MAIN_LOOP_DELAY_MS_CONST = 10;
    }
    
    namespace DspLimit {
        constexpr int MAX_MEDIAN_WINDOW_CONST = 15;    // Median 필터 최대 윈도우 크기 (스택 오버플로우 방어용 상한선)

    }

    namespace StorageLimit {
        inline constexpr uint8_t  DMA_SLOT_COUNT_CONST  = 3;
        inline constexpr uint32_t DMA_SLOT_BYTES_CONST  = 16384;
        inline constexpr uint16_t MAX_ROTATE_LIST_CONST = 16;
        inline constexpr uint16_t ROTATE_KEEP_MAX_CONST = 8;
        inline constexpr uint16_t WATERMARK_HIGH_CONST  = 8;
        
        // 파일 절대 경로 및 문자열 버퍼 최대 길이
        constexpr uint16_t MAX_PATH_LEN_CONST = 128;
        // 세션 접두어(prefix) 문자열 버퍼 최대 길이
        constexpr uint16_t MAX_PREFIX_LEN_CONST = 16;
        // 메가바이트(MB) -> 바이트(Bytes) 변환 상수 (1024 * 1024)
        constexpr uint32_t BYTES_PER_MB_CONST = 1048576;
        // 분(Minute) -> 밀리초(ms) 변환 상수
        constexpr uint32_t MS_PER_MIN_CONST = 60000;
    }

    namespace MlLimit {
        inline constexpr uint16_t MAX_SEQUENCE_FRAMES_CONST = 128;
    }
    
    namespace FeatureLimit {
        
        // 구조체 배열 크기 고정용 최대치 (메모리 정합성 방어)
        inline constexpr uint8_t  MAX_BAND_RMS_COUNT_CONST = 8; 
        inline constexpr uint16_t FIR_TAPS_CONST           = 63; 
        
        constexpr uint16_t DELTA_HISTORY_FRAMES_CONST = 5; // N=2 Delta 연산 버퍼
        constexpr uint32_t STA_SAMPLES_CONST = 42;         // 단기 에너지 윈도우(1ms)
        constexpr uint32_t LTA_SAMPLES_CONST = 420;        // 장기 에너지 윈도우(10ms)
        constexpr float    MEL_SCALE_2595_CONST = 2595.0f;
        constexpr float    MEL_SCALE_700_CONST = 700.0f;
        constexpr uint16_t MAX_PEAK_CANDIDATES_CONST = 128;
        constexpr uint8_t  TOP_PEAKS_COUNT_CONST = 5;
        constexpr uint8_t  CEPS_TARGET_COUNT_CONST = 4;
        constexpr float    CEPS_TOLERANCE_CONST = 0.0003f;
    }
    
    
    namespace DecisionLimit {
        // 검증 시나리오 최대 반복 횟수 (워밍업 포함)
        constexpr uint8_t MAX_TRIAL_COUNT_CONST = 3;
    }
    
    namespace NetworkLimit {
        
        // 네트워크 관련 문자열 버퍼 상한선
        constexpr uint16_t MAX_SSID_LEN_CONST = 32;
        constexpr uint16_t MAX_PW_LEN_CONST = 64;
        constexpr uint16_t MAX_IP_LEN_CONST = 16;
        
        // 다중 접속 지원 AP 최대 개수
        constexpr uint8_t MAX_MULTI_AP_CONST = 3;
        
        // MQTT 브로커 주소 최대 길이
        constexpr uint16_t MAX_BROKER_LEN_CONST = 64;
        
        // WiFi 하드웨어 모드 전환 대기 시간 (ms)
        constexpr uint32_t WIFI_MODE_SWITCH_DELAY_MS_CONST = 50;
        
        // WiFi 기존 연결 해제 대기 시간 (ms)
        constexpr uint32_t WIFI_DISCONNECT_DELAY_MS_CONST = 100;
    }
    

    // ========================================================================
    // [2] 동적 설정 기본값 (Dynamic Defaults - _DEF 접미사)
    // Web API 및 JSON 파일을 통해 런타임에 변경 가능한 설정의 초기값.
    // ========================================================================
    
    namespace Dsp {
        inline constexpr float WINDOW_MS_DEF = 25.0f;
        inline constexpr float HOP_MS_DEF    = 10.0f;
        
        inline constexpr float NOTCH_FREQ_HZ_DEF      = 60.0f;
        inline constexpr float NOTCH_FREQ_2_HZ_DEF    = 120.0f;
        inline constexpr float NOTCH_Q_FACTOR_DEF     = 30.0f;
        inline constexpr float PRE_EMPHASIS_ALPHA_DEF = 0.97f;
        inline constexpr float BEAMFORMING_GAIN_DEF   = 0.5f;
        
        inline constexpr float FIR_LPF_CUTOFF_DEF     = 20000.0f;
        inline constexpr float FIR_HPF_CUTOFF_DEF     = 100.0f;
        inline constexpr int   MEDIAN_WINDOW_DEF      = 5;
        inline constexpr float NOISE_GATE_THRESH_DEF  = 0.001f;
        inline constexpr int   NOISE_LEARN_FRAMES_DEF = 100;
        inline constexpr float SPECTRAL_SUB_GAIN_DEF  = 1.2f;
    }

    namespace Feature {
        // [패치] 실제 런타임에 추출/순회할 활성 밴드 개수 (JSON으로 변경 가능)
        inline constexpr uint8_t BAND_RMS_COUNT_DEF = 4; 
        
        // [패치] 여유 공간(MAX 8)을 확보해둔 밴드 범위 배열 기본값
        inline constexpr float BAND_RANGES_DEF[FeatureLimit::MAX_BAND_RMS_COUNT_CONST][2] = {
            {10.0f, 150.0f},     // 0: 저주파
            {150.0f, 1000.0f},   // 1: 중저주파
            {1000.0f, 5000.0f},  // 2: 중고주파
            {5000.0f, 20000.0f}, // 3: 고주파
            {0.0f, 0.0f},        // 4: (Reserved)
            {0.0f, 0.0f},        // 5: (Reserved)
            {0.0f, 0.0f},        // 6: (Reserved)
            {0.0f, 0.0f}         // 7: (Reserved)
        };
    }

    namespace Decision {
        inline constexpr float RULE_ENRG_THRESHOLD_DEF   = 0.00003f;
        inline constexpr float RULE_STDDEV_THRESHOLD_DEF = 0.12f;
        inline constexpr float TEST_NG_MIN_ENERGY_DEF    = 1e-8f;
        
        inline constexpr int   MIN_TRIGGER_COUNT_DEF     = 1; // 누락 복구
        
        inline constexpr float NOISE_PROFILE_SEC_DEF     = 0.15f;
        inline constexpr float VALID_START_SEC_DEF       = 0.30f;
        inline constexpr float VALID_END_SEC_DEF         = 0.50f;
    }

    namespace Storage {
        inline constexpr uint8_t  PRE_TRIGGER_SEC_DEF = 3;
        inline constexpr uint32_t ROTATE_MB_DEF       = 100;
        inline constexpr uint32_t ROTATE_MIN_DEF      = 60;
        inline constexpr uint32_t IDLE_FLUSH_MS_DEF   = 250;
    }
    
    namespace Path {
        inline constexpr char DIR_DATA_DEF[]        = "/t20_data";
        inline constexpr char DIR_RAW_DEF[]         = "/t20_data/raw";
        inline constexpr char SYS_CFG_JSON_DEF[]    = "/sys/config_004_01.json";
        inline constexpr char FILE_INDEX_JSON_DEF[] = "/sys/recorder_index.json";
        inline constexpr char FILE_INDEX_TMP_DEF[]  = "/sys/recorder_index.tmp";
        inline constexpr char WEB_INDEX_DEF[]       = "T4_009_002.html";
        inline constexpr char WWW_ROOT_DEF[]        = "/www";
    }

    namespace Network {
        inline constexpr uint16_t HTTP_PORT_DEF         = 80;
        inline constexpr char     WS_URI_DEF[]          = "/ws";
        inline constexpr uint32_t WIFI_CONN_TIMEOUT_DEF = 4000;
        inline constexpr uint32_t WIFI_RETRY_MS_DEF     = 10000;
        inline constexpr uint32_t LARGE_BUF_SIZE_DEF    = 8192; 
        
        inline constexpr char     NTP_SERVER_1_DEF[]    = "pool.ntp.org";
        inline constexpr char     NTP_SERVER_2_DEF[]    = "time.nist.gov";
        inline constexpr char     TZ_INFO_DEF[]         = "KST-9";
        inline constexpr uint32_t NTP_TIMEOUT_MS_DEF    = 5000;
        
        // 공장 초기화용 WiFi 기본 설정값
        constexpr uint8_t WIFI_MODE_DEF = 3; // 3: AUTO_FALLBACK
        constexpr const char* AP_SSID_DEF = "SMEA_100_AP";
        constexpr const char* AP_PW_DEF = "12345678";
        constexpr const char* AP_IP_DEF = "192.168.4.1";
    }

    namespace Mqtt {
        inline constexpr uint32_t RETRY_INTERVAL_MS_DEF = 5000;
        inline constexpr uint16_t DEFAULT_PORT_DEF      = 1883;
        inline constexpr char     TOPIC_RESULT_DEF[]    = "smea100/inspection/result";
    }
}

enum class SystemState : uint8_t {
    INIT = 0, 
    READY, 
    MONITORING, 
    RECORDING, 
    ERROR
};

enum class DetectionResult : uint8_t {
    PASS = 0, 
    RULE_NG = 1, 
    TEST_NG = 2, 
    ML_NG = 3
};

enum class SystemCommand : uint8_t {
    CMD_MANUAL_RECORD_START, 
    CMD_MANUAL_RECORD_STOP, 
    CMD_LEARN_NOISE, 
    CMD_REBOOT
};








