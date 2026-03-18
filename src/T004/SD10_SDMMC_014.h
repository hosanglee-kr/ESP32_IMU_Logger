/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_014.h
 * 모듈약어 : SD10 (SDMMC)
 * 모듈명 : High-Performance SD_MMC & Buffer Manager
 * ------------------------------------------------------
 * 기능 요약
 * - ESP32-S3 하드웨어 SD_MMC(1-bit) 구동 및 DMA 활용
 * - 8KB 더블 버퍼링을 통한 SD 쓰기 지연(Latency) 완전 격리
 * - JSON 기반 설정 파일 로드 및 CSV 인덱싱 생성
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [SD10] 비결정적인 SD 쓰기 시간을 관리하고 센서 데이터 손실을 방지
 * - 파일 시스템의 마운트/해제 및 실제 물리적 I/O를 전담함
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "FS.h"
#include "C10_Config_014.h"

#define SD10_BUF_SIZE   8192  // 8KB (SD_MMC DMA 최적화)

struct ST_SD10_LogBuffer {
    char data[SD10_BUF_SIZE];
    uint16_t pos = 0;
};

class CL_SD10_SDMMC_Handler {
private:
    fs::File          _logFile;
    SemaphoreHandle_t _sem_sd_write;
    ST_SD10_LogBuffer _buffers[2];
    volatile uint8_t  _curIdx = 0;
    volatile uint8_t  _lastWriteIdx = 0;
    volatile uint16_t _lastWriteLen = 0;

public:
    CL_SD10_SDMMC_Handler() {
        _sem_sd_write = xSemaphoreCreateBinary();
    }

    bool begin() {
        // ESP32-S3 1-bit 모드 핀 할당
        if(!SD_MMC.setPins(C10_Config::SD_MMC_CLK, C10_Config::SD_MMC_CMD, C10_Config::SD_MMC_D0)){
            return false;
        }
        // 1-bit mode, 20MHz
        if (!SD_MMC.begin(C10_Config::SD_MOUNT, true, false, 20000)) {
            return false;
        }
        return true;
    }

    void loadConfig(ST_BMI270_Options_t& p_opts) {
        if(!SD_MMC.exists(C10_Config::CONFIG_PATH)) return;
        fs::File v_file = SD_MMC.open(C10_Config::CONFIG_PATH, FILE_READ);
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
            _lastWriteIdx = _curIdx;
            _lastWriteLen = v_active.pos;
            _curIdx = (_curIdx + 1) % 2;
            _buffers[_curIdx].pos = 0;
            xSemaphoreGive(_sem_sd_write);
        }
        memcpy(&_buffers[_curIdx].data[_buffers[_curIdx].pos], p_str, v_len);
        _buffers[_curIdx].pos += v_len;
    }

    void processWrite() {
        if (xSemaphoreTake(_sem_sd_write, portMAX_DELAY) == pdTRUE) {
            if (_logFile) {
                _logFile.write((const uint8_t*)_buffers[_lastWriteIdx].data, _lastWriteLen);
                static uint32_t v_lastSync = 0;
                if (millis() - v_lastSync > 5000) { // 5초마다 flush
                    _logFile.flush();
                    v_lastSync = millis();
                }
            }
        }
    }

    void end() {
        if (_logFile) _logFile.close();
        SD_MMC.end();
    }
};


