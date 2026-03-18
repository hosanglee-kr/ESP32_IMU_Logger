
/*
 * ------------------------------------------------------
 * 소스명 : T03_SDMMC_006.h
 * 모듈약어 : T03 (SDMMC)
 * 모듈명 : SD_MMC File System & Power Control Module
 * ------------------------------------------------------
 * 기능 요약
 * - SD_MMC 버스 초기화 및 인터페이스 제어
 * - 저전력 진입 시 파일 안전 종료 및 버스 마운트 해제 (Power Cut)
 * - JSON 기반 시스템 설정 파일(config.json) 로드 및 파싱
 * - 중복 없는 파일 인덱싱 기반 로그 생성 (File Rotation)
 * - 실시간 센서 데이터 기록을 위한 경로 관리
 * ------------------------------------------------------
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

    // [추가] JSON 설정 파일 로드 함수 (ArduinoJson 7.4.x 대응)
    void loadConfig(BMI270_Options& opts) {
        File file = SD_MMC.open(Config::CONFIG_PATH);
        if (!file) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        if (error == DeserializationError::Ok) {
            opts.useVQF = doc["useVQF"] | opts.useVQF;
            opts.useSD = doc["useSD"] | opts.useSD;
            opts.dynamicPowerSave = doc["dynamicPowerSave"] | opts.dynamicPowerSave;
            if (doc["logPrefix"]) strlcpy(opts.logPrefix, doc["logPrefix"], sizeof(opts.logPrefix));
        }
        file.close();
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


