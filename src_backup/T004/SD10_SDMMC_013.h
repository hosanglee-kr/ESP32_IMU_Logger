/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_013.h
 * 모듈약어 : SD10 (SDMMC)
 * 모듈명 : SD_MMC 1-bit Interface & File Management
 * ------------------------------------------------------
 * 기능 요약
 * - ESP32-S3 하드웨어 SD_MMC 호스트 사용 (1-bit Mode)
 * - POSIX 스타일 파일 시스템 (FS.h) 기반 로깅
 * - 더블 버퍼링(Double Buffering)을 통한 쓰기 지연 격리
 * - JSON 설정 파일 로드 및 파일 인덱싱 시스템
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <ArduinoJson.h>
#include "C10_Config_013.h"

#define SD10_BUF_SIZE 4096 // 4KB

struct ST_SD10_LogBuffer {
    char data[SD10_BUF_SIZE];
    uint16_t pos = 0;
};

class CL_SD10_SDMMC_Handler {
private:
    File _logFile;
    SemaphoreHandle_t _sem_sd_write;
    ST_SD10_LogBuffer _buffers[2];
    volatile uint8_t _curIdx = 0;
    volatile uint8_t _lastWriteIdx = 0;
    volatile uint16_t _lastWriteLen = 0;
    bool _isReady = false;

public:
    CL_SD10_SDMMC_Handler() {
        _sem_sd_write = xSemaphoreCreateBinary();
    }

    bool begin() {
        // SD_MMC 1-bit 모드 설정
        if (!SD_MMC.setPins(C10_Config::SD_MMC_CLK, C10_Config::SD_MMC_CMD, C10_Config::SD_MMC_D0)) {
            Serial.println("F: SD_MMC Pin Set Fail");
            return false;
        }

        // 1-bit mode, frequency 20MHz (S3 안정성 확보)
        if (!SD_MMC.begin("/sdcard", true, false, 20000)) {
            Serial.println("F: SD_MMC Mount Fail");
            return false;
        }
        
        _isReady = true;
        return true;
    }

    void loadConfig(ST_BMI270_Options_t& p_opts) {
        if (!_isReady) return;
        File v_file = SD_MMC.open(C10_Config::CONFIG_PATH, FILE_READ);
        if (!v_file) return;

        JsonDocument v_jsonDoc;
        if (deserializeJson(v_jsonDoc, v_file) == DeserializationError::Ok) {
            p_opts.useVQF = v_jsonDoc["useVQF"] | p_opts.useVQF;
            p_opts.useSD = v_jsonDoc["useSD"] | p_opts.useSD;
            if (v_jsonDoc["logPrefix"]) strlcpy(p_opts.logPrefix, v_jsonDoc["logPrefix"], sizeof(p_opts.logPrefix));
        }
        v_file.close();
    }

    bool createLogFile(const char* p_prefix, const char* p_header) {
        if (!_isReady) return false;
        
        int v_index = 1;
        char v_filename[32];
        while (v_index < 1000) {
            snprintf(v_filename, sizeof(v_filename), "/%s_%03d.csv", p_prefix, v_index);
            if (!SD_MMC.exists(v_filename)) break;
            v_index++;
        }

        _logFile = SD_MMC.open(v_filename, FILE_WRITE);
        if (!_logFile) return false;

        _logFile.println(p_header);
        return true;
    }

    void logToBuffer(const char* p_str) {
        size_t v_len = strlen(p_str);
        ST_SD10_LogBuffer &v_active = _buffers[_curIdx];

        if (v_active.pos + v_len >= SD10_BUF_SIZE) {
            uint8_t v_oldIdx = _curIdx;
            _curIdx = (_curIdx + 1) % 2;
            _buffers[_curIdx].pos = 0;

            _lastWriteLen = v_active.pos;
            _lastWriteIdx = v_oldIdx;
            xSemaphoreGive(_sem_sd_write);
        }

        memcpy(&_buffers[_curIdx].data[_buffers[_curIdx].pos], p_str, v_len);
        _buffers[_curIdx].pos += v_len;
    }

    void processWrite() {
        if (xSemaphoreTake(_sem_sd_write, portMAX_DELAY) == pdTRUE) {
            if (_logFile) {
                _logFile.write((const uint8_t*)_buffers[_lastWriteIdx].data, _lastWriteLen);
                
                // 1-bit 모드에서의 성능을 위해 flush는 간헐적으로 수행
                static uint32_t v_lastSync = 0;
                if (millis() - v_lastSync > 3000) {
                    _logFile.flush();
                    v_lastSync = millis();
                }
            }
        }
    }

    void end() {
        if (_logFile) {
            _logFile.close();
        }
        SD_MMC.end();
        _isReady = false;
    }
};


