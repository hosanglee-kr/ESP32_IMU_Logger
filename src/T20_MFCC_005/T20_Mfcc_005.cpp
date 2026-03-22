// T20_Mfcc_005.cpp

#include "T20_Mfcc_005.h"

#include <SPI.h>
#include <math.h>
#include <string.h>

#include "SparkFun_BMI270_Arduino_Library.h"
#include "esp_dsp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ============================================================================
// [내부 타입]
// ============================================================================

typedef struct {
    uint8_t frame_index;
} ST_T20_FrameMessage_t;

// ============================================================================
// [내부 구현체]
// ============================================================================

struct CL_T20_Mfcc::ST_Impl
{
    // ---- 외부 객체 ----
    BMI270   imu;
    SPIClass spi;

    // ---- RTOS ----
    TaskHandle_t  sensor_task_handle;
    TaskHandle_t  process_task_handle;
    QueueHandle_t frame_queue;
    SemaphoreHandle_t mutex;

    // ---- 상태 ----
    volatile bool drdy_flag;
    bool initialized;
    bool running;

    ST_T20_Config_t cfg;

    // ---- raw double buffer ----
    float __attribute__((aligned(16))) frame_buffer[2][G_T20_FFT_SIZE];
    volatile uint8_t  active_fill_buffer;
    volatile uint16_t active_sample_index;

    // ---- processing buffers ----
    float __attribute__((aligned(16))) work_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) temp_frame[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) window[G_T20_FFT_SIZE];
    float __attribute__((aligned(16))) fft_buffer[G_T20_FFT_SIZE * 2];
    float __attribute__((aligned(16))) power[(G_T20_FFT_SIZE / 2) + 1];
    float __attribute__((aligned(16))) noise_spectrum[(G_T20_FFT_SIZE / 2) + 1];
    float __attribute__((aligned(16))) log_mel[G_T20_MEL_FILTERS];
    float __attribute__((aligned(16))) mel_bank[G_T20_MEL_FILTERS][(G_T20_FFT_SIZE / 2) + 1];

    // ---- feature history ----
    float __attribute__((aligned(16))) mfcc_history[G_T20_MFCC_HISTORY][G_T20_MFCC_COEFFS];
    uint16_t mfcc_history_count;

    // ---- latest output ----
    ST_T20_FeatureVector_t latest_feature;
    ST_T20_FeatureRingBuffer_t seq_rb;
    float latest_sequence_flat[G_T20_MAX_SEQUENCE_FRAMES * G_T20_FEATURE_DIM];

    bool latest_vector_valid;
    bool latest_sequence_valid;

    // ---- filter ----
    float biquad_coeffs[5];
    float biquad_state[2];

    // ---- misc ----
    float prev_raw_sample;
    uint16_t noise_learned_frames;

    ST_Impl()
    : spi(FSPI)
    {
        sensor_task_handle = nullptr;
        process_task_handle = nullptr;
        frame_queue = nullptr;
        mutex = nullptr;

        drdy_flag = false;
        initialized = false;
        running = false;

        cfg = T20_makeDefaultConfig();

        memset(frame_buffer, 0, sizeof(frame_buffer));
        memset(work_frame, 0, sizeof(work_frame));
        memset(temp_frame, 0, sizeof(temp_frame));
        memset(window, 0, sizeof(window));
        memset(fft_buffer, 0, sizeof(fft_buffer));
        memset(power, 0, sizeof(power));
        memset(noise_spectrum, 0, sizeof(noise_spectrum));
        memset(log_mel, 0, sizeof(log_mel));
        memset(mel_bank, 0, sizeof(mel_bank));
        memset(mfcc_history, 0, sizeof(mfcc_history));
        memset(&latest_feature, 0, sizeof(latest_feature));
        memset(&seq_rb, 0, sizeof(seq_rb));
        memset(latest_sequence_flat, 0, sizeof(latest_sequence_flat));
        memset(biquad_coeffs, 0, sizeof(biquad_coeffs));
        memset(biquad_state, 0, sizeof(biquad_state));

        active_fill_buffer = 0;
        active_sample_index = 0;
        mfcc_history_count = 0;
        prev_raw_sample = 0.0f;
        noise_learned_frames = 0;
        latest_vector_valid = false;
        latest_sequence_valid = false;
    }
};

// ============================================================================
// [파일 내부 전방 선언]
// ============================================================================

static CL_T20_Mfcc* s_t20_instance = nullptr;

static void IRAM_ATTR T20_onBmiDrdyISR(void);
static void T20_sensorTask(void* p_arg);
static void T20_processTask(void* p_arg);

static bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p);
static bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p);
static bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p);
static bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p);
static void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p);
static void T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p);

static float T20_hzToMel(float p_hz);
static float T20_melToHz(float p_mel);

static float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p);
static void T20_applyDCRemove(float* p_data, uint16_t p_len);
static void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha);
static void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs);
static void T20_applyBiquadFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len);
static void T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len);

static void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power);
static void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power);
static void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power);
static void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out);
static void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len);
static void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out);

static void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc);
static void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta_out);
static void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta2_out);
static void T20_buildVector39(const float* p_mfcc, const float* p_delta, const float* p_delta2, float* p_out_vec);

static void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames);
static void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec);
static bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb);
static void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat);
static void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p);

// ============================================================================
// [공개 API]
// ============================================================================

CL_T20_Mfcc::CL_T20_Mfcc()
: _impl(new ST_Impl())
{
    s_t20_instance = this;
}

bool CL_T20_Mfcc::begin(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg != nullptr) {
        _impl->cfg = *p_cfg;
    } else {
        _impl->cfg = T20_makeDefaultConfig();
    }

    if (_impl->cfg.feature.fft_size != G_T20_FFT_SIZE) {
        return false;
    }
    if (_impl->cfg.feature.mel_filters != G_T20_MEL_FILTERS) {
        return false;
    }
    if (_impl->cfg.feature.mfcc_coeffs != G_T20_MFCC_COEFFS) {
        return false;
    }
    if (_impl->cfg.output.sequence_frames == 0 ||
        _impl->cfg.output.sequence_frames > G_T20_MAX_SEQUENCE_FRAMES) {
        return false;
    }

    _impl->mutex = xSemaphoreCreateMutex();
    if (_impl->mutex == nullptr) {
        return false;
    }

    _impl->frame_queue = xQueueCreate(G_T20_QUEUE_LEN, sizeof(ST_T20_FrameMessage_t));
    if (_impl->frame_queue == nullptr) {
        return false;
    }

    _impl->spi.begin(
        G_T20_PIN_SPI_SCK,
        G_T20_PIN_SPI_MISO,
        G_T20_PIN_SPI_MOSI,
        G_T20_PIN_BMI_CS
    );

    pinMode(G_T20_PIN_BMI_CS, OUTPUT);
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);
    pinMode(G_T20_PIN_BMI_INT1, INPUT);

    if (!T20_initDSP(_impl)) {
        return false;
    }

    if (!T20_initBMI270_SPI(_impl)) {
        return false;
    }

    if (!T20_configBMI270_1600Hz_DRDY(_impl)) {
        return false;
    }

    if (!T20_configureFilter(_impl)) {
        return false;
    }

    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames);

    attachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1), T20_onBmiDrdyISR, RISING);

    _impl->initialized = true;
    return true;
}

bool CL_T20_Mfcc::start(void)
{
    if (!_impl->initialized || _impl->running) {
        return false;
    }

    BaseType_t v1 = xTaskCreatePinnedToCore(
        T20_sensorTask,
        "T20_Sensor",
        G_T20_SENSOR_TASK_STACK,
        _impl,
        G_T20_SENSOR_TASK_PRIO,
        &_impl->sensor_task_handle,
        0
    );

    BaseType_t v2 = xTaskCreatePinnedToCore(
        T20_processTask,
        "T20_Process",
        G_T20_PROCESS_TASK_STACK,
        _impl,
        G_T20_PROCESS_TASK_PRIO,
        &_impl->process_task_handle,
        1
    );

    if (v1 != pdPASS || v2 != pdPASS) {
        return false;
    }

    _impl->running = true;
    return true;
}

void CL_T20_Mfcc::stop(void)
{
    _impl->running = false;

    if (_impl->sensor_task_handle != nullptr) {
        vTaskDelete(_impl->sensor_task_handle);
        _impl->sensor_task_handle = nullptr;
    }

    if (_impl->process_task_handle != nullptr) {
        vTaskDelete(_impl->process_task_handle);
        _impl->process_task_handle = nullptr;
    }
}

bool CL_T20_Mfcc::setConfig(const ST_T20_Config_t* p_cfg)
{
    if (p_cfg == nullptr) {
        return false;
    }

    if (p_cfg->output.sequence_frames == 0 ||
        p_cfg->output.sequence_frames > G_T20_MAX_SEQUENCE_FRAMES) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    _impl->cfg = *p_cfg;
    bool v_ok = T20_configureFilter(_impl);
    T20_seqInit(&_impl->seq_rb, _impl->cfg.output.sequence_frames);

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

void CL_T20_Mfcc::getConfig(ST_T20_Config_t* p_cfg_out) const
{
    if (p_cfg_out == nullptr) {
        return;
    }

    *p_cfg_out = _impl->cfg;
}

bool CL_T20_Mfcc::getLatestFeatureVector(ST_T20_FeatureVector_t* p_out) const
{
    if (p_out == nullptr) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ok = _impl->latest_vector_valid;
    if (v_ok) {
        *p_out = _impl->latest_feature;
    }

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

bool CL_T20_Mfcc::getLatestVector39(float* p_out_vec, uint16_t p_len) const
{
    if (p_out_vec == nullptr || p_len < G_T20_FEATURE_DIM_TOTAL) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ok = _impl->latest_vector_valid;
    if (v_ok) {
        memcpy(p_out_vec, _impl->latest_feature.vector39, sizeof(float) * G_T20_FEATURE_DIM_TOTAL);
    }

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

bool CL_T20_Mfcc::isSequenceReady(void) const
{
    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ready = T20_seqIsReady(&_impl->seq_rb);
    xSemaphoreGive(_impl->mutex);
    return v_ready;
}

bool CL_T20_Mfcc::getLatestSequenceFlat(float* p_out_seq, uint16_t p_len) const
{
    uint16_t v_need = _impl->cfg.output.sequence_frames * G_T20_FEATURE_DIM_TOTAL;
    if (p_out_seq == nullptr || p_len < v_need) {
        return false;
    }

    if (xSemaphoreTake(_impl->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool v_ok = _impl->latest_sequence_valid;
    if (v_ok) {
        memcpy(p_out_seq, _impl->latest_sequence_flat, sizeof(float) * v_need);
    }

    xSemaphoreGive(_impl->mutex);
    return v_ok;
}

bool CL_T20_Mfcc::getLatestSequenceFrameMajor(float* p_out_seq, uint16_t p_len) const
{
    return getLatestSequenceFlat(p_out_seq, p_len);
}

void CL_T20_Mfcc::printConfig(Stream& p_out) const
{
    p_out.println(F("----------- T20_Mfcc Config -----------"));
    p_out.printf("SampleRate      : %.1f\n", _impl->cfg.feature.sample_rate_hz);
    p_out.printf("FFT Size        : %u\n",   _impl->cfg.feature.fft_size);
    p_out.printf("Mel Filters     : %u\n",   _impl->cfg.feature.mel_filters);
    p_out.printf("MFCC Coeffs     : %u\n",   _impl->cfg.feature.mfcc_coeffs);
    p_out.printf("Delta Window    : %u\n",   _impl->cfg.feature.delta_window);
    p_out.printf("Output Mode     : %s\n",   _impl->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR ? "VECTOR" : "SEQUENCE");
    p_out.printf("Seq Frames      : %u\n",   _impl->cfg.output.sequence_frames);
    p_out.printf("PreEmphasis     : %s\n",   _impl->cfg.preprocess.preemphasis.enable ? "ON" : "OFF");
    p_out.printf("Noise Gate      : %s\n",   _impl->cfg.preprocess.noise.enable_gate ? "ON" : "OFF");
    p_out.printf("Spectral Sub    : %s\n",   _impl->cfg.preprocess.noise.enable_spectral_subtract ? "ON" : "OFF");
    p_out.printf("Filter Enable   : %s\n",   _impl->cfg.preprocess.filter.enable ? "ON" : "OFF");
    p_out.println(F("--------------------------------------------"));
}

void CL_T20_Mfcc::printLatest(Stream& p_out) const
{
    ST_T20_FeatureVector_t v_feat;
    if (!getLatestFeatureVector(&v_feat)) {
        p_out.println(F("No latest feature available."));
        return;
    }

    p_out.print(F("MFCC      : "));
    for (int i = 0; i < 13; ++i) p_out.printf("%.4f ", v_feat.mfcc[i]);
    p_out.println();

    p_out.print(F("Delta     : "));
    for (int i = 0; i < 13; ++i) p_out.printf("%.4f ", v_feat.delta[i]);
    p_out.println();

    p_out.print(F("DeltaDelta: "));
    for (int i = 0; i < 13; ++i) p_out.printf("%.4f ", v_feat.delta2[i]);
    p_out.println();
}

// ============================================================================
// [ISR / Task]
// ============================================================================

static void IRAM_ATTR T20_onBmiDrdyISR(void)
{
    if (s_t20_instance == nullptr) {
        return;
    }

    CL_T20_Mfcc::ST_Impl* p = s_t20_instance->_impl;
    BaseType_t v_hp_task_woken = pdFALSE;

    p->drdy_flag = true;

    if (p->sensor_task_handle != nullptr) {
        vTaskNotifyGiveFromISR(p->sensor_task_handle, &v_hp_task_woken);
    }

    if (v_hp_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void T20_sensorTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!p->running || !p->drdy_flag) {
            continue;
        }
        p->drdy_flag = false;

        if (p->imu.getSensorData() != BMI2_OK) {
            continue;
        }

        float v_sample = T20_selectAxisSample(p);

        uint8_t v_buf = p->active_fill_buffer;
        uint16_t v_idx = p->active_sample_index;

        if (v_idx < G_T20_FFT_SIZE) {
            p->frame_buffer[v_buf][v_idx] = v_sample;
            v_idx++;
            p->active_sample_index = v_idx;
        }

        if (v_idx >= G_T20_FFT_SIZE) {
            ST_T20_FrameMessage_t v_msg;
            v_msg.frame_index = v_buf;

            xQueueSend(p->frame_queue, &v_msg, 0);

            p->active_fill_buffer = (uint8_t)((v_buf + 1U) % 2U);
            p->active_sample_index = 0;
        }
    }
}

static void T20_processTask(void* p_arg)
{
    CL_T20_Mfcc::ST_Impl* p = reinterpret_cast<CL_T20_Mfcc::ST_Impl*>(p_arg);
    ST_T20_FrameMessage_t v_msg;

    for (;;) {
        if (xQueueReceive(p->frame_queue, &v_msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!p->running) {
            continue;
        }

        memcpy(p->work_frame, p->frame_buffer[v_msg.frame_index], sizeof(float) * G_T20_FFT_SIZE);

        float v_mfcc[13] = {0};
        float v_delta[13] = {0};
        float v_delta2[13] = {0};

        T20_computeMFCC(p, p->work_frame, v_mfcc);
        T20_pushMfccHistory(p, v_mfcc);
        T20_computeDeltaFromHistory(p, v_delta);
        T20_computeDeltaDeltaFromHistory(p, v_delta2);

        if (xSemaphoreTake(p->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memcpy(p->latest_feature.mfcc,   v_mfcc,   sizeof(v_mfcc));
            memcpy(p->latest_feature.delta,  v_delta,  sizeof(v_delta));
            memcpy(p->latest_feature.delta2, v_delta2, sizeof(v_delta2));
            T20_buildVector39(v_mfcc, v_delta, v_delta2, p->latest_feature.vector39);
            p->latest_vector_valid = true;

            T20_updateOutput(p);

            xSemaphoreGive(p->mutex);
        }
    }
}

// ============================================================================
// [초기화]
// ============================================================================

static bool T20_initDSP(CL_T20_Mfcc::ST_Impl* p)
{
    esp_err_t v_res = dsps_fft2r_init_fc32(NULL, G_T20_FFT_SIZE);
    if (v_res != ESP_OK) {
        return false;
    }

    T20_buildHammingWindow(p);
    T20_buildMelFilterbank(p);
    return true;
}

static bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p)
{
    int8_t v_rslt = p->imu.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    return (v_rslt == BMI2_OK);
}

static bool T20_configBMI270_1600Hz_DRDY(CL_T20_Mfcc::ST_Impl* p)
{
    int8_t v_rslt = BMI2_OK;

    v_rslt = p->imu.setAccelODR(BMI2_ACC_ODR_1600HZ);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setGyroODR(BMI2_GYR_ODR_1600HZ);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.setGyroPowerMode(BMI2_PERF_OPT_MODE, BMI2_PERF_OPT_MODE);
    if (v_rslt != BMI2_OK) return false;

    bmi2_sens_config v_acc_cfg;
    memset(&v_acc_cfg, 0, sizeof(v_acc_cfg));
    v_acc_cfg.type = BMI2_ACCEL;

    v_rslt = p->imu.getConfig(&v_acc_cfg);
    if (v_rslt != BMI2_OK) return false;

    v_acc_cfg.cfg.acc.odr         = BMI2_ACC_ODR_1600HZ;
    v_acc_cfg.cfg.acc.range       = BMI2_ACC_RANGE_2G;
    v_acc_cfg.cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    v_acc_cfg.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    v_rslt = p->imu.setConfig(v_acc_cfg);
    if (v_rslt != BMI2_OK) return false;

    bmi2_sens_config v_gyr_cfg;
    memset(&v_gyr_cfg, 0, sizeof(v_gyr_cfg));
    v_gyr_cfg.type = BMI2_GYRO;

    v_rslt = p->imu.getConfig(&v_gyr_cfg);
    if (v_rslt != BMI2_OK) return false;

    v_gyr_cfg.cfg.gyr.odr         = BMI2_GYR_ODR_1600HZ;
    v_gyr_cfg.cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    v_gyr_cfg.cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    v_gyr_cfg.cfg.gyr.noise_perf  = BMI2_PERF_OPT_MODE;
    v_gyr_cfg.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    v_rslt = p->imu.setConfig(v_gyr_cfg);
    if (v_rslt != BMI2_OK) return false;

    bmi2_int_pin_config v_int_cfg;
    memset(&v_int_cfg, 0, sizeof(v_int_cfg));

    v_int_cfg.pin_type = BMI2_INT1;
    v_int_cfg.pin_cfg[0].output_en = BMI2_ENABLE;
    v_int_cfg.pin_cfg[0].od        = BMI2_DISABLE;
    v_int_cfg.pin_cfg[0].lvl       = BMI2_HIGH;
    v_int_cfg.pin_cfg[0].input_en  = BMI2_DISABLE;
    v_int_cfg.pin_cfg[0].int_latch = BMI2_DISABLE;

    v_rslt = p->imu.setInterruptPinConfig(v_int_cfg);
    if (v_rslt != BMI2_OK) return false;

    v_rslt = p->imu.mapInterruptToPin(BMI2_DRDY_INT, BMI2_INT1);
    if (v_rslt != BMI2_OK) return false;

    return true;
}

static bool T20_configureFilter(CL_T20_Mfcc::ST_Impl* p)
{
    if (!p->cfg.preprocess.filter.enable ||
        p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memset(p->biquad_coeffs, 0, sizeof(p->biquad_coeffs));
        memset(p->biquad_state, 0, sizeof(p->biquad_state));
        return true;
    }

    float v_fs = p->cfg.feature.sample_rate_hz;
    float v_q = (p->cfg.preprocess.filter.q_factor <= 0.0f) ? 0.707f : p->cfg.preprocess.filter.q_factor;
    esp_err_t v_res = ESP_OK;

    if (p->cfg.preprocess.filter.type == EN_T20_FILTER_LPF) {
        float v_norm = p->cfg.preprocess.filter.cutoff_hz_1 / v_fs;
        v_res = dsps_biquad_gen_lpf_f32(p->biquad_coeffs, v_norm, v_q);
    }
    else if (p->cfg.preprocess.filter.type == EN_T20_FILTER_HPF) {
        float v_norm = p->cfg.preprocess.filter.cutoff_hz_1 / v_fs;
        v_res = dsps_biquad_gen_hpf_f32(p->biquad_coeffs, v_norm, v_q);
    }
    else {
        float v_low = p->cfg.preprocess.filter.cutoff_hz_1;
        float v_high = p->cfg.preprocess.filter.cutoff_hz_2;
        if (v_high <= v_low) {
            return false;
        }

        float v_center_hz = sqrtf(v_low * v_high);
        float v_bw_hz = v_high - v_low;
        float v_q_bpf = v_center_hz / (v_bw_hz + G_T20_EPSILON);
        float v_center_norm = v_center_hz / v_fs;
        if (v_q_bpf < 0.1f) v_q_bpf = 0.1f;

        v_res = dsps_biquad_gen_bpf_f32(p->biquad_coeffs, v_center_norm, v_q_bpf);
    }

    if (v_res != ESP_OK) {
        return false;
    }

    memset(p->biquad_state, 0, sizeof(p->biquad_state));
    return true;
}

// ============================================================================
// [전처리 / 수학 유틸]
// ============================================================================

static float T20_hzToMel(float p_hz)
{
    return 2595.0f * log10f(1.0f + (p_hz / 700.0f));
}

static float T20_melToHz(float p_mel)
{
    return 700.0f * (powf(10.0f, p_mel / 2595.0f) - 1.0f);
}

static void T20_buildHammingWindow(CL_T20_Mfcc::ST_Impl* p)
{
    for (int i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->window[i] =
            0.54f - 0.46f * cosf((2.0f * G_T20_PI * (float)i) / (float)(G_T20_FFT_SIZE - 1));
    }
}

static void T20_buildMelFilterbank(CL_T20_Mfcc::ST_Impl* p)
{
    memset(p->mel_bank, 0, sizeof(p->mel_bank));

    const int   v_num_bins = (G_T20_FFT_SIZE / 2) + 1;
    const float v_f_min = 0.0f;
    const float v_f_max = p->cfg.feature.sample_rate_hz * 0.5f;

    float v_mel_min = T20_hzToMel(v_f_min);
    float v_mel_max = T20_hzToMel(v_f_max);

    float v_mel_points[G_T20_MEL_FILTERS + 2];
    float v_hz_points[G_T20_MEL_FILTERS + 2];
    int   v_bin_points[G_T20_MEL_FILTERS + 2];

    for (int i = 0; i < G_T20_MEL_FILTERS + 2; ++i) {
        float v_ratio = (float)i / (float)(G_T20_MEL_FILTERS + 1);
        v_mel_points[i] = v_mel_min + (v_mel_max - v_mel_min) * v_ratio;
        v_hz_points[i] = T20_melToHz(v_mel_points[i]);

        int v_bin = (int)floorf(((float)G_T20_FFT_SIZE + 1.0f) * v_hz_points[i] / p->cfg.feature.sample_rate_hz);
        if (v_bin < 0) v_bin = 0;
        if (v_bin >= v_num_bins) v_bin = v_num_bins - 1;
        v_bin_points[i] = v_bin;
    }

    for (int m = 0; m < G_T20_MEL_FILTERS; ++m) {
        int v_left   = v_bin_points[m];
        int v_center = v_bin_points[m + 1];
        int v_right  = v_bin_points[m + 2];

        if (v_center <= v_left) v_center = v_left + 1;
        if (v_right <= v_center) v_right = v_center + 1;
        if (v_right >= v_num_bins) v_right = v_num_bins - 1;

        for (int k = v_left; k < v_center; ++k) {
            p->mel_bank[m][k] = (float)(k - v_left) / (float)(v_center - v_left);
        }

        for (int k = v_center; k <= v_right; ++k) {
            p->mel_bank[m][k] = (float)(v_right - k) / (float)(v_right - v_center + 1e-6f);
        }
    }
}

static float T20_selectAxisSample(CL_T20_Mfcc::ST_Impl* p)
{
    switch (p->cfg.preprocess.axis) {
        case EN_T20_AXIS_X: return p->imu.data.accelX;
        case EN_T20_AXIS_Y: return p->imu.data.accelY;
        case EN_T20_AXIS_Z:
        default:
            return p->imu.data.accelZ;
    }
}

static void T20_applyDCRemove(float* p_data, uint16_t p_len)
{
    float v_mean = 0.0f;
    for (uint16_t i = 0; i < p_len; ++i) {
        v_mean += p_data[i];
    }
    v_mean /= (float)p_len;

    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] -= v_mean;
    }
}

static void T20_applyPreEmphasis(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len, float p_alpha)
{
    float v_prev = p->prev_raw_sample;

    for (uint16_t i = 0; i < p_len; ++i) {
        float v_cur = p_data[i];
        p_data[i] = v_cur - (p_alpha * v_prev);
        v_prev = v_cur;
    }

    p->prev_raw_sample = v_prev;
}

static void T20_applyNoiseGate(float* p_data, uint16_t p_len, float p_threshold_abs)
{
    for (uint16_t i = 0; i < p_len; ++i) {
        if (fabsf(p_data[i]) < p_threshold_abs) {
            p_data[i] = 0.0f;
        }
    }
}

static void T20_applyBiquadFilter(CL_T20_Mfcc::ST_Impl* p, const float* p_in, float* p_out, uint16_t p_len)
{
    if (!p->cfg.preprocess.filter.enable ||
        p->cfg.preprocess.filter.type == EN_T20_FILTER_OFF) {
        memcpy(p_out, p_in, sizeof(float) * p_len);
        return;
    }

    dsps_biquad_f32(p_in, p_out, p_len, p->biquad_coeffs, p->biquad_state);
}

static void T20_applyWindow(CL_T20_Mfcc::ST_Impl* p, float* p_data, uint16_t p_len)
{
    for (uint16_t i = 0; i < p_len; ++i) {
        p_data[i] *= p->window[i];
    }
}

static void T20_computePowerSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_time, float* p_power)
{
    for (int i = 0; i < G_T20_FFT_SIZE; ++i) {
        p->fft_buffer[2 * i + 0] = p_time[i];
        p->fft_buffer[2 * i + 1] = 0.0f;
    }

    dsps_fft2r_f32(p->fft_buffer, G_T20_FFT_SIZE);
    dsps_bit_rev_f32(p->fft_buffer, G_T20_FFT_SIZE);

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        float v_re = p->fft_buffer[2 * k + 0];
        float v_im = p->fft_buffer[2 * k + 1];
        float v_p  = (v_re * v_re + v_im * v_im) / (float)G_T20_FFT_SIZE;

        if (v_p < G_T20_EPSILON) {
            v_p = G_T20_EPSILON;
        }
        p_power[k] = v_p;
    }
}

static void T20_learnNoiseSpectrum(CL_T20_Mfcc::ST_Impl* p, const float* p_power)
{
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames >= p->cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    uint16_t v_count = p->noise_learned_frames;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        p->noise_spectrum[k] =
            ((p->noise_spectrum[k] * (float)v_count) + p_power[k]) / (float)(v_count + 1U);
    }

    p->noise_learned_frames++;
}

static void T20_applySpectralSubtraction(CL_T20_Mfcc::ST_Impl* p, float* p_power)
{
    if (!p->cfg.preprocess.noise.enable_spectral_subtract) {
        return;
    }

    if (p->noise_learned_frames < p->cfg.preprocess.noise.noise_learn_frames) {
        return;
    }

    float v_strength = p->cfg.preprocess.noise.spectral_subtract_strength;

    for (int k = 0; k <= (G_T20_FFT_SIZE / 2); ++k) {
        float v_sub = p_power[k] - (v_strength * p->noise_spectrum[k]);
        if (v_sub < G_T20_EPSILON) {
            v_sub = G_T20_EPSILON;
        }
        p_power[k] = v_sub;
    }
}

static void T20_applyMelFilterbank(CL_T20_Mfcc::ST_Impl* p, const float* p_power, float* p_log_mel_out)
{
    const int v_num_bins = (G_T20_FFT_SIZE / 2) + 1;

    for (int m = 0; m < G_T20_MEL_FILTERS; ++m) {
        float v_sum = 0.0f;

        for (int k = 0; k < v_num_bins; ++k) {
            v_sum += p_power[k] * p->mel_bank[m][k];
        }

        if (v_sum < G_T20_EPSILON) {
            v_sum = G_T20_EPSILON;
        }

        p_log_mel_out[m] = logf(v_sum);
    }
}

static void T20_computeDCT2(const float* p_in, float* p_out, uint16_t p_in_len, uint16_t p_out_len)
{
    for (uint16_t n = 0; n < p_out_len; ++n) {
        float v_sum = 0.0f;
        for (uint16_t k = 0; k < p_in_len; ++k) {
            v_sum += p_in[k] * cosf((G_T20_PI / (float)p_in_len) * ((float)k + 0.5f) * (float)n);
        }
        p_out[n] = v_sum;
    }
}

static void T20_computeMFCC(CL_T20_Mfcc::ST_Impl* p, const float* p_frame, float* p_mfcc_out)
{
    memcpy(p->temp_frame, p_frame, sizeof(float) * G_T20_FFT_SIZE);

    if (p->cfg.preprocess.remove_dc) {
        T20_applyDCRemove(p->temp_frame, G_T20_FFT_SIZE);
    }

    if (p->cfg.preprocess.preemphasis.enable) {
        T20_applyPreEmphasis(p, p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.preemphasis.alpha);
    }

    if (p->cfg.preprocess.noise.enable_gate) {
        T20_applyNoiseGate(p->temp_frame, G_T20_FFT_SIZE, p->cfg.preprocess.noise.gate_threshold_abs);
    }

    T20_applyBiquadFilter(p, p->temp_frame, p->work_frame, G_T20_FFT_SIZE);
    T20_applyWindow(p, p->work_frame, G_T20_FFT_SIZE);
    T20_computePowerSpectrum(p, p->work_frame, p->power);

    T20_learnNoiseSpectrum(p, p->power);
    T20_applySpectralSubtraction(p, p->power);
    T20_applyMelFilterbank(p, p->power, p->log_mel);
    T20_computeDCT2(p->log_mel, p_mfcc_out, G_T20_MEL_FILTERS, G_T20_MFCC_COEFFS);
}

// ============================================================================
// [feature vector / sequence]
// ============================================================================

static void T20_pushMfccHistory(CL_T20_Mfcc::ST_Impl* p, const float* p_mfcc)
{
    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        memcpy(p->mfcc_history[p->mfcc_history_count], p_mfcc, sizeof(float) * 13);
        p->mfcc_history_count++;
    } else {
        for (int i = 0; i < G_T20_MFCC_HISTORY - 1; ++i) {
            memcpy(p->mfcc_history[i], p->mfcc_history[i + 1], sizeof(float) * 13);
        }
        memcpy(p->mfcc_history[G_T20_MFCC_HISTORY - 1], p_mfcc, sizeof(float) * 13);
    }
}

static void T20_computeDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta_out)
{
    memset(p_delta_out, 0, sizeof(float) * 13);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int v_center = G_T20_MFCC_HISTORY / 2;
    const int v_N = p->cfg.feature.delta_window;

    float v_den = 0.0f;
    for (int n = 1; n <= v_N; ++n) {
        v_den += (float)(n * n);
    }
    v_den *= 2.0f;

    for (int c = 0; c < 13; ++c) {
        float v_num = 0.0f;
        for (int n = 1; n <= v_N; ++n) {
            float v_plus  = p->mfcc_history[v_center + n][c];
            float v_minus = p->mfcc_history[v_center - n][c];
            v_num += (float)n * (v_plus - v_minus);
        }
        p_delta_out[c] = v_num / (v_den + G_T20_EPSILON);
    }
}

static void T20_computeDeltaDeltaFromHistory(CL_T20_Mfcc::ST_Impl* p, float* p_delta2_out)
{
    memset(p_delta2_out, 0, sizeof(float) * 13);

    if (p->mfcc_history_count < G_T20_MFCC_HISTORY) {
        return;
    }

    const int v_center = G_T20_MFCC_HISTORY / 2;

    for (int c = 0; c < 13; ++c) {
        float v_prev = p->mfcc_history[v_center - 1][c];
        float v_curr = p->mfcc_history[v_center][c];
        float v_next = p->mfcc_history[v_center + 1][c];
        p_delta2_out[c] = v_next - (2.0f * v_curr) + v_prev;
    }
}

static void T20_buildVector39(const float* p_mfcc, const float* p_delta, const float* p_delta2, float* p_out_vec)
{
    int v_idx = 0;

    for (int i = 0; i < 13; ++i) p_out_vec[v_idx++] = p_mfcc[i];
    for (int i = 0; i < 13; ++i) p_out_vec[v_idx++] = p_delta[i];
    for (int i = 0; i < 13; ++i) p_out_vec[v_idx++] = p_delta2[i];
}

static void T20_seqInit(ST_T20_FeatureRingBuffer_t* p_rb, uint16_t p_frames)
{
    memset(p_rb, 0, sizeof(ST_T20_FeatureRingBuffer_t));
    p_rb->frames = p_frames;
    p_rb->head = 0;
    p_rb->full = false;
}

static void T20_seqPush(ST_T20_FeatureRingBuffer_t* p_rb, const float* p_feature_vec)
{
    memcpy(p_rb->data[p_rb->head], p_feature_vec, sizeof(float) * G_T20_FEATURE_DIM);

    p_rb->head++;
    if (p_rb->head >= p_rb->frames) {
        p_rb->head = 0;
        p_rb->full = true;
    }
}

static bool T20_seqIsReady(const ST_T20_FeatureRingBuffer_t* p_rb)
{
    return p_rb->full;
}

static void T20_seqExportFlatten(const ST_T20_FeatureRingBuffer_t* p_rb, float* p_out_flat)
{
    // oldest -> newest 순서로 export
    uint16_t v_frames = p_rb->frames;
    uint16_t v_start = p_rb->full ? p_rb->head : 0;
    uint16_t v_written = 0;

    for (uint16_t i = 0; i < v_frames; ++i) {
        uint16_t v_idx = (uint16_t)((v_start + i) % v_frames);
        memcpy(&p_out_flat[v_written * G_T20_FEATURE_DIM],
               p_rb->data[v_idx],
               sizeof(float) * G_T20_FEATURE_DIM);
        v_written++;
    }
}

static void T20_updateOutput(CL_T20_Mfcc::ST_Impl* p)
{
    if (!p->latest_vector_valid) {
        return;
    }

    if (p->cfg.output.output_mode == EN_T20_OUTPUT_VECTOR) {
        p->latest_sequence_valid = false;
        return;
    }

    T20_seqPush(&p->seq_rb, p->latest_feature.vector39);

    if (p->cfg.output.sequence_flatten) {
        T20_seqExportFlatten(&p->seq_rb, p->latest_sequence_flat);
    }

    p->latest_sequence_valid = T20_seqIsReady(&p->seq_rb);
}

