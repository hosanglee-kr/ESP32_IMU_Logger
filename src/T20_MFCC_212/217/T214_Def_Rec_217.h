#pragma once
#include "T212_Def_Sens_217.h"
#include "T217_Def_Net_217.h"

typedef struct {
    ST_T20_ConfigSensor_t  sensor;
    ST_T20_ConfigWiFi_t    wifi;
    struct {
        uint16_t hop_size;
        uint16_t mfcc_coeffs;
    } feature;
    struct {
        bool     enabled;
        uint16_t rotate_keep;
    } recorder;
} ST_T20_Config_t;

// 바이너리 헤더 규격 (재활용)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint32_t record_count;
} ST_T20_RecorderBinaryHeader_t;

