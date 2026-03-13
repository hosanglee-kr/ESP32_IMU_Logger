
// T002_BMI270_Main_EampAll_005.h

#ifndef B10_BMI270_MAIN_H
#define B10_BMI270_MAIN_H

#include "T002_BMI270_Handler_006.h"

// 객체 생성
BMI270Handler imuSystem;

void B10_init() {
    // 1. 상세 옵션 변수 설정
    BMI270_Options opts;
    opts.sampleRate     = 200.0f;
    opts.useVQF         = true;
    opts.useSD          = true;
    opts.useStepCounter = true;
    opts.useGestures    = true;
    opts.useAnyMotion   = true;
    opts.fifoThreshold  = 100; // 100바이트마다 인터럽트

    // 2. 시스템 시작 (CS=10, INT1=9)
    if (!imuSystem.begin(10, 9, opts)) {
        Serial.println("BMI270 Integrated System Init Failed!");
        while(1);
    }

    Serial.println("BMI270 Full Features System Ready.");
}

void B10_run() {
    FullSensorPayload liveData;

    // 비동기 큐에서 최신 데이터 하나를 꺼내 확인 (UI/모니터링용)
    if (imuSystem.getLatestData(&liveData)) {
        // 필요한 경우 시리얼 플로터 출력
        // Serial.printf("P:%.2f, R:%.2f, Y:%.2f\n", liveData.rpy[1], liveData.rpy[0], liveData.rpy[2]);
    }
}

#endif




// // main.cpp

// #include <Arduino.h>
// #include "T002_BMI270_Main_EampAll_005.h"

// void setup() {
//     Serial.begin(115200);

//     // BMI270 통합 시스템 초기화 (독립 h파일 내부 함수 호출)
//     B10_init();
// }

// void loop() {
//     // 시스템 실행 (비차단 방식)
//     B10_run();

//     // 메인 루프 지연은 시스템 전체 성능(FreeRTOS Task)에 영향을 주지 않음
//     delay(5);
// }
