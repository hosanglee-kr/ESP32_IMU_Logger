// T20_Main_006.h

#pragma once

#include "T20_Mfcc_005.h"


inline CL_T20_Mfcc& T20_getMfcc()
{
    static CL_T20_Mfcc s_mfcc;
    return s_mfcc;
}
//  inline CL_T20_Mfcc g_mfcc;

inline void T20_init()
{
    CL_T20_Mfcc& v_mfcc = T20_getMfcc();
    
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    // 출력 모드 선택
    cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;
    // cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;

    // 필터 선택
    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;

    if (!v_mfcc.begin(&cfg)) {
        Serial.println("g_mfcc.begin failed");
        while (1) { delay(1000); }
    }

    if (!v_mfcc.start()) {
        Serial.println("g_mfcc.start failed");
        while (1) { delay(1000); }
    }

    g_mfcc.printConfig(Serial);
}

inline void T20_run()
{
    static uint32_t t0 = 0;
    if (millis() - t0 >= 1000) {
        t0 = millis();
        
        CL_T20_Mfcc& v_mfcc = T20_getMfcc();

        v_mfcc.printLatest(Serial);

        if (v_mfcc.isSequenceReady()) {
            Serial.println("Sequence ready");
        }
    }
}

