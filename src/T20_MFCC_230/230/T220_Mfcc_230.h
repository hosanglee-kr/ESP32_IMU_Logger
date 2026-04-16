/* ============================================================================
 * File: T220_Mfcc_230.h
 * Summary: Master Controller Public Interface
 * ========================================================================== */
#pragma once
#include "T210_Def_230.h"

class CL_T20_Mfcc {
public:
    struct ST_Impl;
    CL_T20_Mfcc();
    ~CL_T20_Mfcc();

    bool begin(const ST_T20_Config_t* p_cfg = nullptr);
    bool start();
    void stop();
    void run(); // Watchdog 및 버튼 루프

    void printStatus(Stream& out) const;

private:
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;
    ST_Impl* _impl;

    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
    friend void T20_recorderTask(void* p_arg); // 레코더 독립 태스크
};

extern CL_T20_Mfcc* g_t20;
