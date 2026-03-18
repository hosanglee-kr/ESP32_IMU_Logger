/*
 * ------------------------------------------------------
 * 소스명 : T03_Main_007.h
 * 모듈약어 : T03 (Main)
 * 모듈명 : Multi-Tasking Sensor Fusion & Logging System
 * ------------------------------------------------------
 * 기능 요약
 * - SensorTask: 200Hz 데이터 획득 및 VQF 처리 (최고 우선순위)
 * - LoggingTask: Queue로부터 데이터를 받아 SD 카드 비동기 기록
 * - DebugTask: 시스템 상태 및 센서 데이터 시리얼 출력
 * - 전력 관리: 모션 상태에 따른 태스크 제어 및 슬립 진입
 * ------------------------------------------------------
 */
#include <Arduino.h>
#include "T03_Config_007.h"
#include "T03_SDMMC_007.h"
#include "T03_BM217_007.h"

// 공유 자원 및 핸들러
SDMMCHandler sd;
BMI270Handler imu;
BMI270_Options opts;

// RTOS 핸들러
QueueHandle_t sdQueue;
QueueHandle_t debugQueue;
TaskHandle_t sensorTaskHandle;

// [Task 1] 센서 처리 태스크 (Core 1)
void sensorTask(void* pv) {
    FullSensorPayload payload;
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    for (;;) {
        // 인터럽트 혹은 주기적 타이밍 대기 (200Hz)
        imu.updateProcess(payload);
        
        // 동작 상태 감지 (정지 시 슬립 진입 로직 포함)
        if (opts.dynamicPowerSave) imu.checkMotionStatus();

        // 데이터 전송 조건 확인 (Significant Motion 시에만 큐 전송)
        if (imu.shouldRecord()) {
            xQueueSend(sdQueue, &payload, 0);
        }
        xQueueSend(debugQueue, &payload, 0);

        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(5)); 
    }
}

// [Task 2] SD 로깅 태스크 (Core 0 - I/O 지연 처리용)
void loggingTask(void* pv) {
    FullSensorPayload data;
    while (1) {
        if (xQueueReceive(sdQueue, &data, portMAX_DELAY)) {
            if (opts.useSD) {
                File file = SD_MMC.open(sd.getPath(), FILE_APPEND);
                if (file) {
                    file.printf("%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%lu,%d\n",
                        data.timestamp, data.acc[0], data.acc[1], data.acc[2],
                        data.gyro[0], data.gyro[1], data.gyro[2],
                        data.quat[0], data.quat[1], data.quat[2], data.quat[3],
                        data.euler[0], data.euler[1], data.euler[2],
                        data.stepCount, data.motion);
                    file.close();
                }
            }
        }
    }
}

// [Task 3] 디버그 출력 태스크 (우선순위 낮음)
void debugTask(void* pv) {
    FullSensorPayload d;
    while (1) {
        if (xQueueReceive(debugQueue, &d, portMAX_DELAY)) {
            Serial.printf("[T03] Roll:%.1f Pitch:%.1f Yaw:%.1f Mot:%d\n", 
                          d.euler[0], d.euler[1], d.euler[2], d.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz 출력
    }
}

void B10_init() {
    
    // 1. SD 및 설정 로드
    if (sd.begin()) {
        sd.loadConfig(opts);
        if (opts.useSD) sd.createLogFile(opts.logPrefix, "Time,Ax,Ay,Az,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Sig");
    }

    // 2. 센서 초기화
    imu.begin(opts, &sd);

    // 3. RTOS Queue 생성
    sdQueue = xQueueCreate(Config::QUEUE_LEN_SD, sizeof(FullSensorPayload));
    debugQueue = xQueueCreate(Config::QUEUE_LEN_DEBUG, sizeof(FullSensorPayload));

    // 4. RTOS 태스크 할당 (멀티코어 활용)
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", Config::TASK_STACK_SIZE, NULL, 3, &sensorTaskHandle, 1);
    xTaskCreatePinnedToCore(loggingTask, "LogTask", Config::TASK_STACK_SIZE, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(debugTask, "DebugTask", 2048, NULL, 0, NULL, 0);
}

void B10_run() {
    // RTOS 사용으로 루프 비움
    vTaskDelete(NULL);
}

