/* ============================================================================
 * File: T480_MicEng_011.cpp
 * Summary: I2S DMA Microphone Acquisition Implementation (FSM Optimized)
 * ============================================================================
 * * [AI 메모: 마이그레이션 적용 사항]
 * - DMA 버퍼 카운트(8) 및 32bit PCM 스케일링 상수(2^31) 매직 넘버 철폐 완료.
 * - [방어] 다중 init() 호출에 의한 I2S 드라이버 충돌(Panic) 방어 코드 추가.
 * - [방어] readData() 호출 시 버퍼 오버런(Overrun) 원천 차단 클램프 로직 추가.
 * ========================================================================== */

#include "T480_MicEng_011.hpp"
#include "esp_log.h"

static const char* TAG = "T480_MIC";

T480_MicEngine::T480_MicEngine() {
    memset(_statusText, 0, sizeof(_statusText));
    strlcpy(_statusText, "idle", sizeof(_statusText));
}

bool T480_MicEngine::init() {
    // [방어] 중복 초기화로 인한 ESP-IDF 드라이버 패닉 원천 차단 (멱등성 보장)
    if (_isInitialized) {
        ESP_LOGI(TAG, "I2S driver is already initialized. Bypassing.");
        return true;
    }

    i2s_config_t v_i2sConfig = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SmeaConfig::System::SAMPLING_RATE_CONST,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,        // ICS43434 2채널 모드
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = SmeaConfig::Hardware::I2S_DMA_BUF_COUNT_CONST, 
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
    ESP_LOGI(TAG, "I2S Engine Initialized Successfully.");
    return true;
}

void T480_MicEngine::pause() {
    if (!_isInitialized || _isPaused) return;

    // I2S 정지 (클럭 발생 중단 -> ICS43434 절전 모드 자동 진입)
    i2s_stop((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST);

    _isPaused = true;
    strlcpy(_statusText, "paused", sizeof(_statusText));
    ESP_LOGI(TAG, "I2S Engine Paused. Mic enters sleep mode.");
}

void T480_MicEngine::resume() {
    if (!_isInitialized || !_isPaused) return;

    // 재가동 시 과거 쓰레기 데이터로 인한 스파이크 방어
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

    // [치명적 버그 방어] 동적/가변 요청 샘플 수가 _dmaBuffer의 크기를 초과하여 
    // 메모리를 침범(Buffer Overrun)하고 패닉을 일으키는 것을 원천 차단 (Clamp)
    if (p_reqSamples > SmeaConfig::System::FFT_SIZE_CONST) {
        ESP_LOGW(TAG, "Requested samples (%lu) exceeded buffer size. Clamped to %lu.", 
                 (unsigned long)p_reqSamples, (unsigned long)SmeaConfig::System::FFT_SIZE_CONST);
        p_reqSamples = SmeaConfig::System::FFT_SIZE_CONST;
    }

    size_t v_bytesRead = 0;
    size_t v_bytesToRead = p_reqSamples * SmeaConfig::System::CHANNELS_CONST * sizeof(int32_t);

    // esp_err_t 결과 확인을 통한 드라이버 에러 방어
    esp_err_t v_err = i2s_read((i2s_port_t)SmeaConfig::Hardware::I2S_PORT_NUM_CONST, &_dmaBuffer, v_bytesToRead, &v_bytesRead, portMAX_DELAY);

    if (v_err != ESP_OK) {
        ESP_LOGE(TAG, "I2S Read Error: %d", v_err);
        return 0;
    }

    uint32_t v_samplesRead = v_bytesRead / (SmeaConfig::System::CHANNELS_CONST * sizeof(int32_t));

    // 중앙 관리되는 32비트 PCM 정규화 스케일 상수 사용 (-1.0f ~ 1.0f)
    const float v_scale = SmeaConfig::System::PCM_32BIT_SCALE_CONST;

    // De-interleave (교차 배열 분리) 및 float 변환 적용
    for (uint32_t i = 0; i < v_samplesRead; i++) {
        p_outL[i] = (float)_dmaBuffer[i * 2] * v_scale;
        p_outR[i] = (float)_dmaBuffer[i * 2 + 1] * v_scale;
    }

    return v_samplesRead;
}
