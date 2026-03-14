// T03_Config_001.h

#pragma once
#include <Arduino.h>

namespace Config {
    // BMI270 SPI Pins (ESP32 Standard or Custom)
    static constexpr int BMI_CS    = 10;
    static constexpr int BMI_INT1  = 9;
    static constexpr int BMI_SCLK  = 18;
    static constexpr int BMI_MISO  = 19;
    static constexpr int BMI_MOSI  = 23;

    // SDMMC 1-bit Mode Default Pins (ESP32)
    // CLK: 14, CMD: 15, D0: 2 (기본핀 사용 시 별도 지정 불필요)
    static constexpr const char* SD_MOUNT_POINT = "/sdcard";
    
    // System Constants
    static constexpr float SAMPLE_RATE = 200.0f;
    static constexpr uint16_t FIFO_WTM  = 100; // FIFO Watermark
    static constexpr size_t SD_QUEUE_LEN = 500;
}


