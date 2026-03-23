// T20_Main_004.h

#pragma once

#include "T20_Mfcc_004.h"

CL_T20_Mfcc_004 g_mfcc;

void T20_init()
{
    Serial.begin(115200);

    T20_Config_004_t cfg = T20_makeDefaultConfig_004();

    // 출력 모드 선택
    cfg.output.output_mode = EN_T20_OUTPUT_VECTOR_004;
    // cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE_004;

    // 필터 선택
    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF_004;
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

