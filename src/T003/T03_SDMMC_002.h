#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "T03_Config_002.h"

class SDMMCHandler {
public:
    bool begin() {
        if (!SD_MMC.begin(Config::SD_MOUNT, true)) return false;
        _queue = xQueueCreate(Config::SD_QUEUE_LEN, 64);
        return (_queue != NULL);
    }

    // [신규] JSON 설정 로드 (ArduinoJson 7.4.x)
    void loadConfig(BMI270_Options& opts) {
        File file = SD_MMC.open(Config::CONFIG_PATH);
        if (!file) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        if (!error) {
            opts.useVQF = doc["useVQF"] | opts.useVQF;
            opts.useSD = doc["useSD"] | opts.useSD;
            opts.autoCalibrate = doc["autoCalibrate"] | opts.autoCalibrate;
            if (doc["logPrefix"]) strlcpy(opts.logPrefix, doc["logPrefix"], sizeof(opts.logPrefix));
        }
        file.close();
    }

    // [신규] 파일 인덱싱 (log_001.csv, log_002.csv...)
    String getNextFileName(const char* prefix) {
        int index = 1;
        char filename[32];
        while (index < 1000) {
            snprintf(filename, sizeof(filename), "/%s_%03d.csv", prefix, index);
            if (!SD_MMC.exists(filename)) break;
            index++;
        }
        _currentPath = String(filename);
        return _currentPath;
    }

    void startLogging(void (*worker)(void*), void* arg) {
        xTaskCreatePinnedToCore(worker, "SD_Worker", 8192, arg, 1, NULL, 0);
    }

    QueueHandle_t getQueue() { return _queue; }
    String getPath() { return _currentPath; }

private:
    QueueHandle_t _queue;
    String _currentPath;
};
