/* ============================================================================
 * File: T221_Mfcc_Inter_217.h
 * Summary: v217 통합 내부 구현체 (엔진 조립)
 * ========================================================================== */
#pragma once

#include "T231_Dsp_Pipeline_217.h"
#include "T232_Sensor_Engine_217.h"
#include "T234_Storage_Service_217.h"
#include "T240_Comm_Service_217.h"

struct CL_T20_Mfcc::ST_Impl {
    // [핵심 엔진 객체화]
    CL_T20_SensorEngine   sensor;
    CL_T20_DspPipeline    dsp;
    CL_T20_StorageService storage;
    CL_T20_CommService    comm;

    // RTOS 자원
    TaskHandle_t      sensor_task;
    TaskHandle_t      process_task;
    QueueHandle_t     frame_queue;
    SemaphoreHandle_t mutex;

    ST_T20_Config_t   cfg;
    bool              running = false;

    // 생성자에서 SPI 버스 주입 및 초기화
    ST_Impl() : sensor(FSPI), comm() {
        frame_queue = xQueueCreate(T20::C10_Sys::QUEUE_LEN, sizeof(uint8_t));
        mutex = xSemaphoreCreateMutex();
    }
};

