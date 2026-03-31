

/* ============================================================================
 * File: T20_Def_Viewer_021.h

 * ========================================================================== */

#pragma once
#include "T20_Def_Comm_021.h"

// [UI/Viewer 설정]
#define G_T20_VIEWER_EVENT_MAX			   16U
#define G_T20_VIEWER_RECENT_WAVE_COUNT	   4U
#define G_T20_VIEWER_SELECTION_POINTS_MAX  128U

#define G_T20_CSV_SERVER_MAX_ROWS		   128U
#define G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT  20U

#define G_T20_TEXT_PREVIEW_LINE_BUF_SIZE   256U

// [미리보기 및 메타데이터]
#define G_T20_TYPE_META_KIND_MAX		   24U
#define G_T20_TYPE_META_AUTO_TEXT_MAX	   64U
#define G_T20_TYPE_META_NAME_MAX		   32U
#define G_T20_TYPE_META_PREVIEW_LINK_MAX   16U

#define G_T20_TYPE_PREVIEW_TEXT_BUF_MAX	   512U
#define G_T20_TYPE_PREVIEW_SAMPLE_ROWS_MAX 8U

#define G_T20_WEB_JSON_BUF_SIZE			   2048U
#define G_T20_WEB_LARGE_JSON_BUF_SIZE	   8192U
#define G_T20_WEB_PATH_BUF_SIZE			   256U

typedef struct {
	uint32_t frame_id;
	char	 kind[16];
	char	 text[G_T20_RECORDER_EVENT_TEXT_MAX];
} ST_T20_ViewerEvent_t;

typedef struct {
	char name[32];
	bool used;
} ST_T20_ProfileInfo_t;
