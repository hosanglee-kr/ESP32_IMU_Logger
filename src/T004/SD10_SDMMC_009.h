/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_009.h
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
#include "C10_Config_009.h"

extern TaskHandle_t 			g_A10_TaskHandle_Sensor;

class CL_SD10_SDMMC_Handler {
public:
    bool begin() {
        return SD_MMC.begin(C10_Config::SD_MOUNT, true);
    }

    void end() {
        SD_MMC.end();
    }

    void loadConfig(ST_BMI270_Options_t& p_opts) {
        File v_file = SD_MMC.open(C10_Config::CONFIG_PATH);
        if (!v_file) return;
        JsonDocument v_jsonDoc;
        if (deserializeJson(v_jsonDoc, v_file) == DeserializationError::Ok) {
            p_opts.useVQF 					= v_jsonDoc["useVQF"] | p_opts.useVQF;
            p_opts.useSD 					= v_jsonDoc["useSD"] | p_opts.useSD;
            p_opts.dynamicPowerSave 		= v_jsonDoc["dynamicPowerSave"] | p_opts.dynamicPowerSave;
            p_opts.recordOnlySignificant 	= v_jsonDoc["recordOnlySignificant"] | p_opts.recordOnlySignificant;
            if (v_jsonDoc["logPrefix"]) strlcpy(p_opts.logPrefix, v_jsonDoc["logPrefix"], sizeof(p_opts.logPrefix));
        }
        v_file.close();
    }

    bool createLogFile(const char* p_prefix, const char* p_header) {
        int v_index = 1;
        char v_filename[32];
        while (v_index < 1000) {
            snprintf(v_filename, sizeof(v_filename), "/%s_%03d.csv", p_prefix, v_index);
            if (!SD_MMC.exists(v_filename)) break;
            v_index++;
        }
        _currentPath = String(v_filename);
        File v_file = SD_MMC.open(_currentPath, FILE_WRITE);
        if (v_file) {
            v_file.println(p_header);
            v_file.close();
            return true;
        }
        return false;
    }

    String getPath() { return _currentPath; }

private:
    String _currentPath;
};

