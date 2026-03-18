/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_014.h
 * 모듈약어 : A10 (Main)
 * 모듈명 : Multi-Core Task Orchestrator
 * ------------------------------------------------------
 * 기능 요약
 * - Core 1: 센서 융합 및 실시간 데이터 버퍼링 (우선순위 최고)
 * - Core 0: SD_MMC 비동기 기록 및 디버깅 출력 (우선순위 분리)
 * - Binary Semaphore를 통한 인터럽트 동기화 관리
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [A10] 하위 모듈(SB10, SD10)의 생명주기를 관리하고 코어 간 부하 분산
 * - 전체 시스템의 초기화 순서를 보장하며 런타임 상태를 감독함
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include "C10_Config_014.h"
#include "SD10_SDMMC_014.h"
#include "SB10_BMI270_014.h"

CL_SD10_SDMMC_Handler   g_A10_SdMMC;
CL_SB10_BMI270_Handler  g_A10_Imu;
ST_BMI270_Options_t     g_A10_ImuOptions;
QueueHandle_t           g_A10_Que_Debug;
SemaphoreHandle_t       g_SB10_Sem_FIFO = NULL;

void A10_sensorTask(void* pv) {
    ST_FullSensorPayload_t v_payload;
    for (;;) {
        if (xSemaphoreTake(g_SB10_Sem_FIFO, portMAX_DELAY) == pdTRUE) {
            g_A10_Imu.updateProcess(v_payload);
            if (g_A10_ImuOptions.dynamicPowerSave) g_A10_Imu.checkMotionStatus();
            xQueueSend(g_A10_Que_Debug, &v_payload, 0);
        }
    }
}

void A10_loggingTask(void* pv) {
    for (;;) { g_A10_SdMMC.processWrite(); }
}

void A10_debugTask(void* pv) {
    ST_FullSensorPayload_t v_data;
    while (1) {
        if (xQueueReceive(g_A10_Que_Debug, &v_data, portMAX_DELAY)) {
            Serial.printf("[v014] R:%.1f P:%.1f Y:%.1f Mot:%d\n", v_data.euler[0], v_data.euler[1], v_data.euler[2], v_data.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void A10_init() {
    if (g_SB10_Sem_FIFO == NULL) g_SB10_Sem_FIFO = xSemaphoreCreateBinary();
    
    if (g_A10_SdMMC.begin()) {
        g_A10_SdMMC.loadConfig(g_A10_ImuOptions);
        if (g_A10_ImuOptions.useSD) {
            g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Lx,Ly,Lz,QW,QX,QY,QZ,Mot");
        }
    }
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // SensorTask는 연산 집약적이므로 Core 1 할당, 우선순위 5(최고)
    xTaskCreatePinnedToCore(A10_sensorTask, "SensorTask", 5120, NULL, 5, NULL, 1);
    
    // LoggingTask는 I/O 대기가 많으므로 Core 0 할당, 우선순위 3
    xTaskCreatePinnedToCore(A10_loggingTask, "LoggingTask", 5120, NULL, 3, NULL, 0);
    
    // DebugTask는 시스템 부하 시 자동 밀림, 우선순위 1
    xTaskCreatePinnedToCore(A10_debugTask, "DebugTask", 2560, NULL, 1, NULL, 0);


}

void A10_run() { 
    vTaskDelete(NULL);
}
