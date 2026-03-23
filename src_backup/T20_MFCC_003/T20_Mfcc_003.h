#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "esp_dsp.h"

/*
===============================================================================
소스명: T20_Mfcc_003.ino
버전: v003

[기능 명세 (Specification)]
1. 센서 인터페이스
   - BMI270를 SPI로 연결
   - BMI270 Data Ready 인터럽트(INT1) 기반 샘플 수집
   - 목표 샘플링 주파수: 1.6kHz

2. RTOS 구조
   - FreeRTOS 기반 2-Task 구조
   - Sensor Task:
       * DRDY 인터럽트 통지 수신
       * 샘플 1개 읽기
       * 이중 버퍼(Double Buffer)에 누적
       * 한 프레임 완료 시 Processing Task로 전달
   - Processing Task:
       * 전처리 / 필터링 / FFT / MFCC / Delta / Delta-Delta 수행
       * 최종 분류기 입력 벡터 생성

3. 전처리 기능
   - DC 제거(평균값 제거)
   - Pre-Emphasis
   - 시간영역 Noise Gate
   - LPF / HPF / BPF 선택형 Biquad 필터
   - Hamming Window

4. 주파수 분석 및 특징 추출
   - FFT Size = 256
   - Power Spectrum 계산
   - Spectral Subtraction 기반 노이즈 제거
   - Mel Filterbank 적용
   - Log Mel Energy 계산
   - DCT-II 기반 MFCC 추출 (기본 13계수)

5. 시계열 특징
   - Delta 계산
   - Delta-Delta 계산
   - 최종 분류기 입력 벡터:
       [MFCC(13) + Delta(13) + DeltaDelta(13)] = 39차원

6. 설정 가능 항목
   - 샘플링 주파수
   - FFT 크기
   - 멜 필터 개수
   - MFCC 계수 수
   - 필터 타입 / 차단주파수 / Q
   - Pre-Emphasis 사용 여부 / 계수
   - Noise Gate 사용 여부 / 임계값
   - Spectral Subtraction 사용 여부 / 강도 / 노이즈 학습 프레임 수
   - 축 선택(X/Y/Z)

[튜닝 가이드 (Tuning Guide)]
1. 샘플링 주파수
   - 1.6kHz 사용 시 Nyquist 한계는 800Hz
   - 진동 분석 대상이 800Hz 이하라면 적합
   - 더 높은 대역 분석이 필요하면 ODR/FFT 구조 재설계 필요

2. FFT 크기
   - 256: 반응성 좋음, 분해능 보통
   - 512: 분해능 향상, 지연 증가
   - 1024: 저주파 분해능은 좋지만 실시간성/메모리 부담 증가
   - ESP-DSP FFT는 미리 init 필요

3. 필터 선택
   - HPF: 중력/저주파 드리프트 제거용
   - LPF: 고주파 잡음 억제용
   - BPF: 특정 진동 대역만 분석할 때 유용
   - BPF는 center frequency + Q 기반으로 동작하므로
     low/high cut을 내부적으로 center/Q로 환산

4. Pre-Emphasis
   - 음성 분야에서는 일반적
   - 진동 분석에서는 항상 필요한 것은 아님
   - 고주파 성분을 강조하고 싶을 때 ON
   - 불필요한 고주파 강조가 생기면 OFF 권장

5. Noise 제거
   - 시간영역 Noise Gate:
       * 아주 작은 진폭 성분 제거
       * 과도하게 높이면 미세 진동까지 사라짐
   - Spectral Subtraction:
       * 정지 상태 또는 저잡음 초기 프레임으로 노이즈 학습
       * 학습 프레임 수가 너무 적으면 부정확
       * subtraction strength가 너무 크면 과제거(왜곡) 발생

6. MFCC / Delta / Delta-Delta
   - MFCC만 써도 기본 특징은 가능
   - Delta를 추가하면 변화율 반영
   - Delta-Delta까지 쓰면 동특성 반영 강화
   - 분류기 입력은 39차원 벡터를 권장

7. RTOS 구조
   - Sensor Task는 가볍게 유지
   - 무거운 FFT/MFCC는 Processing Task에서 처리
   - ISR에서는 플래그/notify만 수행
   - I/O 읽기나 DSP 연산은 ISR 내부에서 수행하지 않음

[참고]
- SparkFun BMI270 Arduino Library의 SPI 시작 및 인터럽트 설정 API를 기준으로 구성
- ESP-DSP의 FFT 및 biquad 계수 생성 API를 기준으로 구성
===============================================================================
*/

// ============================================================================
// [고정 상수 / 매크로]
// ============================================================================

// ----- 핀 설정 -----
#define G_T20_PIN_SPI_SCK              12
#define G_T20_PIN_SPI_MISO             13
#define G_T20_PIN_SPI_MOSI             11
#define G_T20_PIN_BMI_CS               10
#define G_T20_PIN_BMI_INT1             14

// ----- 분석 파라미터 -----
#define G_T20_FFT_SIZE                 256
#define G_T20_SAMPLE_RATE_HZ           1600.0f
#define G_T20_MEL_FILTERS              26
#define G_T20_MFCC_COEFFS              13
#define G_T20_CLASSIFIER_DIM           (G_T20_MFCC_COEFFS * 3)   // 13 + 13 + 13 = 39
#define G_T20_DELTA_WINDOW             2                         // ±2 프레임 회귀

// ----- RTOS -----
#define G_T20_SENSOR_TASK_STACK        6144
#define G_T20_PROCESS_TASK_STACK       12288
#define G_T20_SENSOR_TASK_PRIO         4
#define G_T20_PROCESS_TASK_PRIO        3
#define G_T20_FRAME_QUEUE_LEN          4

// ----- 기타 -----
#define G_T20_PI                       3.14159265358979323846f
#define G_T20_EPSILON                  1.0e-12f
#define G_T20_NOISE_HISTORY            8
#define G_T20_MFCC_HISTORY             5
#define G_T20_SPI_FREQ_HZ              10000000

// ============================================================================
// [열거형 / 타입]
// ============================================================================

enum T20_FilterType
{
    EN_T20_FILTER_OFF = 0,
    EN_T20_FILTER_LPF,
    EN_T20_FILTER_HPF,
    EN_T20_FILTER_BPF
};

enum T20_AxisType
{
    EN_T20_AXIS_X = 0,
    EN_T20_AXIS_Y,
    EN_T20_AXIS_Z
};

enum T20_NoiseMode
{
    EN_T20_NOISE_OFF = 0,
    EN_T20_NOISE_GATE_ONLY,
    EN_T20_NOISE_SPECTRAL_SUBTRACT,
    EN_T20_NOISE_BOTH
};

typedef struct
{
    bool     enable;
    float    alpha;      // 보통 0.95 ~ 0.98
} T20_PreEmphasisConfig_t;

typedef struct
{
    T20_FilterType type;
    float          cutoff_hz_1;   // LPF/HPF cutoff, BPF low cut
    float          cutoff_hz_2;   // BPF high cut
    float          q_factor;      // LPF/HPF/BPF용
    bool           enable;
} T20_FilterConfig_t;

typedef struct
{
    bool  enable_gate;
    float gate_threshold_abs;      // 절대값 기준 noise gate

    bool  enable_spectral_subtract;
    float spectral_subtract_strength; // 0.0 ~ 2.0 권장
    uint16_t noise_learn_frames;      // 초기 N프레임 노이즈 학습
} T20_NoiseConfig_t;

typedef struct
{
    T20_AxisType axis;
    bool         remove_dc;
    T20_PreEmphasisConfig_t preemphasis;
    T20_FilterConfig_t      filter;
    T20_NoiseConfig_t       noise;
} T20_PreprocessConfig_t;

typedef struct
{
    uint16_t fft_size;
    float    sample_rate_hz;
    uint16_t mel_filters;
    uint16_t mfcc_coeffs;
    uint16_t classifier_dim;
    uint16_t delta_window;
} T20_FeatureConfig_t;

typedef struct
{
    T20_PreprocessConfig_t preprocess;
    T20_FeatureConfig_t    feature;
} T20_Config_t;

typedef struct
{
    uint8_t frame_index;
} T20_FrameMessage_t;

// ============================================================================
// [전역 객체]
// ============================================================================

static BMI270   g_t20_imu;
static SPIClass g_t20_spi(FSPI);

// FreeRTOS
static TaskHandle_t  g_t20_sensor_task_handle  = nullptr;
static TaskHandle_t  g_t20_process_task_handle = nullptr;
static QueueHandle_t g_t20_frame_queue         = nullptr;

// ISR -> SensorTask 통지용
static volatile bool g_t20_drdy_flag = false;

// 이중 버퍼
static float __attribute__((aligned(16))) g_t20_frame_buffer[2][G_T20_FFT_SIZE];
static volatile uint8_t  g_t20_active_fill_buffer = 0;
static volatile uint16_t g_t20_active_sample_index = 0;

// 전처리/연산 버퍼
static float __attribute__((aligned(16))) g_t20_work_frame[G_T20_FFT_SIZE];
static float __attribute__((aligned(16))) g_t20_window[G_T20_FFT_SIZE];
static float __attribute__((aligned(16))) g_t20_fft_buffer[G_T20_FFT_SIZE * 2];
static float __attribute__((aligned(16))) g_t20_power[(G_T20_FFT_SIZE / 2) + 1];
static float __attribute__((aligned(16))) g_t20_noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];
static float __attribute__((aligned(16))) g_t20_log_mel[G_T20_MEL_FILTERS];
static float __attribute__((aligned(16))) g_t20_mfcc_curr[G_T20_MFCC_COEFFS];
static float __attribute__((aligned(16))) g_t20_delta_curr[G_T20_MFCC_COEFFS];
static float __attribute__((aligned(16))) g_t20_delta2_curr[G_T20_MFCC_COEFFS];
static float __attribute__((aligned(16))) g_t20_classifier_vector[G_T20_CLASSIFIER_DIM];

// 멜 필터뱅크
static float __attribute__((aligned(16)))
g_t20_mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

// MFCC 히스토리: delta / delta-delta 계산용
static float __attribute__((aligned(16)))
g_t20_mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS];
static uint16_t g_t20_mfcc_history_count = 0;

// biquad
static float g_t20_biquad_coeffs[5];
static float g_t20_biquad_state[2] = {0.0f, 0.0f};

// 기타 상태
static float g_t20_prev_raw_sample = 0.0f;
static uint16_t g_t20_noise_learned_frames = 0;

// 설정
static T20_Config_t g_t20_cfg =
{
    .preprocess =
    {
        .axis = EN_T20_AXIS_Z,
        .remove_dc = true,
        .preemphasis =
        {
            .enable = true,
            .alpha  = 0.97f
        },
        .filter =
        {
            .type        = EN_T20_FILTER_HPF,
            .cutoff_hz_1 = 15.0f,     // HPF cut
            .cutoff_hz_2 = 250.0f,    // BPF high cut
            .q_factor    = 0.707f,
            .enable      = true
        },
        .noise =
        {
            .enable_gate = true,
            .gate_threshold_abs = 0.002f,
            .enable_spectral_subtract = true,
            .spectral_subtract_strength = 1.0f,
            .noise_learn_frames = 8
        }
    },
    .feature =
    {
        .fft_size       = G_T20_FFT_SIZE,
        .sample_rate_hz = G_T20_SAMPLE_RATE_HZ,
        .mel_filters    = G_T20_MEL_FILTERS,
        .mfcc_coeffs    = G_T20_MFCC_COEFFS,
        .classifier_dim = G_T20_CLASSIFIER_DIM,
        .delta_window   = G_T20_DELTA_WINDOW
    }
};

// ============================================================================
// [함수 선언]
// ============================================================================

// --- ISR / RTOS ---
static void IRAM_ATTR T20_onBmiDrdyISR(void);
static void T20_sensorTask(void* p_arg);
static void T20_processTask(void* p_arg);

// --- 초기화 ---
static bool T20_initSystem(void);
static bool T20_initDSP(void);
static bool T20_initBMI270_SPI(void);
static bool T20_configBMI270_1600Hz_DRDY(void);
static void T20_createTasks(void);

// --- 설정 / 유틸 ---
static void T20_printConfig(void);
static bool T20_configureFilter(const T20_FilterConfig_t* p_cfg);
static float T20_hzToMel(float p_hz);
static float T20_melToHz(float p_mel);

// --- 전처리 ---
static float T20_selectAxisSample(void);
static void T20_applyDCRemove(float* p_data, uint16_t p_len);
static void T20_applyPreEmphasis(float* p_data, uint16_t p_len, float p_alpha);
static void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
static void T20_applyBiquadFilter(const float* p_in, float* p_out, uint16_t p_len);
static void T20_applyWindow(float* p_data, uint16_t p_len);

// --- 스펙트럼 / MFCC ---
static void T20_buildHammingWindow(void);
static void T20_buildMelFilterbank(void);
static void T20_computePowerSpectrum(const float* p_time, float* p_power);
static void T20_learnNoiseSpectrum(const float* p_power);
static void T20_applySpectralSubtraction(float* p_power);
static void T20_applyMelFilterbank(const float* p_power, float* p_log_mel_out);
static void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);
static void T20_computeMFCC(const float* p_frame, float* p_mfcc_out);

// --- Delta / Classifier Vector ---
static void T20_pushMfccHistory(const float* p_mfcc);
static void T20_computeDeltaFromHistory(float* p_delta_out);
static void T20_computeDeltaDeltaFromHistory(float* p_delta2_out);
static void T20_buildClassifierVector(
    const float* p_mfcc,
    const float* p_delta,
    const float* p_delta2,
    float* p_out_vec);

// --- 디버그 / 출력 ---
static void T20_printMfccAndVector(
    const float* p_mfcc,
    const float* p_delta,
    const float* p_delta2,
    const float* p_classifier_vec);

// ============================================================================
// [setup / loop]
// ============================================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=================================================");
    Serial.println("T20_Mfcc_003 start");
    Serial.println("=================================================");

    if (!T20_initSystem()) {
        Serial.println("System init failed.");
        while (1) { delay(1000); }
    }

    T20_printConfig();
    T20_createTasks();

    Serial.println("System ready.");
}

void loop()
{
    // 실제 처리는 FreeRTOS Task에서 수행
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ============================================================================
// [ISR / RTOS]
// ============================================================================

static void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    BaseType_t v_hp_task_woken = pdFALSE;

    g_t20_drdy_flag = true;

    if (g_t20_sensor_task_handle != nullptr) {
        vTaskNotifyGiveFromISR(g_t20_sensor_task_handle, &v_hp_task_woken);
    }

    if (v_hp_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void T20_sensorTask(void* p_arg)
{
    (void)p_arg;

    for (;;) {
        // DRDY 인터럽트 통지 대기
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!g_t20_drdy_flag) {
            continue;
        }
        g_t20_drdy_flag = false;

        // 센서 데이터 읽기
        if (g_t20_imu.getSensorData() != BMI2_OK) {
            continue;
        }

        float v_sample = T20_selectAxisSample();

        // 이중 버퍼에 샘플 적재
        uint8_t  v_fill_buf = g_t20_active_fill_buffer;
        uint16_t v_idx      = g_t20_active_sample_index;

        if (v_idx < G_T20_FFT_SIZE) {
            g_t20_frame_buffer[v_fill_buf][v_idx] = v_sample;
            v_idx++;
            g_t20_active_sample_index = v_idx;
        }

        // 프레임 완성 시:
        // 1) 현재 채운 버퍼를 처리 큐로 전달
        // 2) 다음 버퍼로 전환
        if (v_idx >= G_T20_FFT_SIZE) {
            T20_FrameMessage_t v_msg;
            v_msg.frame_index = v_fill_buf;

            xQueueSend(g_t20_frame_queue, &v_msg, 0);

            g_t20_active_fill_buffer = (uint8_t)((v_fill_buf + 1U) % 2U);
            g_t20_active_sample_index = 0;
        }
    }
}

static void T20_processTask(void* p_arg)
{
    (void)p_arg;

    T20_FrameMessage_t v_msg;

    for (;;) {
        if (xQueueReceive(g_t20_frame_queue, &v_msg, portMAX_DELAY) == pdTRUE) {
            // 처리용 로컬 버퍼로 복사
            memcpy(g_t20_work_frame,
                   g_t20_frame_buffer[v_msg.frame_index],
                   sizeof(float) * G_T20_FFT_SIZE);

            // MFCC 계산
            T20_computeMFCC(g_t20_work_frame, g_t20_mfcc_curr);

            // 히스토리 적재
            T20_pushMfccHistory(g_t20_mfcc_curr);

            // Delta / Delta-Delta 계산
            T20_computeDeltaFromHistory(g_t20_delta_curr);
            T20_computeDeltaDeltaFromHistory(g_t20_delta2_curr);

            // 분류기 입력 벡터 생성
            T20_buildClassifierVector(
                g_t20_mfcc_curr,
                g_t20_delta_curr,
                g_t20_delta2_curr,
                g_t20_classifier_vector
            );

            // 출력
            T20_printMfccAndVector(
                g_t20_mfcc_curr,
                g_t20_delta_curr,
                g_t20_delta2_curr,
                g_t20_classifier_vector
            );
        }
    }
}

// ============================================================================
// [초기화]
// ============================================================================

static bool T20_initSystem(void)
{
    memset(g_t20_noise_spectrum, 0, sizeof(g_t20_noise_spectrum));
    memset(g_t20_mfcc_history,   0, sizeof(g_t20_mfcc_history));
    memset(g_t20_biquad_state,   0, sizeof(g_t20_biquad_state));

    g_t20_spi.begin(
        G_T20_PIN_SPI_SCK,
        G_T20_PIN_SPI_MISO,
        G_T20_PIN_SPI_MOSI,
        G_T20_PIN_BMI_CS
    );

    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);

    pinMode(G_T20_PIN_BMI_INT1, INPUT);

    if (!T20_initDSP()) {
        return false;
    }

    if (!T20_initBMI270_SPI()) {
        return false;
    }

    if (!T20_configBMI270_1600Hz_DRDY()) {
        return false;
    }

    if (!T20_configureFilter(&g_t20_cfg.preprocess.filter)) {
        return false;
    }

    attachInterrupt(
        digitalPinToInterrupt(G_T20_PIN_BMI_INT1),
        T20_onBmiDrdyISR,
        RISING
    );

    return true;
}

static bool T20_initDSP(void)
{
    esp_err_t v_res = dsps_fft2r_init_fc32(NULL, G_T20_FFT_SIZE);
    if (v_res != ESP_OK) {
        Serial.printf("dsps_fft2r_init_fc32 failed: %d\n", (int)v_res);
        return false;
    }

    T20_buildHammingWindow();
    T20_buildMelFilterbank();

    return true;
}

static bool T20_initBMI270_SPI(void)
{
    int8_t v_rslt = g_t20_imu.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, g_t20_spi);
    if (v_rslt != BMI2_OK) {
        Serial.printf("BMI270 beginSPI failed: %d\n", (int)v_rslt);
        return false;
    }

    return true;
}

static bool T20_configBMI270_1600Hz_DRDY(void)
{
    int8_t v_rslt = BMI2_OK;

    // ODR 설정
    v_rslt = g_t20_imu.setAccelODR(BMI2_ACC_ODR_1600HZ);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setAccelODR failed: %d\n", (int)v_rslt);
        return false;
    }

    v_rslt = g_t20_imu.setGyroODR(BMI2_GYR_ODR_1600HZ);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setGyroODR failed: %d\n", (int)v_rslt);
        return false;
    }

    // Power Mode
    v_rslt = g_t20_imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setAccelPowerMode failed: %d\n", (int)v_rslt);
        return false;
    }

    v_rslt = g_t20_imu.setGyroPowerMode(BMI2_PERF_OPT_MODE, BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setGyroPowerMode failed: %d\n", (int)v_rslt);
        return false;
    }

    // 가속도 설정
    bmi2_sens_config v_acc_cfg;
    memset(&v_acc_cfg, 0, sizeof(v_acc_cfg));
    v_acc_cfg.type = BMI2_ACCEL;

    v_rslt = g_t20_imu.getConfig(&v_acc_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("getConfig(ACCEL) failed: %d\n", (int)v_rslt);
        return false;
    }

    v_acc_cfg.cfg.acc.odr         = BMI2_ACC_ODR_1600HZ;
    v_acc_cfg.cfg.acc.range       = BMI2_ACC_RANGE_2G;
    v_acc_cfg.cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    v_acc_cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    v_rslt = g_t20_imu.setConfig(v_acc_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setConfig(ACCEL) failed: %d\n", (int)v_rslt);
        return false;
    }

    // 자이로 설정
    bmi2_sens_config v_gyr_cfg;
    memset(&v_gyr_cfg, 0, sizeof(v_gyr_cfg));
    v_gyr_cfg.type = BMI2_GYRO;

    v_rslt = g_t20_imu.getConfig(&v_gyr_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("getConfig(GYRO) failed: %d\n", (int)v_rslt);
        return false;
    }

    v_gyr_cfg.cfg.gyr.odr         = BMI2_GYR_ODR_1600HZ;
    v_gyr_cfg.cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    v_gyr_cfg.cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    v_gyr_cfg.cfg.gyr.noise_perf  = BMI2_PERF_OPT_MODE;
    v_gyr_cfg.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    v_rslt = g_t20_imu.setConfig(v_gyr_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setConfig(GYRO) failed: %d\n", (int)v_rslt);
        return false;
    }

    // INT1 설정
    bmi2_int_pin_config v_int_cfg;
    memset(&v_int_cfg, 0, sizeof(v_int_cfg));

    v_int_cfg.pin_type = BMI2_INT1;
    v_int_cfg.pin_cfg[0].output_en = BMI2_ENABLE;
    v_int_cfg.pin_cfg[0].od        = BMI2_DISABLE;
    v_int_cfg.pin_cfg[0].lvl       = BMI2_HIGH;
    v_int_cfg.pin_cfg[0].input_en  = BMI2_DISABLE;
    v_int_cfg.pin_cfg[0].int_latch = BMI2_DISABLE;

    v_rslt = g_t20_imu.setInterruptPinConfig(v_int_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setInterruptPinConfig failed: %d\n", (int)v_rslt);
        return false;
    }

    // Data Ready -> INT1 매핑
    v_rslt = g_t20_imu.mapInterruptToPin(BMI2_DRDY_INT, BMI2_INT1);
    if (v_rslt != BMI2_OK) {
        Serial.printf("mapInterruptToPin failed: %d\n", (int)v_rslt);
        return false;
    }

    return true;
}

static void T20_createTasks(void)
{
    g_t20_frame_queue = xQueueCreate(G_T20_FRAME_QUEUE_LEN, sizeof(T20_FrameMessage_t));

    xTaskCreatePinnedToCore(
        T20_sensorTask,
        "T20_SensorTask",
        G_T20_SENSOR_TASK_STACK,
        nullptr,
        G_T20_SENSOR_TASK_PRIO,
        &g_t20_sensor_task_handle,
        0
    );

    xTaskCreatePinnedToCore(
        T20_processTask,
        "T20_ProcessTask",
        G_T20_PROCESS_TASK_STACK,
        nullptr,
        G_T20_PROCESS_TASK_PRIO,
        &g_t20_process_task_handle,
        1
    );
}

// ============================================================================
// [설정 / 유틸]
// ============================================================================

static void T20_printConfig(void)
{
    Serial.println("------------- Config -------------");
    Serial.printf("SampleRate      : %.1f Hz\n", g_t20_cfg.feature.sample_rate_hz);
    Serial.printf("FFT Size        : %u\n",   g_t20_cfg.feature.fft_size);
    Serial.printf("Mel Filters     : %u\n",   g_t20_cfg.feature.mel_filters);
    Serial.printf("MFCC Coeffs     : %u\n",   g_t20_cfg.feature.mfcc_coeffs);
    Serial.printf("Classifier Dim  : %u\n",   g_t20_cfg.feature.classifier_dim);
    Serial.printf("Delta Window    : %u\n",   g_t20_cfg.feature.delta_window);
    Serial.printf("Remove DC       : %s\n",   g_t20_cfg.preprocess.remove_dc ? "ON" : "OFF");
    Serial.printf("PreEmphasis     : %s\n",   g_t20_cfg.preprocess.preemphasis.enable ? "ON" : "OFF");
    Serial.printf("Noise Gate      : %s\n",   g_t20_cfg.preprocess.noise.enable_gate ? "ON" : "OFF");
    Serial.printf("SpectralSub     : %s\n",   g_t20_cfg.preprocess.noise.enable_spectral_subtract ? "ON" : "OFF");
    Serial.printf("Filter Enable   : %s\n",   g_t20_cfg.preprocess.filter.enable ? "ON" : "OFF");
    Serial.println("----------------------------------");
}

static bool T20_configureFilter(const T20_FilterConfig_t* p_cfg)
{
    if (p_cfg == nullptr) {
        return false;
    }

    if (!p_cfg->enable || p_cfg->type == EN_T20_FILTER_OFF) {
        memset(g_t20_biquad_coeffs, 0, sizeof(g_t20_biquad_coeffs));
        memset(g_t20_biquad_state,  0, sizeof(g_t20_biquad_state));
        return true;
    }

    float v_fs = g_t20_cfg.feature.sample_rate_hz;
    float v_q  = (p_cfg->q_factor <= 0.0f) ? 0.707f : p_cfg->q_factor;
    esp_err_t v_res = ESP_OK;

    if (p_cfg->type == EN_T20_FILTER_LPF) {
        float v_norm = p_cfg->cutoff_hz_1 / v_fs;
        v_res = dsps_biquad_gen_lpf_f32(g_t20_biquad_coeffs, v_norm, v_q);
    }
    else if (p_cfg->type == EN_T20_FILTER_HPF) {
        float v_norm = p_cfg->cutoff_hz_1 / v_fs;
        v_res = dsps_biquad_gen_hpf_f32(g_t20_biquad_coeffs, v_norm, v_q);
    }
    else if (p_cfg->type == EN_T20_FILTER_BPF) {
        float v_low  = p_cfg->cutoff_hz_1;
        float v_high = p_cfg->cutoff_hz_2;

        if (v_high <= v_low) {
            Serial.println("BPF config invalid: cutoff_hz_2 must be > cutoff_hz_1");
            return false;
        }

        // BPF는 center frequency + Q 기준으로 환산
        float v_center_hz = sqrtf(v_low * v_high);
        float v_bw_hz     = v_high - v_low;
        float v_q_bpf     = v_center_hz / (v_bw_hz + G_T20_EPSILON);
        float v_center_norm = v_center_hz / v_fs;

        if (v_q_bpf < 0.1f) v_q_bpf = 0.1f;

        v_res = dsps_biquad_gen_bpf_f32(g_t20_biquad_coeffs, v_center_norm, v_q_bpf);
    }

    if (v_res != ESP_OK) {
        Serial.printf("Filter coefficient generation failed: %d\n", (int)v_res);
        return false;
    }

    memset(g_t20_biquad_state, 0, sizeof(g_t20_biquad_state));
    return true;
}

static float T20_hzToMel(float p_hz)
{
    return 2595.0f * log10f(1.0f + (p_hz / 700.0f));
}

static float T20_melToHz(float p_mel)
{
    return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f);
}

// ============================================================================
// [전처리]
// ============================================================================

static float T20_selectAxisSample(void)
{
    switch (g_t20_cfg.preprocess.axis) {
        case EN_T20_AXIS_X: return g_t20_imu.data.accelX;
        case EN_T20_AXIS_Y: return g_t20_imu.data.accelY;
        case EN_T20_AXIS_Z:
        default:
            return g_t20_imu.data.accelZ;
    }
}

static void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    float v_mean = 0.0f;

    for (uint16_t i = 0; i < p_len; i++) {
        v_mean += p_data[i];
    }
    v_mean /= (float)p_len;

    for (uint16_t i = 0; i < p_len; i++) {
        p_data[i] -= v_mean;
    }
}

static void T20_applyPreEmphasis(float* p_data, uint16_t p_len, float p_alpha)
{
    if (p_len == 0) {
        return;
    }

    float v_prev = g_t20_prev_raw_sample;

    for (uint16_t i = 0; i < p_len; i++) {
        float v_cur = p_data[i];
        p_data[i] = v_cur - (p_alpha * v_prev);
        v_prev = v_cur;
    }

    g_t20_prev_raw_sample = v_prev;
}

static void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs)
{
    for (uint16_t i = 0; i < p_len; i++) {
        if (fabsf(p_data[i]) < p_threshold_abs) {
            p_data[i] = 0.0f;
        }
    }
}

static void T20_applyBiquadFilter(const float* p_in, float* p_out, uint16_t p_len)
{
    if (!g_t20_cfg.preprocess.filter.enable ||
        g_t20_cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    dsps_biquad_f32(p_in, p_out, p_len, g_t20_biquad_coeffs, g_t20_biquad_state);
}

static void T20_applyWindow(float* p_data, uint16_t p_len)
{
    for (uint16_t i = 0; i < p_len; i++) {
        p_data[i] *= g_t20_window[i];
    }
}

// ============================================================================
// [스펙트럼 / MFCC]
// ============================================================================

static void T20_buildHammingWindow(void)
{
    for (uint16_t i = 0; i < G_T20_FFT_SIZE; i++) {
        g_t20_window[i] =
            0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)i) / (float)(G_T20_FFT_SIZE - 1));
    }
}

static void T20_buildMelFilterbank(void)
{
    memset(g_t20_mel_bank, 0, sizeof(g_t20_mel_bank));

    const int   v_num_bins = (G_T20_FFT_SIZE / 2) + 1;
    const float v_f_min = 0.0f;
    const float v_f_max = g_t20_cfg.feature.sample_rate_hz * 0.5f;

    const float v_mel_min = T20_hzToMel(v_f_min);
    const float v_mel_max = T20_hzToMel(v_f_max);

    float v_mel_points[G_T20_MEL_FILTERS + 2];
    float v_hz_points[G_T20_MEL_FILTERS + 2];
    int   v_bin_points[G_T20_MEL_FILTERS + 2];

    for (int i = 0; i < G_T20_MEL_FILTERS + 2; i++) {
        float v_ratio = (float)i / (float)(G_T20_MEL_FILTERS + 1);
        v_mel_points[i] = v_mel_min + (v_mel_max - v_mel_min) * v_ratio;
        v_hz_points[i]  = T20_melToHz(v_mel_points[i]);

        int v_bin = (int)floorf(((float)G_T20_FFT_SIZE + 1.0f) * v_hz_points[i] / g_t20_cfg.feature.sample_rate_hz);

        if (v_bin < 0) v_bin = 0;
        if (v_bin >= v_num_bins) v_bin = v_num_bins - 1;
        v_bin_points[i] = v_bin;
    }

    for (int m = 0; m < G_T20_MEL_FILTERS; m++) {
        int v_left   = v_bin_points[m];
        int v_center = v_bin_points[m + 1];
        int v_right  = v_bin_points[m + 2];

        if (v_center <= v_left) {
            v_center = v_left + 1;
        }
        if (v_right <= v_center) {
            v_right = v_center + 1;
        }
        if (v_right >= v_num_bins) {
            v_right = v_num_bins - 1;
        }

        for (int k = v_left; k < v_center; k++) {
            g_t20_mel_bank[m][k] =
                (float)(k - v_left) / (float)(v_center - v_left);
        }

        for (int k = v_center; k <= v_right; k++) {
            g_t20_mel_bank[m][k] =
                (float)(v_right - k) / (float)(v_right - v_center + 1e-6f);
        }
    }
}

static void T20_computePowerSpectrum(const float* p_time, float* p_power)
{
    for (int i = 0; i < G_T20_FFT_SIZE; i++) {
        g_t20_fft_buffer[2 * i + 0] = p_time[i];
        g_t20_fft_buffer[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_f32(g_t20_fft_buffer, G_T20_FFT_SIZE);
    dsps_bit_rev_f32(g_t20_fft_buffer, G_T20_FFT_SIZE);

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); k++) {
        float v_re = g_t20_fft_buffer[2 * k + 0];
        float v_im = g_t20_fft_buffer[2 * k + 1];
        float v_p  = (v_re * v_re + v_im * v_im) / (float)G_T20_FFT_SIZE;

        if (v_p < G_T20_EPSILON) {
            v_p = G_T20_EPSILON;
        }

        p_power[k] = v_p;
    }
}

static void T20_learnNoiseSpectrum(const float* p_power)
{
    if (!g_t20_cfg.preprocess.noise.enable_spectral_subtract) {
        return;
    }

    if (g_t20_noise_learned_frames >= g_t20_cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    uint16_t v_count = g_t20_noise_learned_frames;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); k++) {
        g_t20_noise_spectrum[k] =
            ((g_t20_noise_spectrum[k] * (float)v_count) + p_power[k]) / (float)(v_count + 1U);
    }

    g_t20_noise_learned_frames++;
}

static void T20_applySpectralSubtraction(float* p_power)
{
    if (!g_t20_cfg.preprocess.noise.enable_spectral_subtract) {
        return;
    }

    // 아직 충분한 노이즈 학습 전이면 적용하지 않음
    if (g_t20_noise_learned_frames < g_t20_cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    float v_strength = g_t20_cfg.preprocess.noise.spectral_subtract_strength;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); k++) {
        float v_sub = p_power[k] - (v_strength * g_t20_noise_spectrum[k]);
        if (v_sub < G_T20_EPSILON) {
            v_sub = G_T20_EPSILON;
        }
        p_power[k] = v_sub;
    }
}

static void T20_applyMelFilterbank(const float* p_power, float* p_log_mel_out)
{
    const int v_num_bins = (G_T20_FFT_SIZE / 2) + 1;

    for (int m = 0; m < G_T20_MEL_FILTERS; m++) {
        float v_sum = 0.0f;

        for (int k = 0; k < v_num_bins; k++) {
            v_sum += p_power[k] * g_t20_mel_bank[m][k];
        }

        if (v_sum < G_T20_EPSILON) {
            v_sum = G_T20_EPSILON;
        }

        p_log_mel_out[m] = logf(v_sum);
    }
}

static void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len)
{
    for (uint16_t n = 0; n < p_out_len; n++) {
        float v_sum = 0.0f;

        for (uint16_t k = 0; k < p_in_len; k++) {
            v_sum += p_in[k] * cosf((G_T20_PI / (float)p_in_len) * ((float)k + 0.5f) * (float)n);
        }

        p_out[n] = v_sum;
    }
}

static void T20_computeMFCC(const float* p_frame, float* p_mfcc_out)
{
    float v_temp1[G_T20_FFT_SIZE] __attribute__((aligned(16)));
    float v_temp2[G_T20_FFT_SIZE] __attribute__((aligned(16)));

    memcpy(v_temp1, p_frame, sizeof(v_temp1));

    // ------------------------------------------------------------
    // 1) DC 제거
    // ------------------------------------------------------------
    if (g_t20_cfg.preprocess.remove_dc) {
        T20_applyDCRemove(v_temp1, G_T20_FFT_SIZE);
    }

    // ------------------------------------------------------------
    // 2) Pre-Emphasis
    // ------------------------------------------------------------
    if (g_t20_cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(
            v_temp1,
            G_T20_FFT_SIZE,
            g_t20_cfg.preprocess.preemphasis.alpha
        );
    }

    // ------------------------------------------------------------
    // 3) 시간영역 Noise Gate
    // ------------------------------------------------------------
    if (g_t20_cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(
            v_temp1,
            G_T20_FFT_SIZE,
            g_t20_cfg.preprocess.noise.gate_threshold_abs
        );
    }

    // ------------------------------------------------------------
    // 4) Biquad Filter (LPF / HPF / BPF)
    // ------------------------------------------------------------
    T20_applyBiquadFilter(v_temp1, v_temp2, G_T20_FFT_SIZE);

    // ------------------------------------------------------------
    // 5) Window
    // ------------------------------------------------------------
    T20_applyWindow(v_temp2, G_T20_FFT_SIZE);

    // ------------------------------------------------------------
    // 6) Power Spectrum
    // ------------------------------------------------------------
    T20_computePowerSpectrum(v_temp2, g_t20_power);

    // ------------------------------------------------------------
    // 7) Noise Spectrum 학습
    // ------------------------------------------------------------
    T20_learnNoiseSpectrum(g_t20_power);

    // ------------------------------------------------------------
    // 8) Spectral Subtraction
    // ------------------------------------------------------------
    T20_applySpectralSubtraction(g_t20_power);

    // ------------------------------------------------------------
    // 9) Mel Filterbank + Log
    // ------------------------------------------------------------
    T20_applyMelFilterbank(g_t20_power, g_t20_log_mel);

    // ------------------------------------------------------------
    // 10) DCT-II -> MFCC
    // ------------------------------------------------------------
    T20_computeDCT2(
        g_t20_log_mel,
        p_mfcc_out,
        G_T20_MEL_FILTERS,
        G_T20_MFCC_COEFFS
    );
}

// ============================================================================
// [Delta / Classifier Vector]
// ============================================================================

static void T20_pushMfccHistory(const float* p_mfcc)
{
    // 히스토리 시프트
    if (g_t20_mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(g_t20_mfcc_history[g_t20_mfcc_history_count], p_mfcc, sizeof(float) * G_T20_MFCC_COEFFS);
        g_t20_mfcc_history_count++;
    } else {
        for (int i = 0; i < G_T20_MFCC_HISTORY - 1; i++) {
            memcpy(g_t20_mfcc_history[i],
                   g_t20_mfcc_history[i + 1],
                   sizeof(float) * G_T20_MFCC_COEFFS);
        }
        memcpy(g_t20_mfcc_history[G_T20_MFCC_HISTORY - 1], p_mfcc, sizeof(float) * G_T20_MFCC_COEFFS);
    }
}

static void T20_computeDeltaFromHistory(float* p_delta_out)
{
    memset(p_delta_out, 0, sizeof(float) * G_T20_MFCC_COEFFS);

    // 히스토리가 충분하지 않으면 0으로 둠
    if (g_t20_mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    // 중앙 프레임 기준 회귀
    const int v_center = G_T20_MFCC_HISTORY / 2;
    const int v_N = g_t20_cfg.feature.delta_window;

    float v_den = 0.0f;
    for (int n = 1; n <= v_N; n++) {
        v_den += (float)(n * n);
    }
    v_den *= 2.0f;

    for (int c = 0; c < G_T20_MFCC_COEFFS; c++) {
        float v_num = 0.0f;

        for (int n = 1; n <= v_N; n++) {
            float v_plus  = g_t20_mfcc_history[v_center + n][c];
            float v_minus = g_t20_mfcc_history[v_center - n][c];
            v_num += (float)n * (v_plus - v_minus);
        }

        p_delta_out[c] = v_num / (v_den + G_T20_EPSILON);
    }
}

static void T20_computeDeltaDeltaFromHistory(float* p_delta2_out)
{
    memset(p_delta2_out, 0, sizeof(float) * G_T20_MFCC_COEFFS);

    if (g_t20_mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    // 먼저 각 히스토리 프레임 기준 delta를 근사 계산하지 않고
    // 간단한 2차 차분으로 구현
    // 현재 중앙 프레임 기준:
    // delta-delta ~= mfcc[t+1] - 2*mfcc[t] + mfcc[t-1]
    const int v_center = G_T20_MFCC_HISTORY / 2;

    for (int c = 0; c < G_T20_MFCC_COEFFS; c++) {
        float v_prev = g_t20_mfcc_history[v_center - 1][c];
        float v_curr = g_t20_mfcc_history[v_center][c];
        float v_next = g_t20_mfcc_history[v_center + 1][c];

        p_delta2_out[c] = v_next - (2.0f * v_curr) + v_prev;
    }
}

static void T20_buildClassifierVector(
    const float* p_mfcc,
    const float* p_delta,
    const float* p_delta2,
    float* p_out_vec)
{
    int v_idx = 0;

    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        p_out_vec[v_idx++] = p_mfcc[i];
    }

    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        p_out_vec[v_idx++] = p_delta[i];
    }

    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        p_out_vec[v_idx++] = p_delta2[i];
    }
}

// ============================================================================
// [디버그 / 출력]
// ============================================================================

static void T20_printMfccAndVector(
    const float* p_mfcc,
    const float* p_delta,
    const float* p_delta2,
    const float* p_classifier_vec)
{
    Serial.println("-------------------------------------------------");

    Serial.print("MFCC      : ");
    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        Serial.printf("%.4f ", p_mfcc[i]);
    }
    Serial.println();

    Serial.print("Delta     : ");
    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        Serial.printf("%.4f ", p_delta[i]);
    }
    Serial.println();

    Serial.print("DeltaDelta: ");
    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        Serial.printf("%.4f ", p_delta2[i]);
    }
    Serial.println();

    Serial.print("Classifier[0..9]: ");
    for (int i = 0; i < 10; i++) {
        Serial.printf("%.4f ", p_classifier_vec[i]);
    }
    Serial.println();

    Serial.printf("NoiseLearnFrames: %u / %u\n",
                  g_t20_noise_learned_frames,
                  g_t20_cfg.preprocess.noise.noise_learn_frames);

    Serial.println("-------------------------------------------------");
}



/*

추가 설명입니다.
이 코드는 ISR에서는 vTaskNotifyGiveFromISR()만 사용하고, 
실제 센서 읽기와 DSP 처리는 각각 태스크에서 수행하도록 나눴습니다. 
ESP-IDF FreeRTOS는 듀얼코어 환경을 지원하므로 이런 식의 작업 분리가 자연스럽습니다.

필터 부분은 ESP-DSP의 biquad 계수 생성 함수를 전제로 했습니다. 
공식 문서상 LPF/HPF/BPF 계수 생성은 정규화 주파수 0..0.5 범위를 사용하고, 
biquad 실행 함수는 direct form II 구조입니다. 

실무적으로는 몇 가지 바로 손보면 더 좋아집니다.
- Delta / Delta-Delta는 지금 실시간성과 단순성 위주입니다. 
  추후에는 프레임 히스토리를 더 길게 두고, 
  delta 자체의 히스토리로 delta-delta를 다시 회귀 계산하는 방식으로 바꾸면 
  더 정석적입니다.
  
- Spectral Subtraction은 현재 초기 N프레임 평균 노이즈 기반입니다. 
  장시간 운용이면 적응형 업데이트를 넣는 편이 더 안정적입니다.

- BPF는 low/high cut을 내부에서 center/Q로 환산했습니다. 이는 ESP-DSP의 BPF 계수 생성 방식에 맞추기 위한 것입니다.
  BMI270 래퍼 버전에 따라 bmi2_sens_config, bmi2_int_pin_config, mapInterruptToPin() 관련 enum 이름이 약간 다를 수 있습니다. 
  큰 구조는 맞지만, 사용 중인 SparkFun 라이브러리 버전에 맞춰 상수명만 소폭 조정이 필요할 수 있습니다. 
  이 부분은 라이브러리 헤더를 기준으로 맞추면 됩니다.

- 원하시는 다음 단계는 이 코드에서 바로 이어서 
    - AI 및 DSP 연산 가속을 위한 **PIE(Processor Instruction Extensions)**라는 벡터 명령 집합을 지원합니다. BMI270과 같은 고속 센서 데이터를 처리할 때 SIMD(Single Instruction Multiple Data)를 활용하여 연삭속도 개선
    - 분류기 입력 벡터를 ring buffer 시퀀스 형태로 확장
    - T20_Mfcc_004 형태로 설정 구조를 JSON/WebUI 연동형 구현 추가
*/