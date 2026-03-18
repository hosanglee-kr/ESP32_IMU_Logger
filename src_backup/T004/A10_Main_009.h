/*
 * ------------------------------------------------------
 * 소스명 : A10_Main_009.h
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
#include "C10_Config_009.h"
#include "SD10_SDMMC_009.h"
#include "SB10_BM217_010.h"

// 공유 자원 및 핸들러
CL_SD10_SDMMC_Handler 	g_A10_SdMMC;
CL_SB10_BMI270_Handler 	g_A10_Imu;
ST_BMI270_Options_t 	g_A10_ImuOptions;

// RTOS 핸들러
QueueHandle_t 			g_A10_Que_SD;
QueueHandle_t 			g_A10_Que_Debug;
TaskHandle_t 			g_A10_TaskHandle_Sensor;

SemaphoreHandle_t 		g_SB10_Sem_FIFO = NULL; // 실제 선언

// [Task 1] 센서 처리 태스크 (Core 1)
void A10_sensorTask(void* pv) {
    ST_FullSensorPayload_t v_payload;
    for (;;) {
        // 하드웨어 인터럽트(세마포어)가 올 때까지 무한 대기 (CPU 점유율 0%)
        if (xSemaphoreTake(g_SB10_Sem_FIFO, portMAX_DELAY) == pdTRUE) {
            g_A10_Imu.updateProcess(v_payload);

            if (g_A10_ImuOptions.dynamicPowerSave) g_A10_Imu.checkMotionStatus();
            if (g_A10_Imu.shouldRecord()) xQueueSend(g_A10_Que_SD, &v_payload, 0);
            xQueueSend(g_A10_Que_Debug, &v_payload, 0);
        }
    }
}



// [Task 2] SD 로깅 태스크 (Core 0 - I/O 지연 처리용)
// [Task 2] SD 로깅 태스크 (성능 최적화 버전)
void A10_loggingTask(void* pv) {
    ST_FullSensorPayload_t v_sensor_data;
    int v_flushCounter = 0;

    // 파일 핸들을 루프 밖에서 한 번만 오픈하여 성능 향상
    File v_file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);

	if (!v_file) {
		Serial.println("!!! SD: Failed to open log file - Task Terminated");
		vTaskDelete(NULL); // 태스크를 안전하게 종료하여 시스템 크래시 방지
		return;
	}

    for (;;) {
        if (xQueueReceive(g_A10_Que_SD, &v_sensor_data, portMAX_DELAY)) {
            if (g_A10_ImuOptions.useSD && v_file) {
                v_file.printf("%lu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%lu,%d\n",
                    v_sensor_data.timestamp	, 
                    v_sensor_data.rawAcc[0]	, v_sensor_data.rawAcc[1]	, v_sensor_data.rawAcc[2],
                    v_sensor_data.linAcc[0]	, v_sensor_data.linAcc[1]	, v_sensor_data.linAcc[2],
                    v_sensor_data.gyro[0]	, v_sensor_data.gyro[1]	, v_sensor_data.gyro[2]	,
                    v_sensor_data.quat[0]	, v_sensor_data.quat[1]	, v_sensor_data.quat[2]	, v_sensor_data.quat[3],
                    v_sensor_data.euler[0]	, v_sensor_data.euler[1], v_sensor_data.euler[2],
                    v_sensor_data.stepCount	, v_sensor_data.motion);

                // 20개 레코드마다 실제 SD 쓰기 수행 (지연 최소화)
                if (++v_flushCounter >= 20) {
                    v_file.flush();
                    v_flushCounter = 0;
                }
            }
        }
    }

	// 태스크 종료 시(정상적으론 미발생) 파일 닫기
    if (v_file) v_file.close();

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
            g_A10_SdMMC.createLogFile(g_A10_ImuOptions.logPrefix, "Time,Ax,Ay,Az,Lx,Ly,Lz,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Sig");
        }
    }

    // 2. 센서 초기화
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);

    // 3. RTOS Queue 생성
    g_A10_Que_SD = xQueueCreate(C10_Config::QUEUE_LEN_SD, sizeof(ST_FullSensorPayload_t));
    g_A10_Que_Debug = xQueueCreate(C10_Config::QUEUE_LEN_DEBUG, sizeof(ST_FullSensorPayload_t));

    // 4. RTOS 태스크 할당 (멀티코어 활용)
    xTaskCreatePinnedToCore(A10_sensorTask	, "A10_sensorTask"	, C10_Config::TASK_STACK_SIZE	, NULL, 3, &g_A10_TaskHandle_Sensor	, 1);
    xTaskCreatePinnedToCore(A10_loggingTask	, "A10_loggingTask"	, C10_Config::TASK_STACK_SIZE	, NULL, 1, NULL						, 0);
    xTaskCreatePinnedToCore(A10_debugTask	, "A10_debugTask"	, 2048							, NULL, 0, NULL						, 0);
}

void A10_run() {
    // RTOS 사용으로 루프 비움
    vTaskDelete(NULL);
}

