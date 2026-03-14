// T03_Main_001.h

#include <Arduino.h>
#include "T03_SDMMC_001.h"
#include "T03_BM217_001.h"

SDMMCHandler sdLogger;
BMI270Handler imuSystem;

void B10_init() {
    Serial.begin(115200);

    // 1. SD 로거 시작
    if (sdLogger.begin()) {
        sdLogger.createLogFile("/sensor_data.csv", "Time,Ax,Ay,Az,Roll,Pitch,Yaw,Steps");
    }

    // 2. IMU 옵션 및 시작
    BMI270_Options opts;
    opts.useVQF = true;
    opts.useSD = true;
    opts.useStepCounter = true;

    if (!imuSystem.begin(opts, &sdLogger)) {
        Serial.println("System Initialization Failed!");
        while(1) delay(10);
    }
    Serial.println("System Ready.");
}

void B10_run() {
    FullSensorPayload live;
    // Core 1에서 생산된 최신 데이터를 가져와 시리얼 플로팅
    if (imuSystem.getLatest(&live)) {
        static uint32_t lastDisp = 0;
        if (millis() - lastDisp > 100) {
            Serial.printf("R:%.1f P:%.1f Y:%.1f Steps:%u\n", 
                          live.rpy[0], live.rpy[1], live.rpy[2], live.stepCount);
            lastDisp = millis();
        }
    }
}


