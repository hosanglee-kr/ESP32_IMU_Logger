#pragma once

#include "T20_Mfcc_074.h"

inline CL_T20_Mfcc& T20_getMfcc()
{
    static CL_T20_Mfcc s_mfcc;
    return s_mfcc;
}

inline void T20_init()
{
    CL_T20_Mfcc& mfcc = T20_getMfcc();
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    if (!mfcc.begin(&cfg)) {
        Serial.println("T20 begin failed");
        while (1) { delay(1000); }
    }

    if (!mfcc.start()) {
        Serial.println("T20 start failed");
        while (1) { delay(1000); }
    }

    mfcc.printConfig(Serial);
}

inline void T20_run()
{
    static uint32_t t0 = 0;
    if (millis() - t0 >= 1000) {
        t0 = millis();
        T20_getMfcc().printStatus(Serial);
    }
}
