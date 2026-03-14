/**
 * @file T03_SDMMC_005.h
 * @brief SD 카드 관리 및 파일 로테이션 (절전 기능 포함)
 */
#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>

class SDMMCHandler {
public:
    bool begin() {
        return SD_MMC.begin(Config::SD_MOUNT, true);
    }

    // [절전] SD 카드 마운트 해제 (전류 소모 차단)
    void end() {
        SD_MMC.end();
    }

    // [최적화] 인덱싱된 로그 파일 생성
    bool createLogFile(const char* prefix, const char* header) {
        int index = 1;
        char filename[32];
        while (index < 1000) { // 기존 파일 검색하여 새 인덱스 부여
            snprintf(filename, sizeof(filename), "/%s_%03d.csv", prefix, index);
            if (!SD_MMC.exists(filename)) break;
            index++;
        }
        _currentPath = String(filename);
        File file = SD_MMC.open(_currentPath, FILE_WRITE);
        if (file) {
            file.println(header);
            file.close();
            return true;
        }
        return false;
    }

    String getPath() { return _currentPath; }

private:
    String _currentPath;
};


