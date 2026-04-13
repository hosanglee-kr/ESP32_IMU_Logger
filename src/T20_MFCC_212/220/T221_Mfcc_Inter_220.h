/* ============================================================================
 * File: T221_Mfcc_Inter_220.h
 * Summary: 통합 내부 구현체 (Pimpl)
 * ========================================================================== */
#pragma once
#include "T220_Mfcc_220.h"
#include "T231_Dsp_Pipeline_220.h"
#include "T232_Sensor_Engine_220.h"
#include "T233_Sequence_Builder_220.h"
#include "T234_Storage_Service_220.h"
#include "T240_Comm_Service_220.h"

struct CL_T20_Mfcc::ST_Impl {
	CL_T20_SensorEngine	   sensor;
	CL_T20_DspPipeline	   dsp;
	CL_T20_SequenceBuilder seq_builder;
	CL_T20_StorageService  storage;
	CL_T20_CommService	   comm;

	// RTOS 자원
	TaskHandle_t		   sensor_task	  = nullptr;
	TaskHandle_t		   process_task	  = nullptr;
	TaskHandle_t		   recorder_task  = nullptr;
	QueueHandle_t		   frame_queue	  = nullptr;
	QueueHandle_t		   recorder_queue = nullptr;
	SemaphoreHandle_t	   mutex		  = nullptr;

	// 핑퐁 버퍼 및 제어 상태
	// 3축(X,Y,Z) x 핑퐁슬롯(4) x 최대 FFT 사이즈(4096) 대응 버퍼
    // 내부 SRAM의 약 192KB를 점유합니다 (3 * 4 * 4096 * 4 bytes)
    alignas(16) float raw_buffer[3][T20::C10_Sys::RAW_FRAME_BUFFERS][4096];
    
    uint8_t   active_fill_buffer  = 0;
    uint16_t  active_sample_index = 0;
    
	// --- 트리거 및 딥슬립 상태 관리 ---
	uint32_t		last_trigger_ms		= 0;
	uint16_t		trigger_hold_frames = 0;

	// Watchdog & Button
	bool			measurement_active	= false;
	uint32_t		sample_counter		= 0;
	uint32_t		last_btn_ms			= 0;

	ST_T20_Config_t cfg;
	bool			running = false;

	ST_Impl() : sensor(SPI), comm() {
	}
};

