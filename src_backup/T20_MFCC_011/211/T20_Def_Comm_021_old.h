
/* ============================================================================
 * File: T20_Def_Comm_021_old.h

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





// 어디 둬야 되지???
#define G_T20_CFG_PROFILE_COUNT				   4U
#define G_T20_RAW_FRAME_BUFFERS				   4U
#define G_T20_SDMMC_BOARD_HINT_MAX			   32U

#define G_T20_PREPROCESS_STAGE_MAX			   8U




typedef struct
{
    bool						 enable;
    EM_T20_PreprocessStageType_t stage_type;
    float						 param_1;
    float						 param_2;
    float						 q_factor;
    float						 reserved_1;
    float						 reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t					   stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;


enum EM_T20_AxisType_t {
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
};


typedef struct
{
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

enum EM_T20_FilterType_t {
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
};

typedef struct
{
    bool				enable;
    EM_T20_FilterType_t type;
    float				cutoff_hz_1;
    float				cutoff_hz_2;
    float				q_factor;
} ST_T20_FilterConfig_t;

typedef struct
{
    bool	 enable_gate;
    float	 gate_threshold_abs;
    bool	 enable_spectral_subtract;
    float	 spectral_subtract_strength;
    uint16_t noise_learn_frames;
} ST_T20_NoiseConfig_t;

typedef struct
{
    bool						 enable;
    EM_T20_PreprocessStageType_t stage_type;
    float						 param_1;
    float						 param_2;
    float						 q_factor;
    float						 reserved_1;
    float						 reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t					   stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;

typedef struct
{
    EM_T20_AxisType_t				  axis;
    bool							  remove_dc;
    ST_T20_PreEmphasisConfig_t		  preemphasis;
    ST_T20_FilterConfig_t			  filter;
    ST_T20_NoiseConfig_t			  noise;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct
{
    uint16_t fft_size;
    uint16_t frame_size;
    uint16_t hop_size;
    float	 sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

