/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_010.h
 * 모듈약어 : A10 (Main)
 * 모듈명 : Multi-Tasking Sensor Fusion & Logging System
 * ------------------------------------------------------
 * 기능 요약
 * - [성능] Core 1(Sensor): 200Hz 데이터 획득, VQF 필터링, 메모리 버퍼링 (Zero-copy 지향)
 * - [성능] Core 0(Storage): SdFat 기반 더블 버퍼링 기록, Write Latency 완벽 격리
 * - [전력] 모션 상태 기반 태스크 서스펜드 및 ESP32-S3 Light Sleep 진입/복귀 제어
 * - [안정성] Pre-allocation(파일 미리 할당) 적용으로 SD카드 쓰기 스파이크(Spike) 제거
 * - [통신] Binary Semaphore 기반 인터럽트 동기화로 불필요한 CPU 폴링 방지
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
#include "C10_Config_009.h"
#include "SD10_SDMMC_010.h"
#include "SB10_BM217_011.h"

// 공유 자원 및 핸들러
CL_SD10_SDMMC_Handler 	g_A10_SdMMC;
CL_SB10_BMI270_Handler 	g_A10_Imu;
ST_BMI270_Options_t 	g_A10_ImuOptions;

// RTOS 핸들러
// 삭제 예정 QueueHandle_t 			g_A10_Que_SD;
QueueHandle_t 			g_A10_Que_Debug;
TaskHandle_t 			g_A10_TaskHandle_Sensor;

SemaphoreHandle_t 		g_SB10_Sem_FIFO = NULL; // 실제 선언

void A10_sensorTask(void* pv) {
    ST_FullSensorPayload_t v_payload;
    for (;;) {
        if (xSemaphoreTake(g_SB10_Sem_FIFO, portMAX_DELAY) == pdTRUE) {
            // 1. BMI270 FIFO로부터 배치 데이터 읽기 및 VQF 연산 수행
            g_A10_Imu.updateProcess(v_payload);

            // 2. 모션 감지 및 전력 관리
            if (g_A10_ImuOptions.dynamicPowerSave) g_A10_Imu.checkMotionStatus();

            // 3. [변경] SD 로깅: 큐가 아닌 SB10 내부에서 sd->logToBuffer() 호출됨
            // SB10_BM217_011.h 내부 updateProcess 하단에서 이미 logToBuffer를 처리함

            // 4. 모니터링용 큐 전송 (디버그는 데이터가 누락되어도 무방하므로 큐 유지)
            xQueueSend(g_A10_Que_Debug, &v_payload, 0);
        }
    }
}


// [Task 2] SD 로깅 태스크 (Core 0 - I/O 지연 처리용)
// [Task 2] SD 로깅 태스크 (성능 최적화 버전)
void A10_loggingTask(void* pv) {

    // SD10_SDMMC_011.h에 구현된 processWrite 기능을 활용하도록 호출 구조 단순화
    for (;;) {
         // 내부적으로 세마포어를 기다리며 가득 찬 버퍼(4KB)를 SD에 기록
        g_A10_SdMMC.processWrite();
    }

}



// [Task 3] 디버그 출력 태스크 (우선순위 낮음)
void A10_debugTask(void* pv) {
    ST_FullSensorPayload_t v_sensor_data;
    while (1) {
        if (xQueueReceive(g_A10_Que_Debug, &v_sensor_data, portMAX_DELAY)) {
            Serial.printf("[T03] Roll:%.1f Pitch:%.1f Yaw:%.1f Mot:%d\n",
                          v_sensor_data.euler[0], v_sensor_data.euler[1], v_sensor_data.euler[2], v_sensor_data.motion);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz 출력
    }
}


void A10_init() {

	// 0. 인터럽트 발생 전 세마포어 핸들 정의 및 생성
    if (g_SB10_Sem_FIFO == NULL) {
        g_SB10_Sem_FIFO = xSemaphoreCreateBinary();
    }

    // 1. SD 및 설정 로드
    if (g_A10_SdMMC.begin()) {
        g_A10_SdMMC.loadConfig(g_A10_ImuOptions);
        if (g_A10_ImuOptions.useSD){
            g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Ax,Ay,Az,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,StepCnt,Motion");
            // g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Ax,Ay,Az,Lx,Ly,Lz,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Sig");
        }
    }

    // 2. 센서 초기화
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);

    // 3. RTOS Queue 생성
    // 삭제 예정 g_A10_Que_SD = xQueueCreate(C10_Config::QUEUE_LEN_SD, sizeof(ST_FullSensorPayload_t));
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // 4. RTO=S 태스크 할당 (멀티코어 활용)

    // SensorTask (Core 1, Priority 3): 가장 높은 우선순위로 데이터 유실 방지
    xTaskCreatePinnedToCore(A10_sensorTask, "SensorTask", 4096, NULL, 3, &g_A10_TaskHandle_Sensor, 1);

    // LoggingTask (Core 0, Priority 2): 쓰기 작업은 Core 0에서 전담
    xTaskCreatePinnedToCore(A10_loggingTask, "LoggingTask", 4096, NULL, 2, NULL, 0);

    // DebugTask (Core 0, Priority 1): 여유 시간에만 실행
    xTaskCreatePinnedToCore(A10_debugTask, "DebugTask", 2048, NULL, 1, NULL, 0);

}

void A10_run() {
    // RTOS 사용으로 루프 비움
    vTaskDelete(NULL);
}

