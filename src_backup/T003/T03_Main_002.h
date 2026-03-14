#include <Arduino.h>
#include "T03_SDMMC_002.h"
#include "T03_BM217_004.h"

SDMMCHandler sdLogger;
BMI270Handler imuSystem;

void B10_init() {
    Serial.begin(115200);

    if (sdLogger.begin()) {
        BMI270_Options opts;
        sdLogger.loadConfig(opts); // 1. JSON 설정 로드
        String logPath = sdLogger.getNextFileName(opts.logPrefix); // 2. 새 파일명 생성
        sdLogger.createLogFile(logPath.c_str(), "Time,Ax,Ay,Az,R,P,Y,Steps,Gest,Mot");
        
        if (!imuSystem.begin(opts, &sdLogger)) { // 3. 센서 & 캘리브레이션 시작
            Serial.println("IMU Init Failed!");
        }
    }
    Serial.println("System Integrated & Calibrated.");
}

void B10_run() {
    FullSensorPayload live;
    if (imuSystem.getLatest(&live)) {
        // 루프 작업 수행
    }
}

