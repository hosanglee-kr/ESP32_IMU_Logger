/*
 * ------------------------------------------------------
 * 소스명 : T03_Main_006.h
 * 모듈약어 : T03 (Main)
 * 모듈명 : Dynamic Power Management Loop & Task Controller
 * ------------------------------------------------------
 * 기능 요약
 * - 하드웨어 초기화 시퀀스 관리 (SD -> Config -> IMU)
 * - 메인 데이터 획득 및 전력 모니터링 루프 실행
 * - Any/Significant Motion 상태에 따른 동적 로깅 및 절전 제어
 * - RTOS 기반 태스크 딜레이 및 CPU 자원 최적화
 * ------------------------------------------------------
 */

 
#include <Arduino.h>
#include "T03_Config_006.h"
#include "T03_SDMMC_006.h"
#include "T03_BM217_006.h"

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

