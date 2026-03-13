#pragma once

// T002_BMI270_Main_EampAll_005.h


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


