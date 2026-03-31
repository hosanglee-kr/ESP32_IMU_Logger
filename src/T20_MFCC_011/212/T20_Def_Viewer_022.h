// T20_Def_Viewer_021.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "T20_Def_Comm_021.h"

/* ============================================================================
 * Global Constants (G_T20_)
 * ========================================================================== */

/* --- Viewer Limits --- */
static const uint16_t G_T20_VIEWER_EVENT_MAX            = 16U;
static const uint16_t G_T20_VIEWER_RECENT_WAVE_COUNT    = 4U;
static const uint16_t G_T20_VIEWER_SELECTION_POINTS_MAX = 128U;

/* --- CSV / Table --- */
static const uint16_t G_T20_CSV_SERVER_MAX_ROWS          = 128U;
static const uint16_t G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT  = 20U;

/* --- Text Buffer --- */
static const uint16_t G_T20_TEXT_PREVIEW_LINE_BUF_SIZE = 256U;

/* --- Metadata --- */
static const uint16_t G_T20_TYPE_META_KIND_MAX         = 24U;
static const uint16_t G_T20_TYPE_META_AUTO_TEXT_MAX    = 64U;
static const uint16_t G_T20_TYPE_META_NAME_MAX         = 32U;
static const uint16_t G_T20_TYPE_META_PREVIEW_LINK_MAX = 16U;

/* --- Preview --- */
static const uint16_t G_T20_TYPE_PREVIEW_TEXT_BUF_MAX    = 512U;
static const uint16_t G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX = 8U;

/* --- Web Buffer --- */
static const uint16_t G_T20_WEB_JSON_BUF_SIZE        = 2048U;
static const uint16_t G_T20_WEB_LARGE_JSON_BUF_SIZE  = 8192U;
static const uint16_t G_T20_WEB_PATH_BUF_SIZE        = 256U;

/* ============================================================================
 * Viewer Limits (Magic Number 그룹화)
 * ========================================================================== */

typedef struct {
    uint16_t event_max;
    uint16_t recent_wave_count;
    uint16_t selection_points_max;
    uint16_t csv_max_rows;
    uint16_t csv_page_size;
} ST_T20_ViewerLimits_t;

static const ST_T20_ViewerLimits_t G_T20_VIEWER_LIMITS = {
    .event_max = 16U,
    .recent_wave_count = 4U,
    .selection_points_max = 128U,
    .csv_max_rows = 128U,
    .csv_page_size = 20U
};

/* ============================================================================
 * Common State / Result (일관성 유지)
 * ========================================================================== */

typedef enum {
    EN_T20_STATE_IDLE = 0,
    EN_T20_STATE_READY,
    EN_T20_STATE_RUNNING,
    EN_T20_STATE_DONE,
    EN_T20_STATE_ERROR
} EM_T20_State_t;

typedef enum {
    EN_T20_RESULT_FAIL = 0,
    EN_T20_RESULT_OK
} EM_T20_Result_t;

/* ============================================================================
 * Debug String
 * ========================================================================== */

static inline const char* T20_StateToString(EM_T20_State_t s)
{
    switch (s) {
        case EN_T20_STATE_IDLE: return "IDLE";
        case EN_T20_STATE_READY: return "READY";
        case EN_T20_STATE_RUNNING: return "RUNNING";
        case EN_T20_STATE_DONE: return "DONE";
        case EN_T20_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/* ============================================================================
 * Viewer State
 * ========================================================================== */

typedef struct {
    EM_T20_State_t ui;
    EM_T20_State_t render;
    EM_T20_State_t data_fetch;
    EM_T20_State_t interaction;
} ST_T20_ViewerState_t;

/* ============================================================================
 * Viewer State Machine
 * ========================================================================== */

typedef struct {
    ST_T20_ViewerState_t state;
} ST_T20_ViewerHandle_t;

static inline void T20_Viewer_StateMachine(ST_T20_ViewerHandle_t* h)
{
    switch (h->state.render)
    {
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

/* ============================================================================
 * Viewer Event
 * ========================================================================== */

typedef struct {
    uint32_t frame_id;
    char kind[16];
    char text[80]; /* Recorder dependency 제거 */
} ST_T20_ViewerEvent_t;

/* ============================================================================
 * Profile Info
 * ========================================================================== */

typedef struct {
    char name[G_T20_TYPE_META_NAME_MAX];
    bool used;
} ST_T20_ProfileInfo_t;

/* ============================================================================
 * Debug Helper
 * ========================================================================== */

static inline void T20_Viewer_DebugState(ST_T20_ViewerState_t* s)
{
    printf("[UI:%s RENDER:%s FETCH:%s INPUT:%s]\n",
        T20_StateToString(s->ui),
        T20_StateToString(s->render),
        T20_StateToString(s->data_fetch),
        T20_StateToString(s->interaction));
}
