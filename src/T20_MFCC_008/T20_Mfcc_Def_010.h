#pragma once

/* ============================================================================
 * File: T20_Mfcc_Def_010.h
 * Summary:
 *   T20 MFCC / vibration feature module의 공개 정의 헤더
 *
 *   이 파일은 아래 항목들을 포함한다.
 *   1) 시스템 기본 상수
 *   2) 핀 설정
 *   3) 공개 열거형
 *   4) 공개 설정 구조체
 *   5) 공개 출력 구조체
 *   6) sequence ring buffer 구조체
 *   7) 기본 설정 생성 함수
 *
 * [설계 의도]
 * - 런타임 설정값(default/current)과 메모리 최대 크기(max)를 분리한다.
 * - 임베디드 환경에서는 배열 크기를 컴파일 타임에 고정해야 하므로,
 *   실제 사용 길이와 최대 버퍼 길이를 분리해서 관리한다.
 * - MFCC 계수 수, sequence frame 수, feature dimension은 향후 가변화를
 *   고려한 구조로 설계한다.
 * ========================================================================== */

// ============================================================================
// [기본 상수]
// ============================================================================

/*
 * 모듈 버전 문자열
 * - 로그 출력, 디버깅, 상태 출력 시 사용 가능
 * - 현재 파일명은 010이지만 버전 문자열은 프로젝트 정책에 따라 별도 관리 가능
 */
#define G_T20_VERSION_STR                "T20_Mfcc_009"

/*
 * Raw frame buffer 개수
 * - sensor task가 채우는 프레임 버퍼 개수
 * - process task가 늦을 때 임시로 흡수 가능한 프레임 수 증가
 * - 너무 크게 잡으면 RAM 사용량 증가
 */
#define G_T20_RAW_FRAME_BUFFERS          4

/*
 * FFT 크기
 * - 반드시 2의 거듭제곱 사용 권장
 * - 현재 256 samples
 * - 1600Hz 샘플링 기준 한 프레임 길이 = 256 / 1600 = 0.16초
 */
#define G_T20_FFT_SIZE                   256

/*
 * 기본 샘플링 주파수
 * - BMI270 accel ODR 1600Hz 기준
 * - Nyquist 주파수 = 800Hz
 */
#define G_T20_SAMPLE_RATE_HZ             1600.0f

/*
 * Mel filterbank 개수
 * - MFCC 계산 전 power spectrum을 mel scale로 투영할 때 사용
 * - 일반적으로 mfcc_coeffs보다 크거나 같아야 함
 */
#define G_T20_MEL_FILTERS                26

/*
 * MFCC 계수 개수 관련 정의
 *
 * G_T20_MFCC_COEFFS_DEFAULT
 * - 기본 설정에서 사용하는 MFCC 계수 개수
 * - 예: 13
 *
 * G_T20_MFCC_COEFFS_MAX
 * - 시스템이 허용하는 최대 MFCC 계수 개수
 * - 배열 크기/메모리 크기 결정용
 *
 * [중요]
 * - DEFAULT는 "기본 사용값"
 * - MAX는 "메모리 확보용 최대값"
 * - 둘은 의미가 다르므로 분리 유지
 */
#define G_T20_MFCC_COEFFS_DEFAULT        13
#define G_T20_MFCC_COEFFS_MAX            32

/*
 * Delta 계산 윈도우 기본값
 * - 일반적인 delta 계산식에서 앞뒤 몇 프레임을 참조할지 결정
 * - 예: 2이면 중심 프레임 기준 ±2 프레임 사용
 */
#define G_T20_DELTA_WINDOW               2

/*
 * Frame queue 길이
 * - sensor task -> process task 전달용 queue depth
 * - 너무 작으면 frame drop 가능성 증가
 * - 너무 크면 지연(latency) 증가
 */
#define G_T20_QUEUE_LEN                  4

/*
 * FreeRTOS task stack / priority
 * - 실제 프로젝트에서는 로그, 센서 라이브러리, dsp 사용량에 따라 조정 필요
 */
#define G_T20_SENSOR_TASK_STACK          6144
#define G_T20_PROCESS_TASK_STACK         12288
#define G_T20_SENSOR_TASK_PRIO           4
#define G_T20_PROCESS_TASK_PRIO          3

/*
 * MFCC history 프레임 수
 * - delta / delta-delta 계산용 history depth
 * - 현재 5면 중심 프레임 기준 앞2 뒤2 계산 구조와 잘 맞음
 */
#define G_T20_MFCC_HISTORY               5

/*
 * 노이즈 학습 최소 프레임 수
 * - spectral subtraction에서 초기 noise profile 학습용
 */
#define G_T20_NOISE_MIN_FRAMES           8

/*
 * Sequence frame 개수 관련 정의
 *
 * G_T20_SEQUENCE_FRAMES_DEFAULT
 * - 기본 sequence 길이
 *
 * G_T20_SEQUENCE_FRAMES_MAX
 * - ring buffer 메모리 확보용 최대 frame 수
 */
#define G_T20_SEQUENCE_FRAMES_MAX        16
#define G_T20_SEQUENCE_FRAMES_DEFAULT    8

/*
 * Feature dimension 관련 정의
 *
 * G_T20_FEATURE_DIM_DEFAULT
 * - 기본 MFCC 계수 수 기준 vector dimension
 * - mfcc + delta + delta2 = 13 * 3 = 39
 *
 * G_T20_FEATURE_DIM_MAX
 * - 최대 MFCC 계수 수 기준 최대 vector dimension
 * - 배열 크기 확보용
 */
#define G_T20_FEATURE_DIM_DEFAULT        (G_T20_MFCC_COEFFS_DEFAULT * 3)
#define G_T20_FEATURE_DIM_MAX            (G_T20_MFCC_COEFFS_MAX * 3)

/*
 * BMI270 SPI 통신 속도
 * - 보드/배선 길이/노이즈 환경에 따라 낮춰야 할 수 있음
 */
#define G_T20_SPI_FREQ_HZ                10000000UL

/*
 * 수학 상수
 */
#define G_T20_PI                         3.14159265358979323846f
#define G_T20_EPSILON                    1.0e-12f

// ============================================================================
// [핀 설정]
// ============================================================================

/*
 * BMI270 SPI 핀맵
 * - 실제 보드 연결과 반드시 일치해야 함
 * - ESP32-S3 기준 예시
 */
#define G_T20_PIN_SPI_SCK                12
#define G_T20_PIN_SPI_MISO               13
#define G_T20_PIN_SPI_MOSI               11
#define G_T20_PIN_BMI_CS                 10
#define G_T20_PIN_BMI_INT1               14

// ============================================================================
// [열거형]
// ============================================================================

/*
 * 필터 타입
 * - OFF : 필터 미사용
 * - LPF : 저역통과
 * - HPF : 고역통과
 * - BPF : 대역통과
 */
enum EM_T20_FilterType_t
{
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
};

/*
 * 센서 축 선택
 * - 단일 축 기반 진동 특징 추출용
 */
enum EM_T20_AxisType_t
{
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
};

/*
 * 출력 모드
 * - VECTOR   : 단일 feature vector 출력
 * - SEQUENCE : 최근 N프레임을 시퀀스로 축적하여 출력
 */
enum EM_T20_OutputMode_t
{
    EN_T20_OUTPUT_VECTOR = 0,
    EN_T20_OUTPUT_SEQUENCE
};

// ============================================================================
// [설정 구조체]
// ============================================================================

/*
 * Pre-emphasis 설정
 * - 고주파 성분 강조용
 * - 진동 분석에서는 항상 유리하지 않을 수 있으므로 실험 필요
 */
typedef struct
{
    bool  enable;
    float alpha;
} ST_T20_PreEmphasisConfig_t;

/*
 * IIR biquad 필터 설정
 * - type에 따라 cutoff_hz_1 / cutoff_hz_2 의미가 달라짐
 *
 * LPF/HPF:
 *   cutoff_hz_1 사용
 *
 * BPF:
 *   cutoff_hz_1 = low cut
 *   cutoff_hz_2 = high cut
 */
typedef struct
{
    bool                enable;
    EM_T20_FilterType_t type;
    float               cutoff_hz_1;
    float               cutoff_hz_2;
    float               q_factor;
} ST_T20_FilterConfig_t;

/*
 * 노이즈 제거 설정
 *
 * enable_gate
 * - 절대값 threshold 이하 신호를 0으로 클리핑
 *
 * enable_spectral_subtract
 * - 학습된 noise spectrum을 power spectrum에서 빼는 방식
 */
typedef struct
{
    bool     enable_gate;
    float    gate_threshold_abs;

    bool     enable_spectral_subtract;
    float    spectral_subtract_strength;
    uint16_t noise_learn_frames;
} ST_T20_NoiseConfig_t;

/*
 * 전처리 전체 설정
 * - 축 선택
 * - DC 제거
 * - pre-emphasis
 * - biquad filter
 * - noise 제거
 */
typedef struct
{
    EM_T20_AxisType_t          axis;
    bool                       remove_dc;
    ST_T20_PreEmphasisConfig_t preemphasis;
    ST_T20_FilterConfig_t      filter;
    ST_T20_NoiseConfig_t       noise;
} ST_T20_PreprocessConfig_t;

/*
 * 특징 추출 설정
 *
 * fft_size
 * - 현재 구현은 고정 FFT 크기 정책을 사용할 수 있음
 *
 * sample_rate_hz
 * - 필터 계산, mel filterbank bin mapping 계산에 사용
 *
 * mel_filters
 * - mel filterbank 개수
 *
 * mfcc_coeffs
 * - 최종 추출할 MFCC 개수
 *
 * delta_window
 * - delta 계산 시 앞뒤 참조 길이
 */
typedef struct
{
    uint16_t fft_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t delta_window;
} ST_T20_FeatureConfig_t;

/*
 * 출력 설정
 *
 * output_mode
 * - vector / sequence 선택
 *
 * sequence_frames
 * - sequence 모드에서 몇 프레임을 누적할지 결정
 *
 * sequence_flatten
 * - true  : [T x D]를 1차원 flat 배열로 export
 * - false : 향후 frame-major/non-flat 확장용 여지
 */
typedef struct
{
    EM_T20_OutputMode_t output_mode;
    uint16_t            sequence_frames;
    bool                sequence_flatten;
} ST_T20_OutputConfig_t;

/*
 * 전체 모듈 설정
 */
typedef struct
{
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_FeatureConfig_t    feature;
    ST_T20_OutputConfig_t     output;
} ST_T20_Config_t;

// ============================================================================
// [출력 구조체]
// ============================================================================

/*
 * 최신 특징 벡터 출력 구조체
 *
 * [길이 필드]
 * - 실제 사용된 길이를 함께 보관
 * - 내부 배열은 MAX 크기로 잡고, 유효 길이는 별도 필드로 관리
 *
 * [배열]
 * - log_mel : log mel energy
 * - mfcc    : cepstral coefficients
 * - delta   : 1차 미분 특징
 * - delta2  : 2차 미분 특징
 * - vector  : mfcc + delta + delta2 를 이어붙인 분류기 입력 벡터
 */
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

// ============================================================================
// [시퀀스 ring buffer]
// ============================================================================

/*
 * 최근 N프레임 feature vector 시퀀스 저장용 ring buffer
 *
 * data
 * - [frame][feature_dim] 구조
 *
 * frames
 * - 실제 사용하는 sequence frame 수
 *
 * feature_dim
 * - 실제 feature vector 차원
 *
 * head
 * - 다음 write 위치
 *
 * full
 * - ring이 한 바퀴 이상 돌아서 oldest->newest 순서 export 가능 여부
 */
typedef struct
{
    float    data[G_T20_SEQUENCE_FRAMES_MAX][G_T20_FEATURE_DIM_MAX];
    uint16_t frames;
    uint16_t feature_dim;
    uint16_t head;
    bool     full;
} ST_T20_FeatureRingBuffer_t;

// ============================================================================
// [기본 설정 생성 함수]
// ============================================================================

/*
 * 기본 설정 생성 함수
 *
 * [기본 정책]
 * - Z축 분석
 * - DC 제거 사용
 * - pre-emphasis ON
 * - HPF 사용 (저주파/중력 성분 제거 목적)
 * - spectral subtraction 사용
 * - vector 출력 기본
 *
 * [시간 해석]
 * - FFT 256 / 1600Hz = 160ms
 * - sequence_frames = 8 이면 약 1.28초 구간 표현 가능
 */
static inline ST_T20_Config_t T20_makeDefaultConfig(void)
{
    ST_T20_Config_t cfg;

    cfg.preprocess.axis = EN_T20_AXIS_Z;
    cfg.preprocess.remove_dc = true;

    cfg.preprocess.preemphasis.enable = true;
    cfg.preprocess.preemphasis.alpha  = 0.97f;

    cfg.preprocess.filter.enable      = true;
    cfg.preprocess.filter.type        = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1 = 15.0f;
    cfg.preprocess.filter.cutoff_hz_2 = 250.0f;
    cfg.preprocess.filter.q_factor    = 0.707f;

    cfg.preprocess.noise.enable_gate = true;
    cfg.preprocess.noise.gate_threshold_abs = 0.002f;
    cfg.preprocess.noise.enable_spectral_subtract = true;
    cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    cfg.preprocess.noise.noise_learn_frames = G_T20_NOISE_MIN_FRAMES;

    cfg.feature.fft_size       = G_T20_FFT_SIZE;
    cfg.feature.sample_rate_hz = G_T20_SAMPLE_RATE_HZ;
    cfg.feature.mel_filters    = G_T20_MEL_FILTERS;
    cfg.feature.mfcc_coeffs    = G_T20_MFCC_COEFFS_DEFAULT;
    cfg.feature.delta_window   = G_T20_DELTA_WINDOW;

    cfg.output.output_mode      = EN_T20_OUTPUT_VECTOR;

    // 프레임 길이: 256 / 1600Hz = 0.16초 = 160ms
    // 전체 시퀀스 길이: 8 * 160ms = 약 1.28초
    cfg.output.sequence_frames  = G_T20_SEQUENCE_FRAMES_DEFAULT;
    cfg.output.sequence_flatten = true;

    return cfg;
}
