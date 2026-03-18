/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_014.h
 * 모듈약어 : SD10 (SDMMC)
 * 모듈명 : High-Performance SD_MMC & Buffer Manager
 * ------------------------------------------------------
 * 기능 요약
 * - ESP32-S3 하드웨어 SD_MMC(1-bit) 구동 및 DMA 활용 고속 기록
 * - 8KB 더블 버퍼링을 통한 SD 쓰기 지연(Latency Spike) 완전 격리
 * - 시스템 슬립 전 잔여 데이터 강제 기록(Flush) 및 세션 관리
 * ------------------------------------------------------
 * 모듈별 책임 정리
 * - [SD10] 비결정적인 SD 쓰기 시간을 관리하고 센서 데이터 손실 방지
 * - 파일 시스템 관리(Mount/Unmount) 및 물리적 I/O 추상화 전담
 * ------------------------------------------------------
 * [SD10 Tuning Guide]
 * 1. SD10_BUF_SIZE: 반드시 512의 배수(섹터 크기). 클수록 SD 내부 컨트롤러 부하 감소.
 * 2. SD10_FLASH_INTERVAL: 전력 최적화를 위해 10초 이상 권장 (현재 10s).
 * 3. SD_MMC_FREQ: 배선이 길면 20000(20MHz), 짧고 안정적이면 40000(40MHz) 가능.
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "FS.h"
#include "C10_Config_014.h"

// 8KB (SD_MMC DMA 최적화), 반드시 512의 배수여야 함
#define SD10_BUF_SIZE         8192  
#define SD10_FLASH_INTERVAL   10000 // 10초

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

    // [최적화] SD_MMC 초기화 및 핀 설정
    bool begin() {
        if(!SD_MMC.setPins(C10_Config::SD_MMC_CLK, C10_Config::SD_MMC_CMD, C10_Config::SD_MMC_D0)){
            return false;
        }
        // 1-bit mode, 전압/속도 파라미터 적용
        if (!SD_MMC.begin(C10_Config::SD_MOUNT, true, false, 20000)) {
            Serial.println(F("E: SD_MMC Mount Failed"));
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
            p_opts.useSD  = v_jsonDoc["useSD"]  | p_opts.useSD;
            if (v_jsonDoc["logPrefix"]) {
                strlcpy(p_opts.logPrefix, v_jsonDoc["logPrefix"], sizeof(p_opts.logPrefix));
            }
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

    // [속도 최적화] 데이터 버퍼링 인터페이스
    void logToBuffer(const char* p_str) {
        size_t v_len = strlen(p_str);
        ST_SD10_LogBuffer &v_active = _buffers[_curIdx];

        // 공간 부족 시 버퍼 교체
        if (v_active.pos + v_len >= SD10_BUF_SIZE) {
            _lastWriteIdx = _curIdx;
            _lastWriteLen = v_active.pos;
            
            _curIdx = (_curIdx + 1) % 2;
            _buffers[_curIdx].pos = 0; // 새 버퍼 초기화

            xSemaphoreGive(_sem_sd_write); // 기록 태스크 깨움
        }

        // 현재 버퍼에 데이터 복사
        ST_SD10_LogBuffer &v_new_active = _buffers[_curIdx];
        if (v_new_active.pos + v_len < SD10_BUF_SIZE) {
            memcpy(&v_new_active.data[v_new_active.pos], p_str, v_len);
            v_new_active.pos += v_len;
        }
    }

    // [전력 최적화] 전용 태스크에서 실행되는 쓰기 로직
    void processWrite() {
        if (xSemaphoreTake(_sem_sd_write, portMAX_DELAY) == pdTRUE) {
            if (_logFile) {
                // DMA 활용한 물리적 쓰기
                _logFile.write((const uint8_t*)_buffers[_lastWriteIdx].data, _lastWriteLen);
                
                // 불필요한 flush 억제 (10초 주기)
                static uint32_t v_lastSync = 0;
                if (millis() - v_lastSync > SD10_FLASH_INTERVAL) {
                    _logFile.flush();
                    v_lastSync = millis();
                }
            }
        }
    }

    // [누락 보강] 종료 시 잔여 데이터 처리
    void end() {
        if (_logFile) {
            // 현재 쓰고 있던 버퍼의 남은 내용 강제 기록
            if (_buffers[_curIdx].pos > 0) {
                _logFile.write((const uint8_t*)_buffers[_curIdx].data, _buffers[_curIdx].pos);
                _buffers[_curIdx].pos = 0;
            }
            _logFile.flush();
            _logFile.close();
        }
        SD_MMC.end();
        Serial.println(F("I: SD_MMC Session Closed"));
    }
};
