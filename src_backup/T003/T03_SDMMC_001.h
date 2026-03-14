// T03_SDMMC_001.h

#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include "T03_Config_001.h"

class SDMMCHandler {
public:
    SDMMCHandler() : _queue(NULL), _taskHandle(NULL) {}

    bool begin() {
        // SDMMC 1-bit 모드 시작
        if (!SD_MMC.begin(Config::SD_MOUNT_POINT, true)) {
            Serial.println("[-] SDMMC Mount Failed");
            return false;
        }
        
        // 데이터 전달용 큐 생성 (포인터가 아닌 구조체 복사 방식)
        _queue = xQueueCreate(Config::SD_QUEUE_LEN, 64); // FullSensorPayload 크기 넉넉히
        return (_queue != NULL);
    }

    bool createLogFile(const char* path, const char* header) {
        if (!SD_MMC.exists(path)) {
            File file = SD_MMC.open(path, FILE_WRITE);
            if (file) {
                file.println(header);
                file.close();
            }
        }
        _logPath = path;
        return true;
    }

    void startLogging(void (*worker)(void*), void* arg) {
        // Core 0에서 낮은 우선순위로 실행 (IDLE 태스크보다 높게)
        xTaskCreatePinnedToCore(worker, "SD_Worker", 8192, arg, 1, &_taskHandle, 0);
    }

    QueueHandle_t getQueue() { return _queue; }
    const char* getPath() { return _logPath.c_str(); }

private:
    QueueHandle_t _queue;
    TaskHandle_t _taskHandle;
    String _logPath;
};
