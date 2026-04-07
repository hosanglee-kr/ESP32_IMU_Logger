/* ============================================================================
 * File: T220_Mfcc_217.h
 * Summary: Master Controller Public Interface
 * ========================================================================== */
#pragma once
#include "T214_Def_Rec_217.h"

class CL_T20_Mfcc {
public:
    struct ST_Impl;
    CL_T20_Mfcc();
    ~CL_T20_Mfcc();

    bool begin(const ST_T20_Config_t* p_cfg = nullptr);
    bool start();
    void stop();
    void run(); // [복원] Watchdog 및 버튼 루프

    void printStatus(Stream& out) const;

private:
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;
    ST_Impl* _impl;

    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
    friend void T20_recorderTask(void* p_arg); // [복원] 레코더 독립 태스크
};

extern CL_T20_Mfcc* g_t20;
