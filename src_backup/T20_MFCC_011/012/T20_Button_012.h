#pragma once

#include <Arduino.h>
#include "T20_Mfcc_Def_012.h"

/*
===============================================================================
소스명: T20_Button_012.h
버전: v012

[기능 스펙]
- 버튼 입력 보조 헤더
- 현재 단계에서는 CL_T20_Mfcc::pollButton()을 사용하므로
  별도 독립 클래스 없이 확장 포인트 문서 역할을 겸함

[향후 단계 TODO]
- 버튼 입력을 별도 클래스로 분리
- short / long / double press
- event marker / calibration / UI mode toggle
===============================================================================
*/
