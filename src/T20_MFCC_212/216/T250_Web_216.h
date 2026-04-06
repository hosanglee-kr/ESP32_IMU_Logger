/* ============================================================================
 * File: T250_Web_216.h
 * Summary: Web API & WebSocket Handler Declarations (C++17 Namespace 통합)
 * ========================================================================== */

#pragma once

#include <AsyncJson.h>
#include "T221_Mfcc_Inter_216.h"

/* ----------------------------------------------------------------------------
 * [참고] 이전의 T250_Web 전용 상수들은 모두 T210_Def_Com_216.h 파일의 
 * T20::C10_Web 네임스페이스로 완벽히 통폐합되었습니다. 
 * 따라서 이 파일에서는 불필요한 상수 중복 선언을 모두 제거했습니다.
 * ---------------------------------------------------------------------------- */

// API 핸들러 등록 함수
void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);

// 프론트엔드 정적 파일 서빙 전용 핸들러
void T20_registerStaticFrontendHandlers(AsyncWebServer* v_server);

// 바이너리 데이터 브로드캐스트
void T20_broadcastBinaryData(CL_T20_Mfcc::ST_Impl* p);

// 캘리브레이션 실행 및 결과를 LittleFS에 JSON으로 저장 (수동 트리거)
bool T20_bmi270_RunAndSaveCalibration(CL_T20_Mfcc::ST_Impl* p) {


