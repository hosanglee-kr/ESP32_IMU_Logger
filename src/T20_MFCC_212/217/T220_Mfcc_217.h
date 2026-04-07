/* ============================================================================
 * File: T220_Mfcc_217.h
 * Summary: Master Controller Public Interface (v217)
 * ========================================================================== */
#pragma once

#include "T210_Def_Com_217.h"
#include "T214_Def_Rec_217.h"

class CL_T20_Mfcc {
public:
    struct ST_Impl; // Pimpl 패턴 유지

    CL_T20_Mfcc();
    ~CL_T20_Mfcc();

    // 시스템 시작/종료
    bool begin(const ST_T20_Config_t* p_cfg = nullptr);
    bool start();
    void stop();

    // 런타임 제어 및 정보 조회
    bool setConfig(const ST_T20_Config_t* p_cfg);
    void printStatus(Stream& out) const;

private:
    CL_T20_Mfcc(const CL_T20_Mfcc&) = delete;
    CL_T20_Mfcc& operator=(const CL_T20_Mfcc&) = delete;

    ST_Impl* _impl;

    // RTOS 태스크 프렌드 선언
    friend void T20_sensorTask(void* p_arg);
    friend void T20_processTask(void* p_arg);
};

// 전역 싱글톤 인스턴스 (선택 사항)
extern CL_T20_Mfcc* g_t20;

