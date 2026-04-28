/* ============================================================================
 * File: T480_MicEng_006.cpp
 * Summary: I2S DMA Microphone Acquisition Implementation (FSM Optimized)
 * ========================================================================== */
#include "T480_MicEng_006.hpp"
#include "esp_log.h"

static const char* TAG = "T480_MIC";

T480_MicEngine::T480_MicEngine() {
    memset(_statusText, 0, sizeof(_statusText));
    strlcpy(_statusText, "idle", sizeof(_statusText));
}

bool T480_MicEngine::init() {
    i2s_config_t v_i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SmeaConfig::System::SAMPLING_RATE_CONST,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,        // ICS43434 2채널 모드
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = SmeaConfig::System::FFT_SIZE_CONST,
        .use_apll = true                                     // 고음질 오디오용 고정밀 오실레이터 활성화
    };

    i2s_pin_config_t v_pinConfig = {
        .bck_io_num = SmeaConfig::Hardware::PIN_I2S_BCLK_CONST,
        .ws_io_num = SmeaConfig::Hardware::PIN_I2S_WS_CONST,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = SmeaConfig::Hardware::PIN_I2S_DIN_CONST
    };

    if (i2s_driver_install((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST, &v_i2sConfig, 0, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver install failed");
        strlcpy(_statusText, "driver_fail", sizeof(_statusText));
        return false;
    }
    if (i2s_set_pin((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST, &v_pinConfig) != ESP_OK) {
        ESP_LOGE(TAG, "I2S pin config failed");
        strlcpy(_statusText, "pin_fail", sizeof(_statusText));
        return false;
    }

    _isInitialized = true;
    _isPaused = false;
    strlcpy(_statusText, "42kHz_active", sizeof(_statusText));
    return true;
}

void T480_MicEngine::pause() {
    if (!_isInitialized || _isPaused) return;
    
    // I2S 정지 (클럭 발생 중단 -> ICS43434 절전 모드 자동 진입)
    i2s_stop((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST);
    
    _isPaused = true;
    strlcpy(_statusText, "paused", sizeof(_statusText));
    ESP_LOGI(TAG, "I2S Engine Paused.");
}

void T480_MicEngine::resume() {
    if (!_isInitialized || !_isPaused) return;
    
    // 재가동 시 과거 쓰레기 데이터 방어
    clearBuffer();
    i2s_start((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST);
    
    _isPaused = false;
    strlcpy(_statusText, "42kHz_active", sizeof(_statusText));
    ESP_LOGI(TAG, "I2S Engine Resumed.");
}

void T480_MicEngine::clearBuffer() {
    if (!_isInitialized) return;
    i2s_zero_dma_buffer((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST);
}

uint32_t T480_MicEngine::readData(float* p_outL, float* p_outR, uint32_t p_reqSamples) {
    // [정합성 방어] 일시 정지(pause) 상태일 때는 I2S 읽기 대기(Block)를 즉시 회피
    if (!_isInitialized || _isPaused || !p_outL || !p_outR) return 0;

    size_t v_bytesRead = 0;
    size_t v_bytesToRead = p_reqSamples * SmeaConfig::System::CHANNELS_CONST * sizeof(int32_t);

    // esp_err_t 결과 확인 추가
    esp_err_t v_err = i2s_read((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST, &_dmaBuffer, v_bytesToRead, &v_bytesRead, portMAX_DELAY);
    
    if (v_err != ESP_OK) {
        ESP_LOGE(TAG, "I2S Read Error: %d", v_err);
        return 0;
    }
    
    uint32_t v_samplesRead = v_bytesRead / (SmeaConfig::System::CHANNELS_CONST * sizeof(int32_t));

    // De-interleave 및 float 변환 (32bit PCM -> Float)
    const float v_scale = 1.0f / 2147483648.0f; // 2^31
    for (uint32_t i = 0; i < v_samplesRead; i++) {
        p_outL[i] = (float)_dmaBuffer[i * 2] * v_scale;
        p_outR[i] = (float)_dmaBuffer[i * 2 + 1] * v_scale;
    }

    return v_samplesRead;
}
