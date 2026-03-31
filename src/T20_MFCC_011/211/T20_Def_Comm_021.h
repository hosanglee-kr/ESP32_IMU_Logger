
/* ============================================================================
 * File: T20_Def_Comm_021.h

 * ========================================================================== */

#pragma once
#include <Arduino.h>

// [버전 및 기본 정보]
#define G_T20_VERSION_STR				"T20_Mfcc_210"


// [하드웨어 핀 맵 - ESP32-S3]
#define G_T20_PIN_SPI_SCK				12
#define G_T20_PIN_SPI_MISO				13
#define G_T20_PIN_SPI_MOSI				11
#define G_T20_PIN_BMI_CS				10
#define G_T20_PIN_BMI_INT1				14

// [OS 리소스 및 태스크 설정]
#define G_T20_QUEUE_LEN					4U
#define G_T20_SENSOR_TASK_STACK			6144U
#define G_T20_PROCESS_TASK_STACK		12288U
#define G_T20_RECORDER_TASK_STACK		8192U
#define G_T20_SENSOR_TASK_PRIO			4U
#define G_T20_PROCESS_TASK_PRIO			3U
#define G_T20_RECORDER_TASK_PRIO		2U

// [공통 수학 및 유틸리티]
#define G_T20_PI						3.14159265358979323846f
#define G_T20_EPSILON					1.0e-12f

// [Web/JSON 버퍼 설정]
#define G_T20_WEB_JSON_BUF_SIZE			2048U
#define G_T20_WEB_LARGE_JSON_BUF_SIZE	8192U
#define G_T20_RUNTIME_CFG_JSON_BUF_SIZE 1536U


#define G_T20_CFG_PROFILE_COUNT				   4U
#define G_T20_RAW_FRAME_BUFFERS				   4U
#define G_T20_SELECTION_SYNC_NAME_MAX		   32U
#define G_T20_RUNTIME_CFG_PROFILE_NAME_MAX	   32U
#define G_T20_RENDER_SELECTION_SYNC_MAX		   4U


#define G_T20_WEB_PATH_BUF_SIZE				   256U
#define G_T20_PREVIEW_TEXT_BYTES_DEFAULT	   4096U
#define G_T20_PREVIEW_TEXT_BYTES_MAX		   16384U

#define G_T20_CSV_TABLE_PAGE_SIZE_MAX		   100U
