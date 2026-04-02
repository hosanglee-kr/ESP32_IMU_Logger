/* ============================================================================
 * File: T210_Def_Com_214.h
 * Summary: 시스템 전역 공통 정의, 상태 코드 및 하드웨어 구성
 * Standard: gnu++17 (C++17 with GNU extensions)
 * ========================================================================== */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * [REMIND / TO-DO LIST] - 시스템 안정화를 위한 잔여 업무
 * 1. [ ] 1600Hz ODR에서 SDMMC 1-bit 쓰기 대역폭 실측 (Jitter 발생 시 Triple Buffering 점검)
 * 2. [ ] GPIO 0(Button) 입력 시 외부 풀업 저항 유무에 따른 내부 Pull-up 설정 검증
 * 3. [ ] gnu++17의 constexpr을 활용한 부동소수점 상수 최적화 적용 확인
 * 4. [ ] 하드웨어 Watchdog 타임아웃 주기를 1600Hz Batch 처리 시간에 맞춰 재조정
 * ========================================================================== */

/* --- Version Control --- */
inline constexpr char const* G_T20_VERSION_STR = "T20_Mfcc_v214_gnu17";

/* ============================================================================
 * 1. 시스템 동작 타이밍 및 시뮬레이션
 * ========================================================================== */
// v210 복구: 실제 센서가 없을 때 데이터 흐름을 시뮬레이션하기 위한 주기
inline constexpr uint16_t G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS = 160U;
inline constexpr float    G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT = 0.20f;

/* ============================================================================
 * 2. RTOS 태스크 구성 (ESP32-S3 듀얼 코어 분산)
 * ========================================================================== */
// 스택 사이즈는 SIMD(ESP-DSP) 연산 부하를 고려하여 넉넉히 할당
#define G_T20_SENSOR_TASK_STACK               6144U    // Core 0: SPI/FIFO 데이터 수집
#define G_T20_PROCESS_TASK_STACK              12288U   // Core 1: MFCC/SIMD 가속 연산
#define G_T20_RECORDER_TASK_STACK             8192U    // Core 1: SDMMC 파일 로깅

#define G_T20_SENSOR_TASK_PRIO                4U
#define G_T20_PROCESS_TASK_PRIO               3U
#define G_T20_RECORDER_TASK_PRIO              2U

/* ============================================================================
 * 3. 하드웨어 핀 맵 (ESP32-S3 기반)
 * ========================================================================== */
// SPI1(FSPI) 인터페이스 구성
#define G_T20_PIN_SPI_SCK                     12
#define G_T20_PIN_SPI_MISO                    13
#define G_T20_PIN_SPI_MOSI                    11
#define G_T20_PIN_BMI_CS                      10
#define G_T20_PIN_BMI_INT1                    14

// [v214] 수동 제어 버튼 및 상태 LED (GPIO 0은 S3 Zero 보드 기본 버튼)
#define G_T20_PIN_CTRL_BUTTON                 0
#define G_T20_PIN_STATUS_LED                  21

/* ============================================================================
 * 4. 시스템 제한 및 버퍼 구성
 * ========================================================================== */
// 큐 길이는 지터 방지를 위해 v210 대비 상향 조정 검토 가능
inline constexpr uint8_t  G_T20_QUEUE_LEN             = 8U; 
inline constexpr uint16_t G_T20_SYSTEM_JSON_BUF_MAX   = 2048U; // API 정교화로 인한 확장

// [v214] SDMMC 1-bit 고정 설정
inline constexpr bool     G_T20_SDMMC_FORCE_1BIT      = true;

// 로깅 배치 설정
#define G_T20_RECORDER_BATCH_FLUSH_RECORDS    32U
#define G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS 2000U
#define G_T20_RECORDER_MAX_ROTATE_LIST        16U

/* ============================================================================
 * 5. 공통 열거형 (Enums) - 상태 및 모드 제어
 * ========================================================================== */

// 시스템 마스터 상태
typedef enum {
    EN_T20_STATE_IDLE = 0,      // 초기 상태
    EN_T20_STATE_READY,         // 하드웨어 초기화 완료
    EN_T20_STATE_RUNNING,       // 측정 및 분석 동작 중
    EN_T20_STATE_BUSY,          // 노이즈 학습 등 특정 작업 점유 중
    EN_T20_STATE_ERROR,         // 시스템 치명적 오류
    EN_T20_STATE_TIMEOUT,       // 통신/데이터 수집 시간 초과
    EN_T20_STATE_MAINTENANCE    // 펌웨어 업데이트 등 대기 모드
} EM_T20_State_t;

// [v214] 측정 시작 모드 선택
typedef enum {
    EN_T20_START_AUTO = 0,      // 부팅 즉시 활성화
    EN_T20_START_MANUAL         // 버튼/Web API 대기
} EM_T20_StartMode_t;

// 결과 리포트
typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

/* ============================================================================
 * 6. 시스템 전역 설정 구조체 (Config Group)
 * ========================================================================== */
typedef struct {
    EM_T20_StartMode_t start_mode;     // 시작 시 자동 실행 여부
    uint8_t            button_pin;     // 제어 버튼 핀 번호
    bool               sd_1bit_mode;   // SDMMC 1-bit 강제 여부
    uint16_t           watchdog_ms;    // 데이터 흐름 감시 주기
} ST_T20_SystemConfig_t;

/* ============================================================================
 * 7. 유틸리티 헬퍼 (Utility Inlines)
 * ========================================================================== */
/**
 * @brief 시스템 상태를 문자열로 변환 (Web API/Serial 출력용)
 */
static inline const char* T20_StateToString(EM_T20_State_t s) {
    switch (s) {
        case EN_T20_STATE_IDLE:    return "IDLE";
        case EN_T20_STATE_READY:   return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_BUSY:    return "BUSY";
        case EN_T20_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}

static inline float T20_Clamp(float v, float min, float max) {
    return (v < min) ? min : (v > max) ? max : v;
}
