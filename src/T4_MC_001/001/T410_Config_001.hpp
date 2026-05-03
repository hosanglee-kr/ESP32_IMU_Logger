/* ============================================================================
 * File: T410_Config_001.hpp
 * Summary: SMEA-100 Global Configuration & Constants (No Magic Numbers)
 * * [AI 메모: 제공 기능 요약]
 * 1. 시스템 전역 매개변수(샘플링, FFT, MFCC)를 Namespace 기반으로 관리.
 * 2. 하드웨어 의존적 핀 맵, 임계값 상수, RTOS 태스크 규격을 inline constexpr로 정의.
 ========================================================================== 
 */
#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"

namespace SmeaConfig {
    // 1. 샘플링 및 데이터 규격
    inline constexpr uint32_t SAMPLING_RATE = 42000;
    inline constexpr uint32_t CHANNELS = 2;
    inline constexpr uint32_t BITS_PER_SAMPLE = 32; // float 기준

    // 2. FFT 및 MFCC 설정
    inline constexpr uint32_t FFT_SIZE = 1024;
    inline constexpr uint32_t MFCC_COEFFS = 13;
    inline constexpr uint32_t MFCC_TOTAL_DIM = 39; // Static(13) + Delta(13) + Delta-Delta(13)
    inline constexpr uint32_t MEL_BANDS = 26;
    inline constexpr float    WINDOW_MS = 25.0f;
    inline constexpr float    HOP_MS = 10.0f;

    // 3. 버퍼 및 메모리 풀 규모
    inline constexpr uint32_t FEATURE_POOL_SIZE = 100; // 10 -> 100 으로 변경 (1초 딜레이 방어용)
    inline constexpr uint32_t RAW_BUFFER_MS = 500; // 사고 기록용 버퍼 길이
    inline constexpr uint32_t ALIGNMENT_BYTE = 16; // SIMD용

    // 4. 하이브리드 판정 임계치 (Rule-based)
    inline constexpr float RULE_ENRG_THRESHOLD = 0.00003f;
    inline constexpr float RULE_STDDEV_THRESHOLD = 0.12f;
    inline constexpr int   MIN_TRIGGER_COUNT = 1;

    // 5. 트리거 구간 설정 (Seconds)
    inline constexpr float NOISE_PROFILE_SEC = 0.15f;
    inline constexpr float VALID_START_SEC = 0.30f;
    inline constexpr float VALID_END_SEC = 0.50f;

    // 6. 하드웨어 핀 맵 및 통신 규격
    namespace Hardware {
        inline constexpr uint8_t  PIN_TRIGGER     = 21;
        inline constexpr uint8_t  PIN_I2S_BCLK    = 4;
        inline constexpr uint8_t  PIN_I2S_WS      = 5;
        inline constexpr uint8_t  PIN_I2S_DIN     = 6;
        inline constexpr uint32_t SERIAL_BAUD     = 115200;
        inline constexpr int      I2S_PORT_NUM    = 0; // I2S_NUM_0
    }

    // 7. RTOS 태스크 및 메모리 규격
    namespace Task {
        inline constexpr uint32_t CAPTURE_STACK_SIZE = 8192;
        inline constexpr uint32_t PROCESS_STACK_SIZE = 16384;
        inline constexpr uint8_t  CAPTURE_PRIORITY   = 10;
        inline constexpr uint8_t  PROCESS_PRIORITY   = 5;
        inline constexpr BaseType_t CORE_CAPTURE     = 0; // Core 0
        inline constexpr BaseType_t CORE_PROCESS     = 1; // Core 1
        inline constexpr uint32_t TICK_DELAY_MS      = 1;
        inline constexpr uint32_t ALIVE_CHECK_MS     = 5000;
        inline constexpr uint32_t SD_BOUNCE_BUF_SIZE = 4096;
    }

    // 8. DSP 고정 파라미터
    namespace Dsp {
        inline constexpr float NOTCH_FREQ_HZ = 60.0f;
        inline constexpr float NOTCH_Q_FACTOR = 30.0f;
        inline constexpr float PRE_EMPHASIS_ALPHA = 0.97f;
        inline constexpr float BEAMFORMING_GAIN = 0.5f;
        
        inline constexpr uint16_t FIR_TAPS           = 63;
        inline constexpr float    FIR_LPF_CUTOFF     = 3800.0f; // 42kHz 빔포밍 후 대역 정제용
        inline constexpr float    FIR_HPF_CUTOFF     = 100.0f;
        inline constexpr int      MEDIAN_WINDOW      = 5;
        inline constexpr float    NOISE_GATE_THRESH  = 0.001f;
        inline constexpr uint16_t NOISE_LEARN_FRAMES = 100;
        inline constexpr float    SPECTRAL_SUB_GAIN  = 1.2f;
    }
    
     // 9. 네트워크 및 웹 서버 규격 (신규 추가)
    namespace Network {
        inline constexpr uint16_t HTTP_PORT         = 80;
        inline constexpr char     WS_URI[]          = "/ws";
        inline constexpr uint32_t WIFI_CONN_TIMEOUT = 4000;
        inline constexpr uint32_t WIFI_RETRY_MS     = 10000;
        inline constexpr uint32_t LARGE_BUF_SIZE    = 8192; // 설정 파일 등 대용량 JSON 허용 크기
        
        inline constexpr char     NTP_SERVER_1[]    = "pool.ntp.org";
        inline constexpr char     NTP_SERVER_2[]    = "time.nist.gov";
        inline constexpr char     TZ_INFO[]         = "KST-9"; // 한국 표준시
        inline constexpr uint32_t NTP_TIMEOUT_MS    = 5000;
    }

    // 10. MQTT 및 경로 규격 (신규 추가)
    namespace Mqtt {
        inline constexpr uint32_t RETRY_INTERVAL_MS = 5000;
        inline constexpr uint16_t DEFAULT_PORT      = 1883;
        inline constexpr char     TOPIC_RESULT[]    = "smea100/inspection/result"; // 추가
    }

    namespace Path {
        inline constexpr char SYS_CFG_JSON[]    = "/sys/config.json";
        inline constexpr char WEB_INDEX[]       = "index.html";
        inline constexpr char WWW_ROOT[]        = "/www";
    }
    
        // 11. 스토리지 엔진 규격 (T410_Config_001.hpp 추가 분)
    namespace Storage {
        inline constexpr uint8_t  DMA_SLOT_COUNT     = 3;
        inline constexpr uint32_t DMA_SLOT_BYTES     = 16384; // (16KB) 42kHz Raw 데이터 대응 확장
        inline constexpr uint16_t MAX_ROTATE_LIST    = 16;
        inline constexpr uint16_t ROTATE_KEEP_MAX    = 8;
        inline constexpr uint16_t WATERMARK_HIGH     = 8;
        inline constexpr uint32_t IDLE_FLUSH_MS      = 250;
        
        inline constexpr uint8_t  PRE_TRIGGER_SEC    = 3;    // 사전 기록 버퍼 (초)
        inline constexpr uint32_t ROTATE_MB          = 100;  // 100MB 도달 시 파일 분할
        inline constexpr uint32_t ROTATE_MIN         = 60;   // 60분 도달 시 파일 분할
        
        inline constexpr char DIR_DATA[]             = "/t20_data";
        inline constexpr char DIR_RAW[]              = "/t20_data/raw";
        inline constexpr char FILE_INDEX_JSON[]      = "/sys/recorder_index.json";
        inline constexpr char FILE_INDEX_TMP[]       = "/sys/recorder_index.tmp";
    }
    

    // 12. ML 텐서 조립 (Sequence Builder) 규격
    namespace Ml {
        inline constexpr uint16_t MAX_SEQUENCE_FRAMES = 128; // 약 1.28초 분량 (Hop 10ms 기준)
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

