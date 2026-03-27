#pragma once

#include "T20_Mfcc_028.h"

/*
===============================================================================
소스명: T20_Main_028.h
버전: v028

[기능 스펙]
- T20 모듈 사용 예시
- begin/start + 버튼 poll + 상태 출력 예시
- recorder 설정 예시 포함
===============================================================================
*/

inline CL_T20_Mfcc& T20_getMfcc()
{
    static CL_T20_Mfcc s_mfcc;
    return s_mfcc;
}

inline void T20_init()
{
    CL_T20_Mfcc& v_mfcc = T20_getMfcc();
    ST_T20_Config_t cfg = T20_makeDefaultConfig();

    // 예시:
    // cfg.feature.hop_size = 128; // 50% overlap
    // cfg.output.output_mode = EN_T20_OUTPUT_SEQUENCE;
    // cfg.recorder.raw_format = EN_T20_REC_RAW_CSV;
    // cfg.recorder.write_feature_vector_csv = true;

    if (!v_mfcc.begin(&cfg)) {
        Serial.println("T20 begin failed");
        while (1) { delay(1000); }
    }

    if (!v_mfcc.start()) {
        Serial.println("T20 start failed");
        while (1) { delay(1000); }
    }

    v_mfcc.printConfig(Serial);
    v_mfcc.printStatus(Serial);

    // 향후 단계 TODO:
    // extern AsyncWebServer server;
    // LittleFS.begin(true);
    // v_mfcc.attachWebStaticFiles(&server);
    // v_mfcc.attachWebServer(&server, G_T20_WEB_DEFAULT_BASE_PATH);

    // TODO(향후 단계):
    // AsyncWebServer 사용 시 아래와 같이 연결 가능
    //   extern AsyncWebServer server;
    //   v_mfcc.attachWebServer(&server, G_T20_WEB_DEFAULT_BASE_PATH);
    v_mfcc.printStatus(Serial);
}

inline void T20_run()
{
    CL_T20_Mfcc& v_mfcc = T20_getMfcc();

    v_mfcc.pollButton();

    static uint32_t t0 = 0;
    if (millis() - t0 >= 1000) {
        t0 = millis();

        v_mfcc.printStatus(Serial);
        v_mfcc.printConfig(Serial);
        v_mfcc.printStatus(Serial);
        v_mfcc.printLatest(Serial);

        if (v_mfcc.isSequenceReady()) {
            Serial.println("Sequence ready");
        }
    }
}
