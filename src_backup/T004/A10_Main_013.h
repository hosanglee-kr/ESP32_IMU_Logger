/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_013.h
 * 모듈약어 : A10 (Main)
 * 모듈명 : Dual-Core Optimized Logger with SD_MMC
 * ------------------------------------------------------
 * 기능 요약
 * - Core 1: 센서 데이터 획득, VQF 필터링 (High Priority)
 * - Core 0: SD_MMC 1-bit 기록 및 디버그 출력 (Storage IO)
 * - Binary Semaphore 기반의 인터럽트 동기화 (Event-driven)
 * - SD_MMC 교체를 통한 전력 효율 및 쓰기 안정성 향상
 * ------------------------------------------------------
 */

#include <Arduino.h>
#include "C10_Config_013.h"
#include "SD10_SDMMC_013.h"
#include "SB10_BM217_013.h"

CL_SD10_SDMMC_Handler 	g_A10_SdMMC;
CL_SB10_BMI270_Handler 	g_A10_Imu;
ST_BMI270_Options_t 	g_A10_ImuOptions;

QueueHandle_t 			g_A10_Que_Debug;
TaskHandle_t 			g_A10_TaskHandle_Sensor;
SemaphoreHandle_t 		g_SB10_Sem_FIFO = NULL;

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
    for (;;) {
        g_A10_SdMMC.processWrite();
    }
}

void A10_debugTask(void* pv) {
    ST_FullSensorPayload_t v_data;
    while (1) {
        if (xQueueReceive(g_A10_Que_Debug, &v_data, portMAX_DELAY)) {
            Serial.printf("[V13] R:%.1f P:%.1f Y:%.1f | Step:%u | Mot:%d\n",
                          v_data.euler[0], v_data.euler[1], v_data.euler[2], v_data.stepCount, v_data.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void A10_init() {
    Serial.begin(115200);
    
    if (g_SB10_Sem_FIFO == NULL) g_SB10_Sem_FIFO = xSemaphoreCreateBinary();

    // 1. SD_MMC 1-bit 시작
    if (g_A10_SdMMC.begin()) {
        g_A10_SdMMC.loadConfig(g_A10_ImuOptions);
        if (g_A10_ImuOptions.useSD) {
            g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Lx,Ly,Lz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Motion");
        }
    }

    // 2. BMI270 초기화
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);

    // 3. RTOS 리소스 생성 및 태스크 할당
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // Core 1: Sensor & Fusion (Priority 3)
    xTaskCreatePinnedToCore(A10_sensorTask, "SensorTask", 4096, NULL, 3, &g_A10_TaskHandle_Sensor, 1);
    
    // Core 0: Storage IO (Priority 2)
    xTaskCreatePinnedToCore(A10_loggingTask, "LoggingTask", 4096, NULL, 2, NULL, 0);
    
    // Core 0: Debug UI (Priority 1)
    xTaskCreatePinnedToCore(A10_debugTask, "DebugTask", 2048, NULL, 1, NULL, 0);
}

void A10_run() {
    vTaskDelete(NULL);
}


