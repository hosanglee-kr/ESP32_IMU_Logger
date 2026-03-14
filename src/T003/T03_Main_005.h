/**
 * @file T03_Main_005.h
 * @brief 동적 전력 관리를 포함한 메인 실행부
 */
#include <Arduino.h>
#include "T03_SDMMC_005.h"
#include "T03_BM217_005.h"

SDMMCHandler sd;
BMI270Handler imu;
BMI270_Options options;

void B10_init() {

    // 초기 장치 시작
    if (sd.begin()) {
        sd.loadConfig(options);
        sd.createLogFile(options.logPrefix, "Time,Ax,Ay,Az");
    }
    
    imu.begin(options, &sd);
}

void B10_run() {
    // 1. 센서 데이터 읽기 및 저장 (Active 상태 업무)
    // [데이터 처리 코드 생략]

    // 2. 정지 상태 모니터링
    if (options.dynamicPowerSave) {
        imu.checkMotionStatus(); 
    }
    
    vTaskDelay(pdMS_TO_TICKS(5)); // CPU 부하 조절
}

