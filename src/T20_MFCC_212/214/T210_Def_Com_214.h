/* ============================================================================
 * File: T210_Def_Com_214.h
 * Summary: 시스템 전역 공통 정의 및 전역 상수 (v214 통합본)
 * Compiler: gnu++17 기준 최적화
 * ========================================================================== */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * [TO-DO LIST / 잔여 업무]
 * 1. [ ] EN_T20_STATE_DONE 상태가 모든 태스크 상태 머신에서 정상 작동하는지 검증
 * 2. [ ] G_T20_SYSTEM_JSON_BUF_MAX 크기가 복잡한 JSON 생성 시 충분한지 힙 모니터링
 * 3. [ ] inline constexpr 변수들이 gnu++17 환경에서 중복 정의 없이 링크되는지 확인
 * 4. [ ] 하드웨어 Watchdog 주기를 Batch Flush 시간(2s)보다 길게 설정 확인
 * ========================================================================== */

/* --- Version --- */
inline constexpr char const* G_T20_VERSION_STR = "T20_Mfcc_v214_Final";

/* ============================================================================
 * 1. 시스템 동작 타이밍 및 시뮬레이션
 * ========================================================================== */
inline constexpr uint16_t G_T20_RUNTIME_SIM_FRAME_INTERVAL_MS = 160U;
inline constexpr float    G_T20_RUNTIME_SIM_AMPLITUDE_DEFAULT = 0.20f;

/* ============================================================================
 * 2. RTOS 태스크 설정
 * ========================================================================== */
#define G_T20_SENSOR_TASK_STACK               6144U
#define G_T20_PROCESS_TASK_STACK              12288U
#define G_T20_RECORDER_TASK_STACK             8192U
#define G_T20_SENSOR_TASK_PRIO                4U
#define G_T20_PROCESS_TASK_PRIO               3U
#define G_T20_RECORDER_TASK_PRIO              2U

/* ============================================================================
 * 3. 하드웨어 핀 맵 (ESP32-S3)
 * ========================================================================== */
#define G_T20_PIN_SPI_SCK                     12
#define G_T20_PIN_SPI_MISO                    13
#define G_T20_PIN_SPI_MOSI                    11
#define G_T20_PIN_BMI_CS                      10
#define G_T20_PIN_BMI_INT1                    14

/* ============================================================================
 * 4. 공통 상태 및 결과 정의 (Enum)
 * ========================================================================== */
typedef enum {
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,     // 장치 준비 완료
    EN_T20_STATE_RUNNING,   // 태스크/루프 실행 중
    EN_T20_STATE_DONE,      // [핵심] 작업 정상 종료 (에러 해결용 복구)
    EN_T20_STATE_ERROR,     // 오류 발생
    EN_T20_STATE_BUSY,      // 작업 중 대기
    EN_T20_STATE_TIMEOUT    // 시간 초과
} EM_T20_State_t;

typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

/* ============================================================================
 * 5. 시스템 제한 및 버퍼 설정 (누락된 상수 대거 복구)
 * ========================================================================== */
#define G_T20_QUEUE_LEN                       8U
#define G_T20_SELECTION_SYNC_NAME_MAX         32U  // T221 에러 해결
#define G_T20_RECORDER_MAX_ROTATE_LIST        16U
#define G_T20_CFG_PROFILE_COUNT               4U   // T221/T230 에러 해결
#define G_T20_RUNTIME_CFG_PROFILE_NAME_MAX    32U  // T221 에러 해결
#define G_T20_RAW_FRAME_BUFFERS               4U   // T221 에러 해결
#define G_T20_SYSTEM_JSON_BUF_MAX             2048U
#define G_T20_RUNTIME_CFG_JSON_BUF_SIZE       2048U // T234 에러 해결

/* ============================================================================
 * 6. 수학적 상수 (DSP 최적화)
 * ========================================================================== */
inline constexpr float G_T20_PI      = 3.14159265358979323846f;
inline constexpr float G_T20_EPSILON = 1.0e-12f; // T231 로그 연산 에러 해결

/* ============================================================================
 * 7. 파일 시스템 경로 (Rec/Storage)
 * ========================================================================== */
inline constexpr char const* G_T20_RECORDER_DEFAULT_FILE_PATH  = "/t20_rec.bin";
inline constexpr char const* G_T20_RECORDER_INDEX_FILE_PATH    = "/t20_rec_index.json";
inline constexpr char const* G_T20_RECORDER_RUNTIME_CFG_PATH   = "/t20_runtime_cfg.json";
inline constexpr char const* G_T20_RECORDER_FALLBACK_PATH      = "/rec_fallback.bin";

/* ============================================================================
 * 8. 시스템 리미트 구조체 (T218 에러 해결)
 * ========================================================================== */
typedef struct {
    uint16_t noise_min_frames;
    uint16_t csv_page_size_max;
} ST_T20_SystemLimits_t;

// T218_Def_Main_214.h 에서 참조하는 실제 데이터
inline constexpr ST_T20_SystemLimits_t G_T20_SYSTEM_LIMITS = {
    .noise_min_frames = 8U,
    .csv_page_size_max = 100U
};

/* ============================================================================
 * 9. 디버그 및 유틸리티 헬퍼
 * ========================================================================== */
static inline const char* T20_StateToString(EM_T20_State_t s)
{
    switch (s) {
        case EN_T20_STATE_IDLE:    return "IDLE";
        case EN_T20_STATE_READY:   return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_DONE:    return "DONE";
        case EN_T20_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}
