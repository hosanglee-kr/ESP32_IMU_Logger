/* ============================================================================
 * File: T200_Main_219.h
 * Summary: Application Entry Point Wrapper (v218 Config Auto-load)
 * ========================================================================== */
#pragma once
#include "T220_Mfcc_219.h"
#include "T218_Def_Main_219.h"
#include "T219_Config_Json_219.h"
#include <LittleFS.h>

inline CL_T20_Mfcc& T20_instance() {
    static CL_T20_Mfcc instance;
    return instance;
}

inline void T20_init() {

    auto& mfcc = T20_instance();
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    // LittleFS 마운트 및 JSON 설정 로드
    if (LittleFS.begin(true)) {
        if (LittleFS.exists(T20::C10_Path::FILE_CFG_JSON)) {
            File f = LittleFS.open(T20::C10_Path::FILE_CFG_JSON, "r");
            if (f) {
                String json_str = f.readString();
                if (CL_T20_ConfigJson::parseFromJson(json_str.c_str(), cfg)) {
                    Serial.println(F("[T20] Runtime Config Loaded from FS"));
                }
                f.close();
            }
        } else {
            // 파일이 없으면 현재 cfg(기본값)를 기반으로 새로 생성
            File f = LittleFS.open(T20::C10_Path::FILE_CFG_JSON, "w");
            if (f) {
                String json_str;
                CL_T20_ConfigJson::buildJsonString(cfg, json_str);
                f.print(json_str);
                f.close();
                Serial.println(F("[T20] Default Config Created to FS"));
            }
        }
    } else {
        Serial.println(F("[T20] LittleFS Mount Failed. Using Default Config."));
    }

    // 로드된 설정(cfg)으로 엔진 시작
    if (mfcc.begin(&cfg)) {
        mfcc.start();
        Serial.println(F("[T20] System Started Successfully"));
    } else {
        Serial.println(F("[T20] System Initialization Failed"));
    }
}

inline void T20_run() {
    static uint32_t last_ms = 0;
    if (millis() - last_ms > 2000) {
        last_ms = millis();
        T20_instance().printStatus(Serial);
    }
    T20_instance().run(); // Mfcc 루프 처리 (Watchdog 등)
}

