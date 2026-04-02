/* ============================================================================
 * File: T210_Def_Com_214.h
 * Version: v214_Final (2026-04-02)
 * Summary: 시스템 전역 공통 정의, RTOS 설정 및 하드웨어 핀 맵
 * Compiler: gnu++17 (C++17) 
 * ========================================================================== */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <string_view> // gnu++17 활용

/* ============================================================================
 * 1. 시스템 메타데이터 및 버전 정보
 * ========================================================================== */
inline constexpr std::string_view G_T20_VERSION_STR = "T20_Mfcc_v214_Final";
inline constexpr uint32_t G_T20_BUILD_TIMESTAMP     = 20260402UL;

/* ============================================================================
 * 2. 시스템 동작 타이밍 및 샘플링 (1600Hz 고정)
 * ========================================================================== */
// BMI270 ODR(Output Data Rate)과 동기화된 샘플링 주파수
inline constexpr float    G_T20_SAMPLE_RATE_HZ         = 1600.0f; 
// 시뮬레이션 모드에서 프레임 간격 (160ms = 약 6.25Hz 분석 주기)
inline constexpr uint16_t G_T20_RUNTIME_SIM_INTERVAL_MS = 160U; 

/* ============================================================================
 * 3. RTOS 태스크 설정 (ESP32-S3 Dual Core 최적화)
 * ========================================================================== */
// [튜닝 가이드] 스택 메모리 부족 시 0x400 단위로 증설할 것
inline constexpr uint16_t G_T20_SENSOR_TASK_STACK    = 6144U;  // Core 0: SPI/FIFO 데이터 취득 전용
inline constexpr uint16_t G_T20_PROCESS_TASK_STACK   = 12288U; // Core 1: DSP/SIMD/MFCC 고부하 연산
inline constexpr uint16_t G_T20_RECORDER_TASK_STACK  = 8192U;  // Core 1: SDMMC 파일 쓰기 및 관리

// 태스크 우선순위 (숫자가 높을수록 우선순위가 높음)
inline constexpr uint8_t  G_T20_SENSOR_TASK_PRIO     = 4U;     // 최상위: 데이터 유실 방지
inline constexpr uint8_t  G_T20_PROCESS_TASK_PRIO    = 3U;     // 중위: 실시간 분석 유지
inline constexpr uint8_t  G_T20_RECORDER_TASK_PRIO   = 2U;     // 하위: I/O 대기 가능

/* ============================================================================
 * 4. 하드웨어 핀 맵 (ESP32-S3 기반)
 * ========================================================================== */
// SPI 버스 (FSPI) 설정
#define G_T20_PIN_SPI_SCK                     12
#define G_T20_PIN_SPI_MISO                    13
#define G_T20_PIN_SPI_MOSI                    11
#define G_T20_PIN_BMI_CS                      10 // 센서 Chip Select
#define G_T20_PIN_BMI_INT1                    14 // FIFO Watermark 인터럽트

// 시스템 제어 인터페이스
#define G_T20_PIN_CTRL_BUTTON                 0  // 수동 측정 시작/종료 버튼 (GPIO 0)

/* ============================================================================
 * 5. 데이터 큐 및 스토리지 버퍼 (Triple Buffering 반영)
 * ========================================================================== */
// RTOS 메시지 큐 길이
inline constexpr uint8_t  G_T20_QUEUE_LEN             = 4U;    // Raw Frame 전달용
inline constexpr uint8_t  G_T20_REC_QUEUE_LEN         = 32U;   // 특징량 저장용 마진 확보

// SDMMC 쓰기 지연 대응용 Triple Buffering
inline constexpr uint8_t  G_T20_ZERO_COPY_DMA_SLOTS   = 3U;    
inline constexpr uint16_t G_T20_DMA_SLOT_BYTES        = 1024U; // 32-byte 정렬 권장

// 레코딩 배치 설정
inline constexpr uint16_t G_T20_RECORDER_BATCH_FLUSH_RECORDS  = 32U;
inline constexpr uint16_t G_T20_RECORDER_BATCH_TIMEOUT_MS     = 2000U;

/* ============================================================================
 * 6. 시스템 한도 및 버퍼 크기
 * ========================================================================== */
inline constexpr uint8_t  G_T20_CFG_PROFILE_COUNT           = 4U;    // 최대 프로필 저장 개수
inline constexpr uint8_t  G_T20_PROFILE_NAME_MAX            = 32U;   // 프로필 이름 최대 길이
inline constexpr uint16_t G_T20_SYSTEM_JSON_BUF_MAX         = 2048U; // JSON 직렬화 버퍼

/* ============================================================================
 * 7. 공통 상태 및 열거형 (Enums)
 * ========================================================================== */
// 시스템 전체 마스터 상태
typedef enum {
    EN_T20_STATE_IDLE = 0,      // 초기 상태
    EN_T20_STATE_READY,         // 하드웨어 준비 완료
    EN_T20_STATE_RUNNING,       // 측정 및 분석 중
    EN_T20_STATE_BUSY,          // 노이즈 학습 또는 파일 정리 중
    EN_T20_STATE_ERROR          // 치명적 오류 발생
} EM_T20_State_t;

// 데이터 소스 선택
typedef enum {
    EN_T20_SOURCE_OFF       = 255,
    EN_T20_SOURCE_SYNTHETIC = 0, // 가상 사인파 생성
    EN_T20_SOURCE_BMI270    = 1  // 실제 하드웨어 센서
} EM_T20_SourceMode_t;

// 결과 보고용
typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK = 1
} EM_T20_Result_t;

/* ============================================================================
 * 8. 유틸리티 함수 (Inline)
 * ========================================================================== */
// 상태값을 문자열로 변환 (Web API 및 시리얼 로그용)
static inline const char* T20_StateToString(EM_T20_State_t s)
{
    switch (s) {
        case EN_T20_STATE_IDLE:    return "IDLE";
        case EN_T20_STATE_READY:   return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_BUSY:    return "BUSY";
        case EN_T20_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}

/**
 * [v214 잔여 가능 주석 / TO-DO]
 * 1. SDMMC 4-bit 모드 전환 시 D1~D3 핀 정의를 본 파일의 Pin Mapping 섹션에 추가할 것.
 * 2. 부팅 시 버튼을 길게 누를 경우 'Safe Mode'로 진입하는 로직을 T230_handleControlInputs에 구현 고려.
 */
