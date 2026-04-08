/* ============================================================================
 * File: T200_Main_217.h
 * Summary: Application Entry Point Wrapper (v217)
 * ========================================================================== */
#pragma once
#include "T220_Mfcc_217.h"
#include "T218_Def_Main_217.h"

// 싱글톤 패턴처럼 사용하기 위한 접근자
inline CL_T20_Mfcc& T20_instance() {
    static CL_T20_Mfcc instance;
    return instance;
}

inline void T20_init() {
    
    
    auto& mfcc = T20_instance();
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

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
}

