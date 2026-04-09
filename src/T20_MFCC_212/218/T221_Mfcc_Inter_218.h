/* ============================================================================
 * File: T221_Mfcc_Inter_218.h
 * Summary: v217 통합 내부 구현체 (Pimpl)
 * ========================================================================== */
#pragma once
#include "T220_Mfcc_218.h"
#include "T231_Dsp_Pipeline_218.h"
#include "T232_Sensor_Engine_218.h"
#include "T234_Storage_Service_218.h"
#include "T240_Comm_Service_218.h"

#include "T233_Sequence_Builder_218.h" 


struct CL_T20_Mfcc::ST_Impl {
    CL_T20_SensorEngine   sensor;
    CL_T20_DspPipeline    dsp;
    CL_T20_SequenceBuilder seq_builder;
    CL_T20_StorageService storage;
    CL_T20_CommService    comm;

    // RTOS 자원 (v216의 3-Task 완벽 복원)
    TaskHandle_t      sensor_task = nullptr;
    TaskHandle_t      process_task = nullptr;
    TaskHandle_t      recorder_task = nullptr;
    QueueHandle_t     frame_queue = nullptr;
    QueueHandle_t     recorder_queue = nullptr;
    SemaphoreHandle_t mutex = nullptr;

    // 핑퐁 버퍼 및 제어 상태
    alignas(16) float raw_buffer[T20::C10_Sys::RAW_FRAME_BUFFERS][T20::C10_DSP::FFT_SIZE];
    uint8_t  active_fill_buffer = 0;
    uint16_t active_sample_index = 0;
    
    // Watchdog & Button (v216 복원)
    bool     measurement_active = false;
    uint32_t sample_counter = 0;
    uint32_t last_btn_ms = 0;

    ST_T20_Config_t cfg;
    bool running = false;

    ST_Impl() : sensor(SPI), comm() {}
};
