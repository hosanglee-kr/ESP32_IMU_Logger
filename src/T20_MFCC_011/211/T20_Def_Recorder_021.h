
/* ============================================================================
 * File: T20_Def_Recorder_021.h
 * Summary: SD_MMC Data Logging & Storage Backend Definitions (v210)
 * ========================================================================== */

#pragma once

#include "T20_Def_Comm_021.h"

/* --- [SDMMC Hardware Profile Constants] --- */
#define G_T20_SDMMC_PROFILE_NAME_MAX			 32U /* 핵심 누락 상수 복구 */
#define G_T20_SDMMC_MOUNT_PATH_DEFAULT			 "/sdcard"

/* --- [Recorder Batch & Performance] --- */
#define G_T20_RECORDER_BATCH_COUNT				 16U
#define G_T20_RECORDER_BATCH_BUFFER_MAX			 G_T20_RECORDER_BATCH_COUNT
#define G_T20_RECORDER_BATCH_ACCUM_WATERMARK	 8U
#define G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS	 300U
#define G_T20_RECORDER_FLUSH_INTERVAL_MS		 1000U
#define G_T20_RECORDER_BATCH_WATERMARK_LOW		 2U
#define G_T20_RECORDER_BATCH_WATERMARK_HIGH		 8U
#define G_T20_RECORDER_BATCH_IDLE_FLUSH_MS		 250U
#define G_T20_RECORDER_BATCH_VECTOR_MAX			 16U
#define G_T20_RECORDER_BATCH_FLUSH_RECORDS		 4U
#define G_T20_RECORDER_BATCH_FLUSH_TIMEOUT_MS	 1000U

/* --- [Zero-Copy & DMA Configuration] --- */
#define G_T20_RECORDER_DMA_ALIGN_BYTES			 32U
#define G_T20_RECORDER_ZERO_COPY_SLOT_MAX		 2U
#define G_T20_ZERO_COPY_STAGE_BUFFER_BYTES		 4096U
#define G_T20_ZERO_COPY_DMA_SLOT_BYTES			 1024U
#define G_T20_ZERO_COPY_DMA_SLOT_COUNT			 2U
#define G_T20_ZERO_COPY_DMA_COMMIT_MIN_BYTES	 64U

/* --- [File System & Path Management] --- */
#define G_T20_RECORDER_FILE_PATH_MAX			 192U
#define G_T20_RECORDER_LAST_ERROR_MAX			 128U
#define G_T20_RECORDER_MAX_ROTATE_LIST			 16U
#define G_T20_RECORDER_EVENT_TEXT_MAX			 80U
#define G_T20_RECORDER_ROTATE_KEEP_MAX			 8U
#define G_T20_RECORDER_ROTATE_PREFIX			 "/rec_"
#define G_T20_RECORDER_ROTATE_EXT				 ".bin"
#define G_T20_RECORDER_FALLBACK_PATH			 "/rec_fallback.bin"
#define G_T20_RECORDER_DEFAULT_FILE_PATH		 "/t20_rec.bin"
#define G_T20_RECORDER_INDEX_FILE_PATH			 "/t20_rec_index.json"
#define G_T20_RECORDER_RUNTIME_CFG_FILE_PATH	 "/t20_runtime_cfg.json"
#define G_T20_RECORDER_SESSION_NAME_MAX			 48U
#define G_T20_RECORDER_META_TEXT_MAX			 96U
#define G_T20_RECORDER_HEARTBEAT_INTERVAL_MS	 1000U

/* --- [Binary Protocol Headers] --- */
#define G_T20_BINARY_MAGIC						 0x54323042UL
#define G_T20_BINARY_VERSION					 1U
#define G_T20_BINARY_HEADER_MAGIC_LEN			 4U
#define G_T20_BINARY_HEADER_RESERVED_BYTES		 8U

/* --- [Recorder State Pipeline (Full)] --- */
#define G_T20_RECORDER_FILE_WRITE_STATE_IDLE	 0U
#define G_T20_RECORDER_FILE_WRITE_STATE_READY	 1U
#define G_T20_RECORDER_FILE_WRITE_STATE_DONE	 2U

#define G_T20_RECORDER_STORE_BUNDLE_STATE_IDLE	 0U
#define G_T20_RECORDER_STORE_BUNDLE_STATE_READY	 1U
#define G_T20_RECORDER_STORE_BUNDLE_STATE_DONE	 2U
#define G_T20_RECORDER_STORE_RESULT_NONE		 0U
#define G_T20_RECORDER_STORE_RESULT_OK			 1U
#define G_T20_RECORDER_STORE_RESULT_FAIL		 2U

#define G_T20_RECORDER_FINALIZE_PIPELINE_IDLE	 0U
#define G_T20_RECORDER_FINALIZE_PIPELINE_READY	 1U
#define G_T20_RECORDER_FINALIZE_PIPELINE_EXEC	 2U
#define G_T20_RECORDER_FINALIZE_PIPELINE_DONE	 3U

#define G_T20_RECORDER_MEGA_PIPELINE_STATE_IDLE	 0U
#define G_T20_RECORDER_MEGA_PIPELINE_STATE_READY 1U
#define G_T20_RECORDER_MEGA_PIPELINE_STATE_DONE	 2U

#define G_T20_RECORDER_COMMIT_ROUTE_STATE_IDLE	 0U
#define G_T20_RECORDER_COMMIT_ROUTE_STATE_READY	 1U
#define G_T20_RECORDER_COMMIT_ROUTE_STATE_DONE	 2U

#define G_T20_RECORDER_PATH_ROUTE_STATE_IDLE	 0U
#define G_T20_RECORDER_PATH_ROUTE_STATE_READY	 1U
#define G_T20_RECORDER_PATH_ROUTE_STATE_DONE	 2U

#define G_T20_RECORDER_WRITE_COMMIT_STATE_IDLE	 0U
#define G_T20_RECORDER_WRITE_COMMIT_STATE_READY	 1U
#define G_T20_RECORDER_WRITE_COMMIT_STATE_DONE	 2U

#define G_T20_RECORDER_FINALIZE_STATE_IDLE		 0U
#define G_T20_RECORDER_FINALIZE_STATE_PENDING	 1U
#define G_T20_RECORDER_FINALIZE_STATE_DONE		 2U
#define G_T20_RECORDER_FINALIZE_STATE_SAVED		 3U

#define G_T20_RECORDER_FINALIZE_RESULT_NONE		 0U
#define G_T20_RECORDER_FINALIZE_RESULT_OK		 1U
#define G_T20_RECORDER_FINALIZE_RESULT_FAIL		 2U

#define G_T20_RECORDER_ARTIFACT_STATE_IDLE		 0U
#define G_T20_RECORDER_ARTIFACT_STATE_READY		 1U
#define G_T20_RECORDER_ARTIFACT_STATE_DONE		 2U
#define G_T20_RECORDER_ARTIFACT_RESULT_NONE		 0U
#define G_T20_RECORDER_ARTIFACT_RESULT_OK		 1U
#define G_T20_RECORDER_ARTIFACT_RESULT_FAIL		 2U

#define G_T20_RECORDER_INDEX_STATE_IDLE			 0U
#define G_T20_RECORDER_INDEX_STATE_READY		 1U
#define G_T20_RECORDER_INDEX_STATE_DONE			 2U

#define G_T20_RECORDER_META_SYNC_STATE_IDLE		 0U
#define G_T20_RECORDER_META_SYNC_STATE_READY	 1U
#define G_T20_RECORDER_META_SYNC_STATE_DONE		 2U

#define G_T20_RECORDER_REPORT_SYNC_STATE_IDLE	 0U
#define G_T20_RECORDER_REPORT_SYNC_STATE_READY	 1U
#define G_T20_RECORDER_REPORT_SYNC_STATE_DONE	 2U

#define G_T20_RECORDER_CLEANUP_STATE_IDLE		 0U
#define G_T20_RECORDER_CLEANUP_STATE_READY		 1U
#define G_T20_RECORDER_CLEANUP_STATE_DONE		 2U

/* --- [Enums & Structures] --- */
enum EM_T20_OutputMode_t {
	EN_T20_OUTPUT_VECTOR = 0,
	EN_T20_OUTPUT_SEQUENCE
};
enum EM_T20_StorageBackend_t {
	EN_T20_STORAGE_LITTLEFS = 0,
	EN_T20_STORAGE_SDMMC
};

typedef struct {
	EM_T20_OutputMode_t output_mode;
	uint16_t			sequence_frames;
	bool				sequence_flatten;
} ST_T20_OutputConfig_t;

/* 핵심: 전처리/추출/출력 설정을 통합하는 메인 구조체 */
typedef struct {
	ST_T20_PreprocessConfig_t preprocess;
	ST_T20_FeatureConfig_t	  feature;
	ST_T20_OutputConfig_t	  output;
} ST_T20_Config_t;

typedef struct {
	char	 path[G_T20_RECORDER_FILE_PATH_MAX];
	uint32_t size_bytes;
	uint32_t created_ms;
	uint32_t record_count;
} ST_T20_RecorderIndexItem_t;

typedef struct {
	char	profile_name[G_T20_SDMMC_PROFILE_NAME_MAX];
	bool	use_1bit_mode;
	bool	enabled;
	uint8_t clk_pin;
	uint8_t cmd_pin;
	uint8_t d0_pin;
	uint8_t d1_pin;
	uint8_t d2_pin;
	uint8_t d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t sample_rate_hz;
	uint16_t fft_size;
	uint16_t mfcc_dim;
	uint16_t mel_filters;
	uint16_t sequence_frames;
	uint32_t record_count;
	char	 reserved[G_T20_BINARY_HEADER_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;

typedef struct {
	uint32_t frame_id;
	uint16_t vector_len;
	float	 vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_RecorderVectorMessage_t;
