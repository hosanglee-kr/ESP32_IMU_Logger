#pragma once

#include "T20_Mfcc_Inter_056.h"

/* ============================================================================
 * File: T20_Mfcc_Web_056.h
 * Summary:
 *   Web API 등록 헤더
 * ========================================================================== */

void T20_registerWebHandlers(CL_T20_Mfcc::ST_Impl* p, AsyncWebServer* v_server, const char* p_base_path);
