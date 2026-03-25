#pragma once

#include "T20_Mfcc_009.h"

/*
===============================================================================
소스명: T20_Main_009.h
버전: v009

[기능 스펙]
- T20 MFCC 모듈 단일 인스턴스 접근
- 기본 초기화 / 주기 출력 예제 제공
===============================================================================
*/

inline CL_T20_Mfcc& T20_getMfcc()
{
    static CL_T20_Mfcc s_mfcc;
    return s_mfcc;
}

inline void T20_init()
{
    CL_T20_Mfcc& mfcc = T20_getMfcc();

    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    cfg.output.output_mode = EN_T20_OUTPUT_VECTOR;
    // cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;

    cfg.preprocess.filter.enable = true;
    cfg.preprocess.filter.type = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;

    if (!mfcc.begin(&cfg)) {
        Serial.println("T20 begin failed");
        while (1) { delay(1000); }
    }

    if (!mfcc.start()) {
        Serial.println("T20 start failed");
        while (1) { delay(1000); }
    }

    mfcc.printConfig(Serial);
    mfcc.printStatus(Serial);
}

inline void T20_run()
{
    static uint32_t t0 = 0;

    if (millis() - t0 >= 1000) {
        t0 = millis();

        CL_T20_Mfcc& mfcc = T20_getMfcc();

        mfcc.printStatus(Serial);
        mfcc.printLatest(Serial);

        if (mfcc.isSequenceReady()) {
            Serial.println("Sequence ready");
        }
    }
}
