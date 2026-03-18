/*
 * ------------------------------------------------------
 * 소스명 : SD10_SDMMC_010.h
 * 모듈약어 : SD10 (SDMMC)
 * 모듈명 : SD_MMC Interface & JSON Config Manager
 * ------------------------------------------------------
 * 기능 요약
 * - SD_MMC 버스 제어 및 저전력 모드 대응 (Mount/Unmount)
 * - High-Performance SdFat Interface (Double Buffering)
 * -
 * - 파일 인덱싱을 통한 데이터 로테이션 구현
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "C10_Config_009.h"

#ifndef DISABLE_FS_H_WARNING
	#define DISABLE_FS_H_WARNING  // Disable warning for type File not defined.
#endif  // DISABLE_FS_H_WARNING
#include <SdFat.h>

#define SD_FAT_TYPE 3		// 1 for FAT16/FAT32, 2 for exFAT, 3 for FAT16/FAT32 and exFAT.


// 더블 버퍼링 설정
#define SD10_BUF_SIZE 4096      // 4KB (SD 섹터 단위 최적화)
#define SD10_PREALLOC_MB 50     // 50MB 미리 할당

struct ST_SD10_LogBuffer {
    char data[SD10_BUF_SIZE];
    uint16_t pos = 0;
};

class CL_SD10_SDMMC_Handler {
private:
    SdFs     _sd;
    FsFile   _logFile;
    String   _currentPath;

    SemaphoreHandle_t  _sem_sd_write;

    ST_SD10_LogBuffer  _buffers[2]; // 더블 버퍼
    volatile uint8_t   _curIdx = 0;
    volatile uint8_t   _lastWriteIdx = 0;
    volatile uint16_t  _lastWriteLen = 0;


public:
    CL_SD10_SDMMC_Handler() {
        _sem_sd_write = xSemaphoreCreateBinary();
    }

    bool begin() {
        // SD_MMC 대신 SdFat의 SdFs(FAT32/exFAT 지원) 사용
        // ESP32-S3 SDMMC 핀 설정에 맞춰 설정 필요
        if (!_sd.begin(SdSpiConfig(SS, SHARED_SPI, SD_SCK_MHZ(25)))) {
            return false;
        }
        return true;
    }

	void loadConfig(ST_BMI270_Options_t& p_opts) {
        FsFile v_file = _sd.open(C10_Config::CONFIG_PATH, O_RDONLY);
        if (!v_file) return;
        JsonDocument v_jsonDoc;
        if (deserializeJson(v_jsonDoc, v_file) == DeserializationError::Ok) {
            p_opts.useVQF = v_jsonDoc["useVQF"] | p_opts.useVQF;
            p_opts.useSD = v_jsonDoc["useSD"] | p_opts.useSD;
            if (v_jsonDoc["logPrefix"]) strlcpy(p_opts.logPrefix, v_jsonDoc["logPrefix"], sizeof(p_opts.logPrefix));
        }
        v_file.close();
    }

    void end() {
        if (_logFile.isOpen()) {
            _logFile.truncate(); // 실제 사용량만큼 파일 크기 조정
            _logFile.close();
        }
    }

    // 파일 미리 할당 및 인덱싱 생성
    bool createLogFile(const char* p_prefix, const char* p_header) {
        int v_index = 1;
        char v_filename[32];
        while (v_index < 1000) {
            snprintf(v_filename, sizeof(v_filename), "/%s_%03d.csv", p_prefix, v_index);
            if (!_sd.exists(v_filename)) break;
            v_index++;
        }
        _currentPath = String(v_filename);

        if (!_logFile.open(v_filename, O_WRONLY | O_CREAT | O_TRUNC)) return false;

        // [최적화] Pre-allocation: 물리적 섹터를 미리 확보하여 쓰기 지연 방지
        if (!_logFile.preAllocate(SD10_PREALLOC_MB * 1024 * 1024)) {
            Serial.println("F: Pre-alloc fail");
        }

        _logFile.println(p_header);
        return true;
    }

    // 더블 버퍼링 기반 쓰기 (센서 태스크에서 호출)
    void logToBuffer(const char* p_str) {
        size_t v_len = strlen(p_str);
        ST_SD10_LogBuffer &v_active = _buffers[_curIdx];

        // 버퍼 공간 부족 시 스위칭
        if (v_active.pos + v_len >= SD10_BUF_SIZE) {
            uint8_t v_oldIdx = _curIdx;
            _curIdx = (_curIdx + 1) % 2; // 버퍼 인덱스 교체
            _buffers[_curIdx].pos = 0;   // 새 버퍼 초기화

            // SD 쓰기 태스크에 데이터가 가득 찼음을 알림
            xSemaphoreGive(_sem_sd_write);

            // 전송할 버퍼의 최종 위치 저장
            _lastWriteLen = v_active.pos;
            _lastWriteIdx = v_oldIdx;
        }

        memcpy(&_buffers[_curIdx].data[_buffers[_curIdx].pos], p_str, v_len);
        _buffers[_curIdx].pos += v_len;
    }

    // SD 쓰기 전용 핸들러 (SD 전용 태스크에서 루프로 실행)
    void processWrite() {
        if (xSemaphoreTake(_sem_sd_write, portMAX_DELAY) == pdTRUE) {
            _logFile.write(_buffers[_lastWriteIdx].data, _lastWriteLen);
            // 가끔씩 물리적으로 저장 보장 (너무 자주 하면 느려짐)
            static uint32_t v_lastSync = 0;
            if (millis() - v_lastSync > 2000) {
                _logFile.sync();
                v_lastSync = millis();
            }
        }
    }



};

