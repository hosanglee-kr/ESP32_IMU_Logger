#pragma once
#include "T210_Def_Com_217.h"

typedef struct {
    uint16_t fft_size;
    uint16_t hop_size;
    uint16_t mfcc_coeffs;
    uint16_t mel_filters;
} ST_T20_FeatureConfig_t;

typedef struct {
    bool enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

typedef struct {
    ST_T20_PreEmphasisConfig_t preemphasis;
    // 필요한 다른 전처리 설정들...
} ST_T20_PreprocessConfig_t;



// 센서 축 및 측정 범위 (유지)
typedef enum {
    EN_T20_AXIS_ACCEL_X = 0, EN_T20_AXIS_ACCEL_Y, EN_T20_AXIS_ACCEL_Z,
    EN_T20_AXIS_GYRO_X,  EN_T20_AXIS_GYRO_Y,  EN_T20_AXIS_GYRO_Z
} EM_T20_SensorAxis_t;

typedef enum { EN_T20_ACCEL_2G = 0, EN_T20_ACCEL_4G, EN_T20_ACCEL_8G, EN_T20_ACCEL_16G } EM_T20_AccelRange_t;
typedef enum { EN_T20_GYRO_125 = 0, EN_T20_GYRO_250, EN_T20_GYRO_500, EN_T20_GYRO_1000, EN_T20_GYRO_2000 } EM_T20_GyroRange_t;

// DSP 설정 (v217 최적화: 런타임 상태 변수 제외)
typedef struct {
    EM_T20_SensorAxis_t axis;
    EM_T20_AccelRange_t accel_range;
    EM_T20_GyroRange_t  gyro_range;
} ST_T20_ConfigSensor_t;

typedef struct {
    uint32_t frame_id;
    uint16_t vector_len; 
    float    vector[T20::C10_DSP::MFCC_COEFFS_MAX * 3]; // 39차 고정
} ST_T20_FeatureVector_t;

