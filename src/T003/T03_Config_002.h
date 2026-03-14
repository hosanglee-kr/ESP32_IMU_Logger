#pragma once
#include <Arduino.h>

namespace Config {
    static constexpr int BMI_CS    = 10;
    static constexpr int BMI_INT1  = 9;
    static constexpr const char* SD_MOUNT = "/sdcard";
    static constexpr const char* CONFIG_PATH = "/config.json";

    static constexpr float SAMPLE_RATE = 200.0f;
    static constexpr uint16_t FIFO_WTM  = 100;
    static constexpr size_t SD_QUEUE_LEN = 512;
    
    // 캘리브레이션 설정
    static constexpr int CALIB_SAMPLES = 400; // 약 2초간 (200Hz 기준)
}

struct BMI270_Options {
    bool useVQF = true;
    bool useSD = true;
    bool useStepCounter = true;
    bool useGestures = true;
    bool useAnyMotion = true;
    bool autoCalibrate = true; // [신규] 시작 시 자동 영점 조정
    char logPrefix[16] = "log"; // [신규] 파일 이름 접두사
};


