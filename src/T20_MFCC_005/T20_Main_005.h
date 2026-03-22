// T20_Main_005.h

#pragma once

#include "T20_Mfcc_005.h"

CL_T20_Mfcc g_mfcc;

void T20_init()
{
    
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    // 출력 모드 선택
    cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;
    // cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;

    // 필터 선택
    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;

    if (!g_mfcc.begin(&cfg)) {
        Serial.println("g_mfcc.begin failed");
        while (1) { delay(1000); }
    }

    if (!g_mfcc.start()) {
        Serial.println("g_mfcc.start failed");
        while (1) { delay(1000); }
    }

    g_mfcc.printConfig(Serial);
}

void T20_run()
{
    static uint32_t t0 = 0;
    if (millis() - t0 >= 1000) {
        t0 = millis();

        g_mfcc.printLatest(Serial);

        if (g_mfcc.isSequenceReady()) {
            Serial.println("Sequence ready");
        }
    }
}

