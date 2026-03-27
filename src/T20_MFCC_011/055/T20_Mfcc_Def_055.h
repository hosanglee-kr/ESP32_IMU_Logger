#pragma once

#include <Arduino.h>


typedef struct
{
    bool  enable;
    EM_T20_PreprocessStageType_t stage_type;
    float param_1;
    float param_2;
    float q_factor;
    float reserved_1;
    float reserved_2;
} ST_T20_PreprocessStageConfig_t;

typedef struct
{
    uint16_t stage_count;
    ST_T20_PreprocessStageConfig_t stages[G_T20_PREPROCESS_STAGE_MAX];
} ST_T20_PreprocessPipelineConfig_t;


/*
 * ============================================================================
 * [구버전 호환 전처리 구조체]
 * ----------------------------------------------------------------------------
 * - 최신 pipeline 방식과 함께 구버전 cpp가 직접 참조하는 필드를 유지한다.
 * - 실제 동작은 pipeline을 기준으로 하되, parse/apply 단계의 호환성을 위해 둔다.
 * ============================================================================
 */
typedef struct
{
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;
/*
 * ============================================================================
 * [컴파일 고정 상수]
 * ----------------------------------------------------------------------------
 * - 혼합 리비전 소스가 참조하는 핵심 매크로를 한 곳에서 고정한다.
 * - 구조체 정의 전에 필요한 항목은 반드시 이 블록에 둔다.
 * ============================================================================
 */
#ifndef G_T20_BINARY_HEADER_MAGIC_LEN
#define G_T20_BINARY_HEADER_MAGIC_LEN         4U

#endif
#ifndef G_T20_BINARY_HEADER_RESERVED_BYTES
#define G_T20_BINARY_HEADER_RESERVED_BYTES    8U

#endif
#ifndef G_T20_BINARY_MAGIC
#define G_T20_BINARY_MAGIC                    0x54323042UL
#endif
#ifndef G_T20_BINARY_VERSION
#define G_T20_BINARY_VERSION                  1U
#endif
#ifndef G_T20_RECORDER_DMA_ALIGN_BYTES
#define G_T20_RECORDER_DMA_ALIGN_BYTES        32U
#endif
#ifndef G_T20_RECORDER_BATCH_COUNT
#define G_T20_RECORDER_BATCH_COUNT            16U

#endif
#ifndef G_T20_RECORDER_BATCH_BUFFER_MAX
#define G_T20_RECORDER_BATCH_BUFFER_MAX       G_T20_RECORDER_BATCH_COUNT
#endif
#ifndef G_T20_ZERO_COPY_STAGE_BUFFER_BYTES
#define G_T20_ZERO_COPY_STAGE_BUFFER_BYTES    4096U
#endif
#ifndef G_T20_SDMMC_BOARD_HINT_MAX
#define G_T20_SDMMC_BOARD_HINT_MAX            32U
#endif
#ifndef G_T20_SDMMC_MOUNT_PATH_DEFAULT
#define G_T20_SDMMC_MOUNT_PATH_DEFAULT        "/sdcard"
#endif
#ifndef G_T20_RECORDER_BATCH_ACCUM_WATERMARK
#define G_T20_RECORDER_BATCH_ACCUM_WATERMARK  8U
#endif
#ifndef G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS
#define G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS 300U
#endif
#ifndef G_T20_RECORDER_ZERO_COPY_SLOT_MAX
#define G_T20_RECORDER_ZERO_COPY_SLOT_MAX     2U
#endif
#ifndef G_T20_CSV_SERVER_MAX_COL_FILTERS
#define G_T20_CSV_SERVER_MAX_COL_FILTERS      8U
#endif
#ifndef G_T20_RECORDER_RENDER_SYNC_SERIES_MAX
#define G_T20_RECORDER_RENDER_SYNC_SERIES_MAX 4U
#endif
#ifndef G_T20_RENDER_SELECTION_SYNC_MAX
#define G_T20_RENDER_SELECTION_SYNC_MAX       4U
#endif
#ifndef G_T20_VIEWER_EVENT_MAX
#define G_T20_VIEWER_EVENT_MAX                16U
#endif
#ifndef G_T20_VIEWER_RECENT_WAVE_COUNT
#define G_T20_VIEWER_RECENT_WAVE_COUNT        4U
#endif
#ifndef G_T20_RECORDER_FLUSH_INTERVAL_MS
#define G_T20_RECORDER_FLUSH_INTERVAL_MS      1000U
#endif
#ifndef G_T20_SDMMC_PROFILE_NAME_MAX
#define G_T20_SDMMC_PROFILE_NAME_MAX          32U
#endif
#ifndef G_T20_TYPE_META_PREVIEW_LINK_MAX
#define G_T20_TYPE_META_PREVIEW_LINK_MAX      16U
#endif
#ifndef G_T20_CSV_SORT_ASC
#define G_T20_CSV_SORT_ASC                       0U
#endif
#ifndef G_T20_CSV_SORT_DESC
#define G_T20_CSV_SORT_DESC                      1U
#endif
#ifndef G_T20_TEXT_PREVIEW_LINE_BUF_SIZE
#define G_T20_TEXT_PREVIEW_LINE_BUF_SIZE      256U
#endif


/*
 * ============================================================================
 * [컴파일 호환 보강 상수]
 * ----------------------------------------------------------------------------
 * - 이전/혼합 리비전 cpp에서 참조하는 상수들을 여기서 먼저 고정한다.
 * - 구조체 정의보다 먼저 선언해야 하는 항목을 상단으로 승격했다.
 * ============================================================================
 */
#ifndef G_T20_BINARY_HEADER_MAGIC_LEN
#endif

#ifndef G_T20_BINARY_HEADER_RESERVED_BYTES
#endif

#ifndef G_T20_BINARY_MAGIC
#define G_T20_BINARY_MAGIC                    0x54323042UL
#endif

#ifndef G_T20_BINARY_VERSION
#define G_T20_BINARY_VERSION                  1U
#endif

#ifndef G_T20_RECORDER_DMA_ALIGN_BYTES
#define G_T20_RECORDER_DMA_ALIGN_BYTES        32U
#endif

#ifndef G_T20_RECORDER_BATCH_BUFFER_MAX
#define G_T20_RECORDER_BATCH_BUFFER_MAX       G_T20_RECORDER_BATCH_COUNT
#endif

#ifndef G_T20_ZERO_COPY_STAGE_BUFFER_BYTES
#define G_T20_ZERO_COPY_STAGE_BUFFER_BYTES    4096U
#endif

#ifndef G_T20_SDMMC_BOARD_HINT_MAX
#define G_T20_SDMMC_BOARD_HINT_MAX            32U
#endif

#ifndef G_T20_SDMMC_MOUNT_PATH_DEFAULT
#define G_T20_SDMMC_MOUNT_PATH_DEFAULT        "/sdcard"
#endif

#ifndef G_T20_RECORDER_BATCH_ACCUM_WATERMARK
#define G_T20_RECORDER_BATCH_ACCUM_WATERMARK  8U
#endif

#ifndef G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS
#define G_T20_RECORDER_BATCH_ACCUM_TIMEOUT_MS 300U
#endif

#ifndef G_T20_RECORDER_ZERO_COPY_SLOT_MAX
#define G_T20_RECORDER_ZERO_COPY_SLOT_MAX     2U
#endif

#ifndef G_T20_VIEWER_RECENT_WAVE_COUNT
#define G_T20_VIEWER_RECENT_WAVE_COUNT        4U
#endif

#ifndef G_T20_VIEWER_EVENT_MAX
#define G_T20_VIEWER_EVENT_MAX                16U
#endif


/* ============================================================================
 * File: T20_Mfcc_Def_055.h
 * Summary:
 *   T20 MFCC / vibration feature module 공개 정의 헤더
 *
 * [이번 단계 반영]
 * - 실제 컴파일 오류 로그 대응 라운드 3 성격의 정의 정리
 * - AsyncWeb 제어/다운로드 대묶음용 공개 상수 추가
 * - 013 기준 실제 컴파일 오류 대응용 정의 정리
 * - recorder 전용 queue/task 지원 상수 추가
 * - binary raw header / record type 정의 추가
 *
 * [향후 단계 TODO]
 * - AsyncWeb 설정/API 연동
 * - live waveform / live feature stream
 * - snapshot별 FFT / mel_bank cache 분리
 * - zero-copy / DMA / cache aligned 고도화
 * ========================================================================== */

#define G_T20_VERSION_STR                   "T20_Mfcc_055"

#define G_T20_RAW_FRAME_BUFFERS             4
#define G_T20_SAMPLE_RING_SIZE              2048
#define G_T20_FFT_SIZE                      256
#define G_T20_SAMPLE_RATE_HZ                1600.0f
#define G_T20_MEL_FILTERS                   26

#define G_T20_MFCC_COEFFS_DEFAULT           13
#define G_T20_MFCC_COEFFS_MAX               32
#define G_T20_DELTA_WINDOW_DEFAULT          2

#define G_T20_PREPROCESS_STAGE_MAX          8

#define G_T20_QUEUE_LEN                     8
#define G_T20_SENSOR_TASK_STACK             6144
#define G_T20_PROCESS_TASK_STACK            12288
#define G_T20_SENSOR_TASK_PRIO              4
#define G_T20_PROCESS_TASK_PRIO             3

#define G_T20_RECORDER_QUEUE_LEN            16
#define G_T20_RECORDER_TASK_STACK           8192
#define G_T20_RECORDER_TASK_PRIO            2
#define G_T20_RECORDER_RAW_MAX_SAMPLES      G_T20_FFT_SIZE
#define G_T20_RECORDER_EVENT_TEXT_MAX       80
#define G_T20_RECORDER_FLUSH_INTERVAL_MS      1000U
#define G_T20_RECORDER_METADATA_INTERVAL_MS   3000U

#define G_T20_WEB_BASE_PATH_MAX              32
#define G_T20_WEB_DEFAULT_BASE_PATH          "/api/t20"
#define G_T20_WEB_FS_MOUNT_PATH              "/t20"
#define G_T20_WEB_STATIC_INDEX_PATH          "/t20/index.html"
#define G_T20_WEB_STATIC_STYLE_PATH          "/t20/t20.css"
#define G_T20_WEB_STATIC_APP_PATH            "/t20/t20.js"
#define G_T20_WEB_STATIC_CACHE_CONTROL       "no-cache"

/*
 * 웹 갱신 주기 기본값(ms)
 * - 정적 페이지 예제에서 상태/특징 조회 polling 기본 간격
 */
#define G_T20_WEB_REFRESH_MS                  1000UL

/*
 * Live 조회용 최대 출력 샘플 수
 * - Web API에서 waveform / sequence 일부를 안전하게 잘라서 내보낼 때 사용
 */
#define G_T20_WEB_LIVE_WAVE_MAX_SAMPLES        256U
#define G_T20_WEB_LIVE_SEQ_MAX_VALUES          (G_T20_SEQUENCE_FRAMES_MAX * G_T20_FEATURE_DIM_MAX)

/*
 * 실시간 push 주기 관련 상수
 * - polling 외에 websocket/SSE 송신 시 사용
 */
#define G_T20_WEB_PUSH_INTERVAL_MS             200UL

/*
 * 실시간 텍스트 버퍼 크기
 * - websocket/SSE JSON 직렬화 임시 버퍼
 */
#define G_T20_WEB_JSON_BUFFER_SIZE             4096U

/*
 * Recorder 재시도/회전 정책
 * - 현재 단계에서는 안정성 우선 값으로 고정
 * - 향후 설정 구조체로 승격 가능
 */
#define G_T20_RECORDER_WRITE_RETRY_MAX        3
#define G_T20_RECORDER_ROTATE_SIZE_BYTES      (8UL * 1024UL * 1024UL)
#define G_T20_RECORDER_ROTATE_INDEX_MAX       999U
#define G_T20_WEB_JSON_DOC_HINT              1024
#define G_T20_WEB_ENABLE_DEFAULT             1

#define G_T20_MFCC_HISTORY                  5
#define G_T20_NOISE_MIN_FRAMES              8

#define G_T20_SEQUENCE_FRAMES_MAX           16
#define G_T20_SEQUENCE_FRAMES_DEFAULT       8

#define G_T20_FEATURE_DIM_DEFAULT           (G_T20_MFCC_COEFFS_DEFAULT * 3)
#define G_T20_FEATURE_DIM_MAX               (G_T20_MFCC_COEFFS_MAX * 3)

#define G_T20_BUTTON_DEBOUNCE_MS            30U
#define G_T20_BUTTON_LONG_PRESS_MS          1000U

#define G_T20_RECORDER_MAX_SESSION_NAME     48
#define G_T20_RECORDER_MAX_PATH_LEN         128

#define G_T20_RAW_BINARY_MAGIC              0x54323052UL
#define G_T20_RAW_BINARY_HEADER_VERSION     1U
#define G_T20_RECORDER_RECORD_MAGIC         0x54323044UL

#define G_T20_SPI_FREQ_HZ                   10000000UL

#define G_T20_PI                            3.14159265358979323846f
#define G_T20_EPSILON                       1.0e-12f

#define G_T20_PIN_SPI_SCK                   12
#define G_T20_PIN_SPI_MISO                  13
#define G_T20_PIN_SPI_MOSI                  11
#define G_T20_PIN_BMI_CS                    10
#define G_T20_PIN_BMI_INT1                  14
#define G_T20_PIN_BUTTON                    0

enum EM_T20_FilterType_t
{
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
};

enum EM_T20_AxisType_t
{
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
};

enum EM_T20_OutputMode_t
{
    EN_T20_OUTPUT_VECTOR = 0,
    EN_T20_OUTPUT_SEQUENCE
};

enum EM_T20_PreprocessStageType_t
{
    EN_T20_STAGE_NONE = 0,
    EN_T20_STAGE_DC_REMOVE,
    EN_T20_STAGE_PREEMPHASIS,
    EN_T20_STAGE_NOISE_GATE,
    EN_T20_STAGE_BIQUAD_LPF,
    EN_T20_STAGE_BIQUAD_HPF,
    EN_T20_STAGE_BIQUAD_BPF
};

enum EM_T20_SessionState_t
{
    EN_T20_SESSION_IDLE = 0,
    EN_T20_SESSION_READY,
    EN_T20_SESSION_RECORDING,
    EN_T20_SESSION_STOPPING,
    EN_T20_SESSION_ERROR
};

enum EM_T20_ButtonEvent_t
{
    EN_T20_BUTTON_EVENT_NONE = 0,
    EN_T20_BUTTON_EVENT_SHORT_PRESS,
    EN_T20_BUTTON_EVENT_LONG_PRESS
};

enum EM_T20_RecorderRawFormat_t
{
    EN_T20_REC_RAW_NONE = 0,
    EN_T20_REC_RAW_CSV,
    EN_T20_REC_RAW_BINARY
};

enum EM_T20_RecorderMetaFormat_t
{
    EN_T20_REC_META_NONE = 0,
    EN_T20_REC_META_JSONL,
    EN_T20_REC_META_CSV
};

enum EM_T20_RecorderEventFormat_t
{
    EN_T20_REC_EVENT_NONE = 0,
    EN_T20_REC_EVENT_JSONL,
    EN_T20_REC_EVENT_CSV
};

enum EM_T20_RecorderRecordType_t
{
    EN_T20_REC_RECORD_NONE = 0,
    EN_T20_REC_RECORD_EVENT,
    EN_T20_REC_RECORD_RAW_FRAME,
    EN_T20_REC_RECORD_FEATURE
};



typedef struct
{
    bool                enable;
    EM_T20_FilterType_t type;
    float               cutoff_hz_1;
    float               cutoff_hz_2;
    float               q_factor;
} ST_T20_FilterConfig_t;

typedef struct
{
    bool     enable_gate;
    float    gate_threshold_abs;
    bool     enable_spectral_subtract;
    float    spectral_subtract_strength;
    uint16_t noise_learn_frames;
} ST_T20_NoiseConfig_t;

typedef struct
{
    EM_T20_AxisType_t             axis;
    bool                          remove_dc;
    ST_T20_PreEmphasisConfig_t    preemphasis;
    ST_T20_FilterConfig_t         filter;
    ST_T20_NoiseConfig_t          noise;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct
{
    uint16_t fft_size;
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

typedef struct
{
    char     profile_name[G_T20_SDMMC_PROFILE_NAME_MAX];
    bool     use_1bit_mode;
    bool     enabled;
    uint8_t  clk_pin;
    uint8_t  cmd_pin;
    uint8_t  d0_pin;
    uint8_t  d1_pin;
    uint8_t  d2_pin;
    uint8_t  d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct
{
    uint32_t frame_id;
    char     kind[16];
    char     text[G_T20_RECORDER_EVENT_TEXT_MAX];
} ST_T20_ViewerEvent_t;


typedef struct
{
    char     path[G_T20_RECORDER_FILE_PATH_MAX];
    uint32_t size_bytes;
    uint32_t created_ms;
    uint32_t record_count;
} ST_T20_RecorderIndexItem_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint16_t mel_filters;
    uint16_t sequence_frames;
    uint32_t record_count;
    char     reserved[G_T20_BINARY_HEADER_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;

#endif
