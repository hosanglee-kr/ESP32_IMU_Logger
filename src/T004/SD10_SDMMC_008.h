/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_007.h
 * 모듈약어 : SD10 (SDMMC)
 * 모듈명 : SD_MMC Interface & JSON Config Manager
 * ------------------------------------------------------
 * 기능 요약
 * - SD_MMC 버스 제어 및 저전력 모드 대응 (Mount/Unmount)
 * - ArduinoJson V7.4.x를 이용한 설정 파일 입출력
 * - 파일 인덱싱을 통한 데이터 로테이션 구현
 * ------------------------------------------------------
 */
#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "C10_Config_008.h"

extern TaskHandle_t 			g_A10_Que_Sensor;

class CL_SD10_SDMMC_Handler {
public:
    bool begin() {
        return SD_MMC.begin(C10_Config::SD_MOUNT, true);
    }

    void end() {
        SD_MMC.end();
    }

    void loadConfig(ST_BMI270_Options_t& opts) {
        File file = SD_MMC.open(C10_Config::CONFIG_PATH);
        if (!file) return;
        JsonDocument doc;
        if (deserializeJson(doc, file) == DeserializationError::Ok) {
            opts.useVQF = doc["useVQF"] | opts.useVQF;
            opts.useSD = doc["useSD"] | opts.useSD;
            opts.dynamicPowerSave = doc["dynamicPowerSave"] | opts.dynamicPowerSave;
            opts.recordOnlySignificant = doc["recordOnlySignificant"] | opts.recordOnlySignificant;
            if (doc["logPrefix"]) strlcpy(opts.logPrefix, doc["logPrefix"], sizeof(opts.logPrefix));
        }
        file.close();
    }

    bool createLogFile(const char* prefix, const char* header) {
        int index = 1;
        char filename[32];
        while (index < 1000) {
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

