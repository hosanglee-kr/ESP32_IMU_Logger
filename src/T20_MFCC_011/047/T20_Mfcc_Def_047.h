#pragma once

#include <Arduino.h>

/* ============================================================================
 * File: T20_Mfcc_Def_047.h
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

#define G_T20_VERSION_STR                   "T20_Mfcc_047"

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
#define G_T20_RECORDER_BATCH_COUNT            4
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

typedef struct
{
    EM_T20_AxisType_t axis;
    ST_T20_PreprocessPipelineConfig_t pipeline;
} ST_T20_PreprocessConfig_t;

typedef struct
{
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

typedef struct
{
    EM_T20_OutputMode_t output_mode;
    uint16_t            sequence_frames;
    bool                sequence_flatten;
} ST_T20_OutputConfig_t;

typedef struct
{
    uint8_t  button_pin;
    bool     active_low;
    uint16_t debounce_ms;
    uint16_t long_press_ms;
    bool     short_press_toggle_measurement;
    bool     long_press_reserved;
} ST_T20_ButtonConfig_t;

typedef struct
{
    bool                         enable;
    bool                         save_config_json;
    EM_T20_RecorderRawFormat_t   raw_format;
    EM_T20_RecorderMetaFormat_t  metadata_format;
    EM_T20_RecorderEventFormat_t event_format;
    bool                         write_feature_vector_csv;
    bool                         auto_session_folder;
    uint16_t                     batch_count;
    uint32_t                     flush_interval_ms;
    uint32_t                     metadata_interval_ms;
    char                         root_dir[G_T20_RECORDER_MAX_PATH_LEN];
    char                         session_prefix[G_T20_RECORDER_MAX_SESSION_NAME];
} ST_T20_RecorderConfig_t;

typedef struct
{
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
    ST_T20_ButtonConfig_t     button;
    ST_T20_RecorderConfig_t   recorder;
} ST_T20_Config_t;

typedef struct
{
    uint16_t log_mel_len;
    uint16_t mfcc_len;
    uint16_t delta_len;
    uint16_t delta2_len;
    uint16_t vector_len;

    float log_mel[G_T20_MEL_FILTERS];
    float mfcc[G_T20_MFCC_COEFFS_MAX];
    float delta[G_T20_MFCC_COEFFS_MAX];
    float delta2[G_T20_MFCC_COEFFS_MAX];
    float vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_FeatureVector_t;

typedef struct
{
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;


/*
 * Recorder binary header 임시 표준 구조체
 * - recorder가 향후 binary 포맷으로 저장할 때 사용할 정식 헤더 초안
 * - magic/version/header_size/sample_rate_hz/mfcc_dim 등 최소 메타데이터 포함
 * - 현재 decode API는 이 구조와 일치하는 경우 strict decode 성공으로 판정
 * - TODO: 이후 단계에서 recorder writer와 완전히 동일한 포맷으로 고정
 */
typedef struct
{
    char     magic[G_T20_BINARY_HEADER_MAGIC_LEN];
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;
    uint16_t mel_filters;
    uint16_t sequence_frames;
    uint32_t record_count;
    uint8_t  reserved[G_T20_BINARY_HEADER_RESERVED_BYTES];
} ST_T20_RecorderBinaryHeader_t;


/*
 * Recorder binary payload 레코드 초안 구조체
 * - timestamp_ms : 레코드 시각(ms)
 * - kind         : 레코드 종류(예: vector/status)
 * - value_count  : 뒤따르는 float 값 개수
 *
 * [주의]
 * - 실제 float payload는 가변 길이이므로 본 구조체 뒤에 연속 저장하는 전제
 * - 현재는 preview/decode 및 writer 설계 기준점 제공 목적
 * - TODO: 이후 단계에서 recorder write/read와 완전 동일한 포맷으로 고정
 */
typedef struct
{
    uint32_t timestamp_ms;
    uint16_t kind;
    uint16_t value_count;
} ST_T20_RecorderBinaryRecord_t;

typedef struct
{
    uint32_t magic;
    uint16_t header_version;
    uint16_t reserved_0;
    char     module_version[16];
    uint16_t frame_size;
    uint16_t hop_size;
    float    sample_rate_hz;
    uint16_t axis;
    uint16_t mfcc_coeffs;
    uint16_t mel_filters;
    uint16_t delta_window;
    uint16_t reserved_1;
    uint32_t session_start_ms;
} ST_T20_RawBinaryHeader_t;

typedef struct
{
    uint32_t magic;
    uint16_t record_type;
    uint16_t payload_bytes;
    uint32_t timestamp_ms;
} ST_T20_RecorderRecordHeader_t;

static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.preprocess.axis = EN_T20_AXIS_Z;
    cfg.preprocess.pipeline.stage_count = 4;

    cfg.preprocess.pipeline.stages[0].enable = true;
    cfg.preprocess.pipeline.stages[0].stage_type = EN_T20_STAGE_DC_REMOVE;

    cfg.preprocess.pipeline.stages[1].enable = true;
    cfg.preprocess.pipeline.stages[1].stage_type = EN_T20_STAGE_BIQUAD_HPF;
    cfg.preprocess.pipeline.stages[1].param_1 = 15.0f;
    cfg.preprocess.pipeline.stages[1].q_factor = 0.707f;

    cfg.preprocess.pipeline.stages[2].enable = true;
    cfg.preprocess.pipeline.stages[2].stage_type = EN_T20_STAGE_NOISE_GATE;
    cfg.preprocess.pipeline.stages[2].param_1 = 0.002f;

    cfg.preprocess.pipeline.stages[3].enable = true;
    cfg.preprocess.pipeline.stages[3].stage_type = EN_T20_STAGE_PREEMPHASIS;
    cfg.preprocess.pipeline.stages[3].param_1 = 0.97f;

    cfg.feature.frame_size       = G_T20_FFT_SIZE;
    cfg.feature.hop_size         = G_T20_FFT_SIZE;
    cfg.feature.sample_rate_hz   = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters      = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs      = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window     = G_T20_DELTA_WINDOW_DEFAULT;

    cfg.output.output_mode       = EN_T20_OUTPUT_VECTOR;
    cfg.output.sequence_frames   = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten  = true;

    cfg.button.button_pin                     = G_T20_PIN_BUTTON;
    cfg.button.active_low                     = true;
    cfg.button.debounce_ms                    = G_T20_BUTTON_DEBOUNCE_MS;
    cfg.button.long_press_ms                  = G_T20_BUTTON_LONG_PRESS_MS;
    cfg.button.short_press_toggle_measurement = true;
    cfg.button.long_press_reserved            = true;

    cfg.recorder.enable                   = true;
    cfg.recorder.save_config_json         = true;
    cfg.recorder.raw_format               = EN_T20_REC_RAW_BINARY;
    cfg.recorder.metadata_format          = EN_T20_REC_META_JSONL;
    cfg.recorder.event_format             = EN_T20_REC_EVENT_JSONL;
    cfg.recorder.write_feature_vector_csv = false;
    cfg.recorder.auto_session_folder      = true;
    cfg.recorder.batch_count              = G_T20_RECORDER_BATCH_COUNT;
    cfg.recorder.flush_interval_ms        = G_T20_RECORDER_FLUSH_INTERVAL_MS;
    cfg.recorder.metadata_interval_ms     = G_T20_RECORDER_METADATA_INTERVAL_MS;
    strncpy(cfg.recorder.root_dir, "/t20", sizeof(cfg.recorder.root_dir) - 1);
    strncpy(cfg.recorder.session_prefix, "session", sizeof(cfg.recorder.session_prefix) - 1);

    return cfg;
}

#define G_T20_WEB_PUSH_MIN_INTERVAL_MS      250
#define G_T20_WEB_PUSH_FORCE_INTERVAL_MS    2000
#define G_T20_RECORDER_ROTATE_INDEX_PATH     "/t20_rotate_index.jsonl"
#define G_T20_RECORDER_MAX_ROTATE_LIST       32
#define G_T20_RECORDER_ROTATE_KEEP_FILES      12
#define G_T20_RECORDER_RECOVERY_RETRY_MAX      3
#define G_T20_RECORDER_RECOVERY_RETRY_DELAY_MS 500
#define G_T20_WEB_ROTATE_JSON_BUFFER_SIZE      6144
#define G_T20_CFG_RUNTIME_PATH                "/t20_cfg_runtime.json"
#define G_T20_CFG_EXPORT_PATH                 "/t20_cfg_export.json"
#define G_T20_CFG_IMPORT_TMP_PATH             "/t20_cfg_import.tmp"
#define G_T20_CFG_JSON_BUFFER_SIZE            2048
#define G_T20_DSP_CACHE_ENABLE_DEFAULT        1
#define G_T20_DSP_CACHE_MAX_ITEMS             4
#define G_T20_CFG_PROFILE_COUNT               4
#define G_T20_CFG_PROFILE_NAME_LEN            24
#define G_T20_CFG_PROFILE_INDEX_DEFAULT       0
#define G_T20_CFG_PROFILE_BASE_PATH           "/t20_cfg_profile_"
#define G_T20_CFG_PROFILE_SUFFIX              ".json"
#define G_T20_RECORDER_INDEX_PATH           "/t20/recorder/index.json"
#define G_T20_RECORDER_SUMMARY_SUFFIX       ".summary.json"
#define G_T20_RECORDER_META_SUFFIX          ".meta.json"
#define G_T20_RECORDER_MAX_LIST_ITEMS       32
#define G_T20_WEB_JSON_BUF_SIZE             2048
#define G_T20_RECORDER_MANIFEST_PATH        "/t20/recorder/manifest.json"
#define G_T20_RECORDER_VIEWER_JSON_PATH     "/t20/recorder/viewer.json"
#define G_T20_WEB_LARGE_JSON_BUF_SIZE       8192
#define G_T20_JSON_PARSE_TMP_BUF_SIZE       512
#define G_T20_WEB_VIEWER_SEQUENCE_MAX        512
#define G_T20_WEB_DOWNLOAD_PARAM_MAX        128
#define G_T20_WEB_PATH_BUF_SIZE             160
#define G_T20_VIEWER_EVENT_MAX              64
#define G_T20_VIEWER_WAVEFORM_POINTS_MAX    G_T20_FFT_SIZE
#define G_T20_VIEWER_SPECTRUM_POINTS_MAX    ((G_T20_FFT_SIZE / 2) + 1)
#define G_T20_RECORDER_PAGE_SIZE_DEFAULT    10
#define G_T20_RECORDER_PAGE_SIZE_MAX        50
#define G_T20_WEB_RANGE_BUF_SIZE              4096
#define G_T20_RECORDER_TAIL_BYTES_DEFAULT     1024
#define G_T20_RECORDER_TAIL_BYTES_MAX         8192
#define G_T20_RECORDER_RANGE_BYTES_DEFAULT    1024
#define G_T20_RECORDER_RANGE_BYTES_MAX        8192
#define G_T20_VIEWER_MULTI_FRAMES_MAX        4
#define G_T20_VIEWER_CHART_POINTS_MAX        G_T20_FFT_SIZE
#define G_T20_RECORDER_STREAM_CHUNK_SIZE     1024
#define G_T20_HOP_SIZE_DEFAULT               0
#define G_T20_HOP_SIZE_MAX                   G_T20_FFT_SIZE
#define G_T20_PREVIEW_TEXT_BYTES_DEFAULT     512
#define G_T20_PREVIEW_TEXT_BYTES_MAX         4096
#define G_T20_FRAME_ACCUM_CAPACITY           (G_T20_FFT_SIZE * 2)
#define G_T20_PREVIEW_LINES_MAX              32
#define G_T20_CHART_DOWNSAMPLE_POINTS_MAX      128
#define G_T20_BINARY_HEADER_PREVIEW_BYTES      64
#define G_T20_HTTP_RANGE_DEFAULT_LENGTH        1024
#define G_T20_HTTP_RANGE_MAX_LENGTH            8192
#define G_T20_HTTP_STREAM_CHUNK_SIZE           1024
#define G_T20_BINARY_HEURISTIC_HEADER_BYTES    32
#define G_T20_CANVAS_WIDTH_DEFAULT             640
#define G_T20_CANVAS_HEIGHT_DEFAULT            220
#define G_T20_CSV_PREVIEW_COLUMNS_MAX          16
#define G_T20_CHART_OVERLAY_SERIES_MAX        4
#define G_T20_BINARY_HEADER_MAGIC_LEN         4
#define G_T20_CSV_PREVIEW_ROWS_MAX             24
#define G_T20_BINARY_HEADER_VERSION_SUPPORTED  1
#define G_T20_BINARY_HEADER_RESERVED_BYTES     8
#define G_T20_CSV_TABLE_PAGE_SIZE_DEFAULT      10
#define G_T20_CSV_TABLE_PAGE_SIZE_MAX          50
#define G_T20_BINARY_RECORD_PREVIEW_MAX        32
#define G_T20_CHART_SELECTION_MIN_POINTS       8
#define G_T20_BINARY_RECORD_KIND_VECTOR        1
#define G_T20_BINARY_RECORD_KIND_STATUS        2
#define G_T20_CSV_MAX_COLUMN_FILTERS          8
#define G_T20_CSV_FILTER_TEXT_MAX            32
#define G_T20_CHART_SYNC_VIEW_DEFAULT        1
#define G_T20_RECORDER_BATCH_MAX_RECORDS       8
#define G_T20_RECORDER_FILE_PATH_MAX          128
#define G_T20_CSV_SERVER_MAX_ROWS             200

#define G_T20_RECORDER_BATCH_SAMPLES        1024
#define G_T20_RECORDER_FLUSH_INTERVAL_MS    200
#define G_T20_RECORDER_BLOCK_ALIGN_BYTES    32

#define G_T20_DSP_TWIDDLE_CACHE_MAX_ITEMS   4
#define G_T20_JSON_SECTION_BUF_SIZE         256
#define G_T20_JSON_ARRAY_BUF_SIZE           128


/*
 * recorder 상태 기계
 * - IDLE      : 미준비
 * - OPEN      : 파일 열림 / header 작성됨
 * - RECORDING : payload 기록 중
 * - STOPPING  : 종료/flush 진행 중
 */
enum EM_T20_RecorderState_t
{
    EN_T20_RECORDER_IDLE = 0,
    EN_T20_RECORDER_OPEN,
    EN_T20_RECORDER_RECORDING,
    EN_T20_RECORDER_STOPPING
};


/*
 * recorder queue용 vector message
 * - process task에서 recorder task로 넘기는 단위 메시지
 * - 고정 최대 길이 버퍼를 사용하고 실제 길이는 vector_len으로 관리
 */
typedef struct
{
    uint32_t timestamp_ms;
    uint16_t vector_len;
    float    vector[G_T20_FEATURE_DIM_MAX];
} ST_T20_RecorderVectorMessage_t;


/*
 * 저장 백엔드 종류
 * - LITTLEFS : preview/내장 flash 저장
 * - SDMMC    : 향후 대용량 기록용 SD_MMC 경로
 */
enum EM_T20_StorageBackend_t
{
    EN_T20_STORAGE_LITTLEFS = 0,
    EN_T20_STORAGE_SDMMC
};
