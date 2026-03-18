/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_014.h
 * 모듈약어 : A10 (Main)
 * 모듈명 : Multi-Core Task Orchestrator & System Life-cycle
 * ------------------------------------------------------
 * 기능 요약
 * - Core 1: 고속 센서 데이터 처리 및 VQF Fusion (우선순위 5)
 * - Core 0: SD_MMC 비동기 쓰기(우선순위 3) 및 디버깅(우선순위 1)
 * - 세마포어를 이용한 인터럽트 구동 방식(Event-driven) 설계
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [A10] 하위 모듈(SB10, SD10)의 인스턴스를 소유하고 초기화 순서 제어
 * - 듀얼 코어 부하 분산 및 태스크 핸들 관리를 통한 전력 모드 전환 감독
 * ------------------------------------------------------
 * [A10 Tuning Guide]
 * 1. Task Priorities: Sensor(5) > Logging(3) > Debug(1). 절대 순위 유지 권장.
 * 2. Stack Size: VQF 연산 및 JSON 처리를 위해 5120(5KB) 이상 할당.
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include "C10_Config_014.h"
#include "SD10_SDMMC_014.h"
#include "SB10_BMI270_014.h"

// 전역 싱글톤 인스턴스 및 핸들
CL_SD10_SDMMC_Handler   g_A10_SdMMC;
CL_SB10_BMI270_Handler  g_A10_Imu;
ST_BMI270_Options_t     g_A10_ImuOptions;
QueueHandle_t           g_A10_Que_Debug = NULL;
SemaphoreHandle_t       g_SB10_Sem_FIFO = NULL;

// 전력 관리 제어를 위한 태스크 핸들
TaskHandle_t            g_A10_TaskHandle_Sensor  = NULL;
TaskHandle_t            g_A10_TaskHandle_Logging = NULL;

// [Core 1] 실시간 센서 처리 태스크
void A10_sensorTask(void* pv) {
    ST_FullSensorPayload_t v_payload;
    Serial.println(F("I: Sensor Task Started on Core 1"));
    
    for (;;) {
        // BMI270 FIFO 워터마크 인터럽트 대기
        if (xSemaphoreTake(g_SB10_Sem_FIFO, portMAX_DELAY) == pdTRUE) {
            // 데이터 추출 및 융합 연산
            g_A10_Imu.updateProcess(v_payload);
            
            // 모션 상태 체크 (정지 시 슬립 진입 로직 포함)
            if (g_A10_ImuOptions.dynamicPowerSave) {
                g_A10_Imu.checkMotionStatus();
            }
            
            // 디버그 큐 전송 (Non-blocking)
            xQueueSend(g_A10_Que_Debug, &v_payload, 0);
        }
    }
}

// [Core 0] 비결정적 I/O 처리 태스크
void A10_loggingTask(void* pv) {
    Serial.println(F("I: Logging Task Started on Core 0"));
    for (;;) {
        // SD 카드 쓰기 작업 실행 (내부에서 세마포어 대기)
        g_A10_SdMMC.processWrite();
    }
}

// [Core 0] 시스템 모니터링 태스크
void A10_debugTask(void* pv) {
    ST_FullSensorPayload_t v_data;
    for (;;) {
        if (xQueueReceive(g_A10_Que_Debug, &v_data, portMAX_DELAY)) {
            // 시리얼 플로터 또는 모니터 출력
            Serial.printf("[v014] R:%.1f P:%.1f Y:%.1f Steps:%u Mot:%d\n", 
                          v_data.euler[0], v_data.euler[1], v_data.euler[2], 
                          v_data.stepCount, v_data.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz 출력
    }
}

void A10_init() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n=== IMU Logger v014 Booting ==="));

    // 1. 동기화 객체 생성
    if (g_SB10_Sem_FIFO == NULL) g_SB10_Sem_FIFO = xSemaphoreCreateBinary();
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // 2. 하드웨어 모듈 초기화
    if (g_A10_SdMMC.begin()) {
        g_A10_SdMMC.loadConfig(g_A10_ImuOptions); // SD에서 설정 로드
        if (g_A10_ImuOptions.useSD) {
            g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, 
                                      "Time,Lx,Ly,Lz,QW,QX,QY,QZ,R,P,Y,Steps,Mot");
        }
    } else {
        Serial.println(F("W: Running without SD Card"));
    }

    if (!g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC)) {
        Serial.println(F("E: IMU Initialization Failed! System Halted."));
        while(1) { delay(1000); }
    }

    // 3. 멀티태스킹 분배 (ESP32-S3 Optimized)
    xTaskCreatePinnedToCore(
        A10_sensorTask, "SensorTask", 5120, NULL, 5, &g_A10_TaskHandle_Sensor, 1
    );
    
    xTaskCreatePinnedToCore(
        A10_loggingTask, "LoggingTask", 5120, NULL, 3, &g_A10_TaskHandle_Logging, 0
    );
    
    xTaskCreatePinnedToCore(
        A10_debugTask, "DebugTask", 3072, NULL, 1, NULL, 0
    );

    Serial.println(F("I: All Systems Green."));
}

void A10_run() { 
    // Arduino Loop 태스크는 종료하고 FreeRTOS 스케줄러에 전권 위임
    vTaskDelete(NULL); 
}
