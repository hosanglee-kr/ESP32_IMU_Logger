#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "T03_Config_002.h"

class SDMMCHandler {
public:
    SDMMCHandler() : _queue(NULL) {}

    bool begin() {
        if (!SD_MMC.begin(Config::SD_MOUNT, true)) return false;
        _queue = xQueueCreate(Config::SD_QUEUE_LEN, 64);
        return (_queue != NULL);
    }

    // [복구] 메인에서 호출하는 로그 파일 초기 생성 함수
    bool createLogFile(const char* path, const char* header) {
        File file = SD_MMC.open(path, FILE_WRITE);
        if (file) {
            file.println(header);
            file.close();
            _currentPath = String(path);
            return true;
        }
        return false;
    }

    void loadConfig(BMI270_Options& opts) {
        File file = SD_MMC.open(Config::CONFIG_PATH);
        if (!file) return;
        JsonDocument doc;
        if (deserializeJson(doc, file) == DeserializationError::Ok) {
            opts.useVQF = doc["useVQF"] | opts.useVQF;
            opts.useSD = doc["useSD"] | opts.useSD;
            opts.autoCalibrate = doc["autoCalibrate"] | opts.autoCalibrate;
            if (doc["logPrefix"]) strlcpy(opts.logPrefix, doc["logPrefix"], sizeof(opts.logPrefix));
        }
        file.close();
    }

    String getNextFileName(const char* prefix) {
        int index = 1;
        char filename[32];
        while (index < 1000) {
            snprintf(filename, sizeof(filename), "/%s_%03d.csv", prefix, index);
            if (!SD_MMC.exists(filename)) break;
            index++;
        }
        return String(filename);
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
