#pragma once

#include "T20_Mfcc_011.h"

/*
===============================================================================
소스명: T20_Main_011.h
버전: v011

[역할]
- T20 MFCC 모듈 사용 예시
- Bundle-A 구조 시험용 간단 init/run helper

[주의]
- 이 파일은 예시 진입점이다.
- 실제 프로젝트에서는 logger, button, web, recorder와 통합될 수 있다.
===============================================================================
*/

inline CL_T20_Mfcc& T20_getMfcc(void)
{
    static CL_T20_Mfcc s_t20_mfcc;
    return s_t20_mfcc;
}

inline void T20_init(void)
{
    CL_T20_Mfcc& v_mfcc = T20_getMfcc();
    ST_T20_Config_t v_cfg = T20_makeDefaultConfig();

    // Example: 50% overlap
    // v_cfg.feature.hop_size = v_cfg.feature.frame_size / 2U;

    // Example: sequence mode
    // v_cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;

    if (!v_mfcc.begin(&v_cfg)) {
        Serial.println("T20 begin failed");
        while (1) { delay(1000); }
    }

    if (!v_mfcc.start()) {
        Serial.println("T20 start failed");
        while (1) { delay(1000); }
    }

    v_mfcc.printConfig(Serial);
    v_mfcc.printStatus(Serial);
}

inline void T20_run(void)
{
    static uint32_t s_last_ms = 0U;
    if ((millis() - s_last_ms) >= 1000U) {
        s_last_ms = millis();

        CL_T20_Mfcc& v_mfcc = T20_getMfcc();
        v_mfcc.printStatus(Serial);
        v_mfcc.printLatest(Serial);
    }
}
