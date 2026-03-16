/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_008.h
 * 모듈약어 : A10 (Main)
 * 모듈명 : Multi-Tasking Sensor Fusion & Logging System
 * ------------------------------------------------------
 * 기능 요약
 * - main.cpp 에서 include 및 A10_init, a10_run호출함
 * - A10_sensorTask: 200Hz 데이터 획득 및 VQF 처리 (최고 우선순위)
 * - A10_loggingTask: Queue로부터 데이터를 받아 SD 카드 비동기 기록
 * - A10_debugTask: 시스템 상태 및 센서 데이터 시리얼 출력
 * - 전력 관리: 모션 상태에 따른 태스크 제어 및 슬립 진입
 * ------------------------------------------------------
 */
 
#include <Arduino.h>
#include "C10_Config_008.h"
#include "SD10_SDMMC_008.h"
#include "SB10_BM217_008.h"

// 공유 자원 및 핸들러
CL_SD10_SDMMC_Handler 	g_A10_SdMMC;
CL_SB10_BMI270_Handler 	g_A10_Imu;
ST_BMI270_Options_t 	g_A10_ImuOptions;

// RTOS 핸들러
QueueHandle_t 			g_A10_Que_SD;
QueueHandle_t 			g_A10_Que_Debug;
TaskHandle_t 			g_A10_Que_Sensor;

// [Task 1] 센서 처리 태스크 (Core 1)
void A10_sensorTask(void* pv) {
    ST_FullSensorPayload_t payload;
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        // 인터럽트 혹은 주기적 타이밍 대기 (200Hz)
        g_A10_Imu.updateProcess(payload);

        // 동작 상태 감지 (정지 시 슬립 진입 로직 포함)
        if (g_A10_ImuOptions.dynamicPowerSave) g_A10_Imu.checkMotionStatus();

        // 데이터 전송 조건 확인 (Significant Motion 시에만 큐 전송)
        if (g_A10_Imu.shouldRecord()) {
            xQueueSend(g_A10_Que_SD, &payload, 0);
        }
        xQueueSend(g_A10_Que_Debug, &payload, 0);

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(5));
    }
}

// [Task 2] SD 로깅 태스크 (Core 0 - I/O 지연 처리용)
// [Task 2] SD 로깅 태스크 (성능 최적화 버전)
void A10_loggingTask(void* pv) {
    ST_FullSensorPayload_t data;
    int flushCounter = 0;

    // 파일 핸들을 루프 밖에서 한 번만 오픈하여 성능 향상
    File file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);

	if (!file) {
        Serial.println("!!! SD: Failed to open log file for appending");
    }


    for (;;) {
        if (xQueueReceive(g_A10_Que_SD, &data, portMAX_DELAY)) {
            if (g_A10_ImuOptions.useSD && file) {
                file.printf("%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%lu,%d\n",
                    data.timestamp, data.acc[0], data.acc[1], data.acc[2],
                    data.gyro[0], data.gyro[1], data.gyro[2],
                    data.quat[0], data.quat[1], data.quat[2], data.quat[3],
                    data.euler[0], data.euler[1], data.euler[2],
                    data.stepCount, data.motion);

                // 20개 레코드마다 실제 SD 쓰기 수행 (지연 최소화)
                if (++flushCounter >= 20) {
                    file.flush();
                    flushCounter = 0;
                }
            }
        }
    }

	// 태스크 종료 시(정상적으론 미발생) 파일 닫기
    if (file) file.close();

}



// [Task 3] 디버그 출력 태스크 (우선순위 낮음)
void A10_debugTask(void* pv) {
    ST_FullSensorPayload_t d;
    while (1) {
        if (xQueueReceive(g_A10_Que_Debug, &d, portMAX_DELAY)) {
            Serial.printf("[T03] Roll:%.1f Pitch:%.1f Yaw:%.1f Mot:%d\n",
                          d.euler[0], d.euler[1], d.euler[2], d.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz 출력
    }
}

void A10_init() {

    // 1. SD 및 설정 로드
    if (g_A10_SdMMC.begin()) {
        g_A10_SdMMC.loadConfig(g_A10_ImuOptions);
        if (g_A10_ImuOptions.useSD) g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Ax,Ay,Az,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Sig");
    }

    // 2. 센서 초기화
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);

    // 3. RTOS Queue 생성
    g_A10_Que_SD = xQueueCreate(C10_Config::QUEUE_LEN_SD, sizeof(ST_FullSensorPayload_t));
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // 4. RTOS 태스크 할당 (멀티코어 활용)
    xTaskCreatePinnedToCore(A10_sensorTask, "A10_sensorTask", C10_Config::TASK_STACK_SIZE, NULL, 3, &g_A10_Que_Sensor, 1);
    xTaskCreatePinnedToCore(A10_loggingTask, "A10_loggingTask", C10_Config::TASK_STACK_SIZE, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(A10_debugTask, "A10_debugTask", 2048, NULL, 0, NULL, 0);
}

void A10_run() {
    // RTOS 사용으로 루프 비움
    vTaskDelete(NULL);
}

