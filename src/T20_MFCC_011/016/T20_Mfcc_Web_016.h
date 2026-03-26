#pragma once

#include "T20_Mfcc_Inter_016.h"

/*
===============================================================================
소스명: T20_Mfcc_Web_016.h
버전: v016

[기능 스펙]
- AsyncWeb 제어/다운로드 대묶음용 내부 웹 계층 선언
- 공개 헤더에서 ESPAsyncWebServer 타입을 직접 노출하지 않기 위해
  void* 기반 등록 인터페이스를 사용
- status / config / latest / measurement control / recorder file download endpoint 제공

[향후 단계 TODO]
- POST body 기반 설정 변경
- live waveform / live feature streaming
- websocket / SSE 실시간 시각화
- route remove / 재등록 고도화
===============================================================================
*/
