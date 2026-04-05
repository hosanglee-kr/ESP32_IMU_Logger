/* ============================================================================
 * File: T216_Def_View_216.h
 * Summary: Viewer, Metadata, and UI Struct Definitions
 * ========================================================================== */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "T210_Def_Com_216.h"

// ----------------------------------------------------------------------------
// 1. Struct Definitions (State)
// ----------------------------------------------------------------------------
typedef struct {
    EM_T20_State_t ui;
    EM_T20_State_t render;
    EM_T20_State_t data_fetch;
    EM_T20_State_t interaction;
} ST_T20_ViewerState_t;

typedef struct {
    ST_T20_ViewerState_t state;
} ST_T20_ViewerHandle_t;

static inline void T20_Viewer_StateMachine(ST_T20_ViewerHandle_t* h) {
    switch (h->state.render) {
        case EN_T20_STATE_IDLE:
            h->state.render = EN_T20_STATE_READY;
            break;
        case EN_T20_STATE_READY:
            h->state.render = EN_T20_STATE_RUNNING;
            break;
        case EN_T20_STATE_RUNNING:
            h->state.render = EN_T20_STATE_DONE;
            break;
        default:
            break;
    }
}

// ----------------------------------------------------------------------------
// 2. Struct Definitions (Data & Profile)
// ----------------------------------------------------------------------------
typedef struct {
    uint32_t frame_id;
    char     kind[16];
    char     text[T20::C10_View::META_TEXT_MAX]; 
} ST_T20_ViewerEvent_t;

typedef struct {
    char name[T20::C10_View::META_NAME_MAX];
    bool used;
} ST_T20_ProfileInfo_t;

// ----------------------------------------------------------------------------
// 3. Debug Helper
// ----------------------------------------------------------------------------
static inline void T20_Viewer_DebugState(ST_T20_ViewerState_t* s) {
    printf("[UI:%s RENDER:%s FETCH:%s INPUT:%s]\n",
        T20_StateToString(s->ui),
        T20_StateToString(s->render),
        T20_StateToString(s->data_fetch),
        T20_StateToString(s->interaction));
}

