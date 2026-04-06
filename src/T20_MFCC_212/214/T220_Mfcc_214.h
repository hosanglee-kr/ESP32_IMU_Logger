
/* ============================================================================
 * File: T220_Mfcc_214.h
 ============================================================================ */

#pragma once

#include <Arduino.h>

#include "T218_Def_Main_214.h"

class CL_T20_Mfcc {
   public:
	struct ST_Impl;

	CL_T20_Mfcc();
	~CL_T20_Mfcc();

	bool begin(const ST_T20_Config_t* p_cfg = nullptr);
	bool start(void);
	void stop(void);

	bool setConfig(const ST_T20_Config_t* p_cfg);
	void getConfig(ST_T20_Config_t* p_cfg_out) const;

	bool getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const;
	bool getLatestVector(float* p_out_vec, uint16_t p_len) const;

	bool isSequenceReady(void) const;
	bool getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const;
	bool getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const;

	bool exportConfigJson(char* p_out_buf, uint16_t p_len) const;
	bool exportConfigSchemaJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerDataJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerWaveformJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerSpectrumJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerEventsJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerSequenceJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerOverviewJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerMultiFrameJson(char* p_out_buf, uint16_t p_len) const;
	bool exportViewerChartBundleJson(char* p_out_buf, uint16_t p_len, uint16_t p_points) const;

	bool exportRecorderManifestJson(char* p_out_buf, uint16_t p_len) const;
	bool exportRecorderIndexJson(char* p_out_buf, uint16_t p_len) const;
	bool exportRecorderPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const;
	bool exportRecorderParsedPreviewJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const;
	bool exportRecorderRangeJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_length) const;
	bool exportRecorderBinaryHeaderJson(char* p_out_buf, uint16_t p_len, const char* p_path) const;
	bool exportRecorderCsvTableJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const;
	bool exportRecorderCsvSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const;
	bool exportRecorderCsvTypeMetaJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes) const;
	bool exportRecorderCsvTableAdvancedJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_bytes,
											const char* p_global_filter, const char* p_col_filters_csv,
											uint16_t p_sort_col, uint16_t p_sort_dir, uint16_t p_page, uint16_t p_page_size) const;
	bool exportRecorderBinaryRecordsJson(char* p_out_buf, uint16_t p_len, const char* p_path, uint32_t p_offset, uint32_t p_limit) const;
	bool exportRecorderBinaryPayloadSchemaJson(char* p_out_buf, uint16_t p_len, const char* p_path) const;

	bool exportRenderSelectionSyncJson(char* p_out_buf, uint16_t p_len) const;
	bool exportTypeMetaPreviewLinkJson(char* p_out_buf, uint16_t p_len) const;

	void printConfig(Stream& p_out) const;
	void printStatus(Stream& p_out) const;
	void printLatest(Stream& p_out) const;
	void printChartSyncStatus(Stream& p_out) const;
	void printRecorderBackendStatus(Stream& p_out) const;
	void printTypeMetaStatus(Stream& p_out) const;
	void printRoadmapTodo(Stream& p_out) const;

   private:
	CL_T20_Mfcc(const CL_T20_Mfcc&)						= delete;
	CL_T20_Mfcc&		  operator=(const CL_T20_Mfcc&) = delete;

	ST_Impl*			  _impl;

	friend void IRAM_ATTR T20_onBmiDrdyISR();
	friend void			  T20_sensorTask(void* p_arg);
	friend void			  T20_processTask(void* p_arg);
	friend void			  T20_recorderTask(void* p_arg);
};
