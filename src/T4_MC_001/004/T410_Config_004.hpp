/* ============================================================================
 * File: T410_Config_003.hpp
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
    }

    namespace Hardware {
        inline constexpr uint8_t  PIN_TRIGGER_CONST  = 21;
        inline constexpr uint8_t  PIN_I2S_BCLK_CONST = 4;
        inline constexpr uint8_t  PIN_I2S_WS_CONST   = 5;
        inline constexpr uint8_t  PIN_I2S_DIN_CONST  = 6;
        inline constexpr int      I2S_PORT_NUM_CONST = 0;
        inline constexpr uint32_t SERIAL_BAUD_CONST  = 115200;
    }

    namespace Task {
        inline constexpr uint32_t CAPTURE_STACK_SIZE_CONST = 8192;
        inline constexpr uint32_t PROCESS_STACK_SIZE_CONST = 16384;
        inline constexpr uint8_t  CAPTURE_PRIORITY_CONST   = 10;
        inline constexpr uint8_t  PROCESS_PRIORITY_CONST   = 5;
        inline constexpr BaseType_t CORE_CAPTURE_CONST     = 0;
        inline constexpr BaseType_t CORE_PROCESS_CONST     = 1;
        inline constexpr uint32_t ALIVE_CHECK_MS_CONST     = 5000;
    }

    namespace StorageLimit {
        inline constexpr uint8_t  DMA_SLOT_COUNT_CONST  = 3;
        inline constexpr uint32_t DMA_SLOT_BYTES_CONST  = 16384;
        inline constexpr uint16_t MAX_ROTATE_LIST_CONST = 16;
        inline constexpr uint16_t ROTATE_KEEP_MAX_CONST = 8;
        inline constexpr uint16_t WATERMARK_HIGH_CONST  = 8;
    }

    namespace MlLimit {
        inline constexpr uint16_t MAX_SEQUENCE_FRAMES_CONST = 128;
    }
    
    namespace FeatureLimit {
        // [패치] 구조체 배열 크기 고정용 최대치 (메모리 정합성 방어)
        inline constexpr uint8_t  MAX_BAND_RMS_COUNT_CONST = 8; 
        inline constexpr uint16_t FIR_TAPS_CONST           = 63; 
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
        inline constexpr char WEB_INDEX_DEF[]       = "index.html";
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
