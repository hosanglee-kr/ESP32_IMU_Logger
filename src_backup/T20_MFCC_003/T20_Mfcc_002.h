#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "esp_dsp.h"

/*
===============================================================================
파일명: T20_Mfcc_002.ino
버전: v002
설명:
  - BMI270 SPI 연결
  - BMI270 Data Ready 인터럽트 기반 1.6kHz 샘플링
  - Hamming Window + FFT + Power Spectrum
  - Mel Filterbank + Log + DCT-II
  - "진짜 MFCC" 13계수 추출

주의:
  - SparkFun BMI270 라이브러리 + Bosch 매크로 사용
  - ESP32-S3 기준
  - loop() polling 아님. INT 핀에서 샘플 획득 트리거
===============================================================================
*/

// -----------------------------------------------------------------------------
// [환경 설정]
// -----------------------------------------------------------------------------
#define G_T20_FFT_SIZE                 256
#define G_T20_SAMPLE_RATE_HZ           1600.0f
#define G_T20_MFCC_COEFFS              13
#define G_T20_MEL_FILTERS              26
#define G_T20_FRAME_HOP                G_T20_FFT_SIZE

// BMI270 SPI 핀
#define G_T20_PIN_SPI_SCK              12
#define G_T20_PIN_SPI_MISO             13
#define G_T20_PIN_SPI_MOSI             11
#define G_T20_PIN_BMI_CS               10
#define G_T20_PIN_BMI_INT1             14

// 옵션
#define G_T20_USE_AXIS_Z               1
#define G_T20_ENABLE_HP_PREEMPHASIS    0
#define G_T20_PREEMPHASIS_ALPHA        0.97f

// 안전 최소값
#define G_T20_EPSILON                  1.0e-12f
#define G_T20_PI                       3.14159265358979323846f

// -----------------------------------------------------------------------------
// [전역 객체 / 상태]
// -----------------------------------------------------------------------------
BMI270 g_t20_imu;
SPIClass g_t20_spi(FSPI);

// ISR -> main 전달용 플래그
volatile bool g_t20_drdy_flag = false;

// raw frame buffer
float __attribute__((aligned(16))) g_t20_frame[G_T20_FFT_SIZE];
float __attribute__((aligned(16))) g_t20_window[G_T20_FFT_SIZE];
float __attribute__((aligned(16))) g_t20_fft_buffer[G_T20_FFT_SIZE * 2];
float __attribute__((aligned(16))) g_t20_power[(G_T20_FFT_SIZE / 2) + 1];
float __attribute__((aligned(16))) g_t20_mel_energies[G_T20_MEL_FILTERS];
float __attribute__((aligned(16))) g_t20_mfcc[G_T20_MFCC_COEFFS];

// mel bank 가중치
float __attribute__((aligned(16)))
g_t20_mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

uint16_t g_t20_sample_index = 0;
float g_t20_prev_sample = 0.0f;

// -----------------------------------------------------------------------------
// [함수 선언]
// -----------------------------------------------------------------------------
static void IRAM_ATTR T20_onBmiDrdyISR(void);

static bool T20_initBMI270_SPI(void);
static bool T20_configBMI270_1600Hz_DRDY(void);

static void T20_buildHammingWindow(void);
static void T20_buildMelFilterbank(void);

static inline float T20_hzToMel(float p_hz);
static inline float T20_melToHz(float p_mel);

static void T20_captureSampleOnInterrupt(void);
static void T20_processFrame(void);

static void T20_applyWindow(float* p_data, uint16_t p_len);
static void T20_computePowerSpectrum(const float* p_time, float* p_power);
static void T20_applyMelFilterbank(const float* p_power, float* p_mel_out);
static void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);
static void T20_computeMFCC(const float* p_frame, float* p_mfcc_out);

// -----------------------------------------------------------------------------
// [setup]
// -----------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== T20_Mfcc_002 start ===");

    // SPI init
    g_t20_spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);

    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);

    pinMode(G_T20_PIN_BMI_INT1, INPUT);

    // DSP init
    esp_err_t v_fft_res = dsps_fft2r_init_fc32(NULL, G_T20_FFT_SIZE);
    if (v_fft_res != ESP_OK) {
        Serial.printf("FFT init failed: %d\n", (int)v_fft_res);
        while (1) { delay(100); }
    }

    T20_buildHammingWindow();
    T20_buildMelFilterbank();

    if (!T20_initBMI270_SPI()) {
        Serial.println("BMI270 SPI init failed");
        while (1) { delay(100); }
    }

    if (!T20_configBMI270_1600Hz_DRDY()) {
        Serial.println("BMI270 config failed");
        while (1) { delay(100); }
    }

    attachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1), T20_onBmiDrdyISR, RISING);

    Serial.println("BMI270 SPI + DRDY + MFCC ready");
}

// -----------------------------------------------------------------------------
// [loop]
// -----------------------------------------------------------------------------
void loop()
{
    if (g_t20_drdy_flag) {
        noInterrupts();
        g_t20_drdy_flag = false;
        interrupts();

        T20_captureSampleOnInterrupt();
    }
}

// -----------------------------------------------------------------------------
// [ISR]
// -----------------------------------------------------------------------------
static void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    g_t20_drdy_flag = true;
}

// -----------------------------------------------------------------------------
// [BMI270 init / config]
// -----------------------------------------------------------------------------
static bool T20_initBMI270_SPI(void)
{
    int8_t v_rslt = g_t20_imu.beginSPI(G_T20_PIN_BMI_CS, 10000000, g_t20_spi);
    if (v_rslt != BMI2_OK) {
        Serial.printf("beginSPI failed: %d\n", (int)v_rslt);
        return false;
    }

    return true;
}

static bool T20_configBMI270_1600Hz_DRDY(void)
{
    int8_t v_rslt;

    // 고수준 ODR 설정
    v_rslt = g_t20_imu.setAccelODR(BMI2_ACC_ODR_1600HZ);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setAccelODR failed: %d\n", (int)v_rslt);
        return false;
    }

    // gyro는 필요 없어도 라이브러리 상태 일관성 위해 설정
    v_rslt = g_t20_imu.setGyroODR(BMI2_GYR_ODR_1600HZ);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setGyroODR failed: %d\n", (int)v_rslt);
        return false;
    }

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

    // accel 상세 config
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

    // gyro 상세 config
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

    // INT1 핀 설정
    bmi2_int_pin_config v_int_cfg;
    memset(&v_int_cfg, 0, sizeof(v_int_cfg));

    v_int_cfg.pin_type    = BMI2_INT1;
    v_int_cfg.pin_cfg[0].output_en = BMI2_ENABLE;
    v_int_cfg.pin_cfg[0].od        = BMI2_DISABLE; // push-pull
    v_int_cfg.pin_cfg[0].lvl       = BMI2_HIGH;
    v_int_cfg.pin_cfg[0].input_en  = BMI2_DISABLE;
    v_int_cfg.pin_cfg[0].int_latch = BMI2_DISABLE;

    v_rslt = g_t20_imu.setInterruptPinConfig(v_int_cfg);
    if (v_rslt != BMI2_OK) {
        Serial.printf("setInterruptPinConfig failed: %d\n", (int)v_rslt);
        return false;
    }

    // Data Ready 인터럽트 -> INT1 매핑
    v_rslt = g_t20_imu.mapInterruptToPin(BMI2_DRDY_INT, BMI2_INT1);
    if (v_rslt != BMI2_OK) {
        Serial.printf("mapInterruptToPin failed: %d\n", (int)v_rslt);
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// [Window / Mel Filterbank]
// -----------------------------------------------------------------------------
static void T20_buildHammingWindow(void)
{
    for (int i = 0; i < G_T20_FFT_SIZE; i++) {
        g_t20_window[i] =
            0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)i) / (float)(G_T20_FFT_SIZE - 1));
    }
}

static inline float T20_hzToMel(float p_hz)
{
    return 2595.0f * log10f(1.0f + (p_hz / 700.0f));
}

static inline float T20_melToHz(float p_mel)
{
    return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f);
}

static void T20_buildMelFilterbank(void)
{
    memset(g_t20_mel_bank, 0, sizeof(g_t20_mel_bank));

    const int v_num_fft_bins = (G_T20_FFT_SIZE / 2) + 1;
    const float v_f_min = 0.0f;
    const float v_f_max = G_T20_SAMPLE_RATE_HZ * 0.5f;

    const float v_mel_min = T20_hzToMel(v_f_min);
    const float v_mel_max = T20_hzToMel(v_f_max);

    float v_mel_points[G_T20_MEL_FILTERS + 2];
    float v_hz_points[G_T20_MEL_FILTERS + 2];
    int   v_bin_points[G_T20_MEL_FILTERS + 2];

    for (int i = 0; i < G_T20_MEL_FILTERS + 2; i++) {
        float v_ratio = (float)i / (float)(G_T20_MEL_FILTERS + 1);
        v_mel_points[i] = v_mel_min + (v_mel_max - v_mel_min) * v_ratio;
        v_hz_points[i] = T20_melToHz(v_mel_points[i]);

        int v_bin = (int)floorf(((float)G_T20_FFT_SIZE + 1.0f) * v_hz_points[i] / G_T20_SAMPLE_RATE_HZ);
        if (v_bin < 0) v_bin = 0;
        if (v_bin >= v_num_fft_bins) v_bin = v_num_fft_bins - 1;
        v_bin_points[i] = v_bin;
    }

    for (int m = 0; m < G_T20_MEL_FILTERS; m++) {
        int v_left   = v_bin_points[m];
        int v_center = v_bin_points[m + 1];
        int v_right  = v_bin_points[m + 2];

        if (v_center <= v_left)  v_center = v_left + 1;
        if (v_right  <= v_center) v_right = v_center + 1;
        if (v_right >= v_num_fft_bins) v_right = v_num_fft_bins - 1;

        for (int k = v_left; k < v_center; k++) {
            g_t20_mel_bank[m][k] = (float)(k - v_left) / (float)(v_center - v_left);
        }

        for (int k = v_center; k <= v_right; k++) {
            g_t20_mel_bank[m][k] = (float)(v_right - k) / (float)(v_right - v_center + 1e-6f);
        }
    }
}

// -----------------------------------------------------------------------------
// [Sampling]
// -----------------------------------------------------------------------------
static void T20_captureSampleOnInterrupt(void)
{
    int8_t v_rslt = g_t20_imu.getSensorData();
    if (v_rslt != BMI2_OK) {
        return;
    }

    float v_sample = 0.0f;

#if G_T20_USE_AXIS_Z
    v_sample = g_t20_imu.data.accelZ;
#else
    v_sample = g_t20_imu.data.accelX;
#endif

#if G_T20_ENABLE_HP_PREEMPHASIS
    float v_emph = v_sample - (G_T20_PREEMPHASIS_ALPHA * g_t20_prev_sample);
    g_t20_prev_sample = v_sample;
    v_sample = v_emph;
#endif

    g_t20_frame[g_t20_sample_index++] = v_sample;

    if (g_t20_sample_index >= G_T20_FFT_SIZE) {
        T20_processFrame();
        g_t20_sample_index = 0;
    }
}

// -----------------------------------------------------------------------------
// [MFCC pipeline]
// -----------------------------------------------------------------------------
static void T20_applyWindow(float* p_data, uint16_t p_len)
{
    for (uint16_t i = 0; i < p_len; i++) {
        p_data[i] *= g_t20_window[i];
    }
}

static void T20_computePowerSpectrum(const float* p_time, float* p_power)
{
    // real -> complex
    for (int i = 0; i < G_T20_FFT_SIZE; i++) {
        g_t20_fft_buffer[2 * i + 0] = p_time[i];
        g_t20_fft_buffer[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_f32(g_t20_fft_buffer, G_T20_FFT_SIZE);
    dsps_bit_rev_f32(g_t20_fft_buffer, G_T20_FFT_SIZE);

    // [0 .. N/2]
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

static void T20_applyMelFilterbank(const float* p_power, float* p_mel_out)
{
    const int v_num_fft_bins = (G_T20_FFT_SIZE / 2) + 1;

    for (int m = 0; m < G_T20_MEL_FILTERS; m++) {
        float v_sum = 0.0f;

        for (int k = 0; k < v_num_fft_bins; k++) {
            v_sum += p_power[k] * g_t20_mel_bank[m][k];
        }

        if (v_sum < G_T20_EPSILON) {
            v_sum = G_T20_EPSILON;
        }

        p_mel_out[m] = logf(v_sum);
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
    float v_work[G_T20_FFT_SIZE] __attribute__((aligned(16)));

    memcpy(v_work, p_frame, sizeof(v_work));

    T20_applyWindow(v_work, G_T20_FFT_SIZE);
    T20_computePowerSpectrum(v_work, g_t20_power);
    T20_applyMelFilterbank(g_t20_power, g_t20_mel_energies);
    T20_computeDCT2(g_t20_mel_energies, p_mfcc_out, G_T20_MEL_FILTERS, G_T20_MFCC_COEFFS);
}

static void T20_processFrame(void)
{
    T20_computeMFCC(g_t20_frame, g_t20_mfcc);

    Serial.print("MFCC: ");
    for (int i = 0; i < G_T20_MFCC_COEFFS; i++) {
        Serial.printf("%.4f ", g_t20_mfcc[i]);
    }
    Serial.println();
}
