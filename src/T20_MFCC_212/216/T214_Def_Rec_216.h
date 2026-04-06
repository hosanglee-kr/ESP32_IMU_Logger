/* ============================================================================
 * File: T214_Def_Rec_216.h
 * Summary: SD_MMC Data Logging & Storage Backend Definitions
 * ========================================================================== */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "T210_Def_Com_216.h"
#include "T212_Def_Sens_216.h"

// ----------------------------------------------------------------------------
// 1. Enum Definitions
// ----------------------------------------------------------------------------
typedef enum {
    EN_T20_OUTPUT_VECTOR = 0,
    EN_T20_OUTPUT_SEQUENCE
} EM_T20_OutputMode_t;

typedef enum {
    EN_T20_STORAGE_LITTLEFS = 0,
    EN_T20_STORAGE_SDMMC
} EM_T20_StorageBackend_t;

// ----------------------------------------------------------------------------
// 2. Struct Definitions (State)
// ----------------------------------------------------------------------------
typedef struct {
    EM_T20_State_t file_io;      
    EM_T20_State_t bundle_map;   
    EM_T20_State_t finalize;     
    EM_T20_State_t sync_report;  
    EM_T20_State_t audit_trail;  
} ST_T20_RecorderRuntimeState_t;

typedef struct {
    EM_T20_State_t storage;    
    EM_T20_State_t file_io;    
    EM_T20_State_t write;      
    EM_T20_State_t sync;       
    EM_T20_State_t audit;      
    EM_T20_State_t pipeline;   
} ST_T20_RecorderState_t;

// ----------------------------------------------------------------------------
// 3. Struct Definitions (Config & Protocol)
// ----------------------------------------------------------------------------
typedef struct {
    EM_T20_OutputMode_t output_mode;
    uint16_t sequence_frames;
    bool sequence_flatten;
} ST_T20_OutputConfig_t;

typedef struct {
    bool auto_start;        
    uint8_t button_pin;     
} ST_T20_SystemConfig_t; 

typedef struct {
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_ConfigSensor_t     sensor; // 가속도/자이로 축 및 측정범위 설정
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
    ST_T20_SystemConfig_t     system;
} ST_T20_Config_t;

typedef struct {
    char profile_name[T20::C10_Rec::SDMMC_PROFILE_NAME_MAX];
    bool use_1bit_mode;
    bool enabled;
    uint8_t clk_pin;
    uint8_t cmd_pin;
    uint8_t d0_pin;
    uint8_t d1_pin;
    uint8_t d2_pin;
    uint8_t d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct {
    char path[T20::C10_Rec::FILE_PATH_MAX];
    uint32_t size_bytes;
    uint32_t created_ms;
    uint32_t record_count;
} ST_T20_RecorderIndexItem_t;

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
    char reserved[T20::C10_Rec::BIN_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;

typedef struct {
    uint32_t frame_id;
    uint16_t vector_len;
    float vector[T20::C10_DSP::FEATURE_DIM_MAX];
} ST_T20_RecorderVectorMessage_t;

// ----------------------------------------------------------------------------
// 4. Debug Helper
// ----------------------------------------------------------------------------
static inline void T20_Recorder_DebugState(ST_T20_RecorderState_t* s) {
    if (s == nullptr) return;
    printf("[STORAGE:%s IO:%s WRITE:%s SYNC:%s AUDIT:%s PIPE:%s]\n",
        T20_StateToString(s->storage),
        T20_StateToString(s->file_io),
        T20_StateToString(s->write),
        T20_StateToString(s->sync),
        T20_StateToString(s->audit),
        T20_StateToString(s->pipeline)
    );
}

