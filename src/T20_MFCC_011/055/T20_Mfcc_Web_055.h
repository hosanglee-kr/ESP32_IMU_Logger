#pragma once

#include "T20_Mfcc_Inter_055.h"

/*
===============================================================================
소스명: T20_Mfcc_Web_055.h
버전: v055

[기능 스펙]
- AsyncWeb 제어/다운로드 계층 선언
- LittleFS 정적 html/css/js 파일 제공 선언
- 설정 조회/설정 적용/live 조회 엔드포인트 지원 선언
- WebSocket / SSE 실시간 push 선언

[향후 단계 TODO]
- live waveform / live sequence
- websocket / SSE
- 인증 / 접근 제어
===============================================================================
*/


void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);
