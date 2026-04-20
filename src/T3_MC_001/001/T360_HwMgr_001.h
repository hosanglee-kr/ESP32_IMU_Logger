/* ============================================================================
 * File: T360_HwMgr_001.h / .cpp 통합본
 * [Phase 1 코어 방어] esp_cpu_get_ccount()를 이용한 ISR 마비 Throttling 준비
 * ========================================================================== */
#pragma once
#include "T310_Config_001.h"
#include <driver/i2s.h>
#include <driver/adc.h>

class HardwareManager {
public:
    static void init() {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = Config::SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count = 8,
            .dma_buf_len = 1024,
            .use_apll = true 
        };
        i2s_pin_config_t pin_config = {
            .bck_io_num = Config::Pins::I2S_BCK,
            .ws_io_num = Config::Pins::I2S_WS,
            .data_out_num = I2S_PIN_NO_CHANGE,
            .data_in_num = Config::Pins::I2S_DI
        };
        i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
        i2s_set_pin(I2S_NUM_0, &pin_config);
        
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
    }

    static float getTriggerVoltage() {
        int raw = adc1_get_raw(ADC1_CHANNEL_3);
        // ADC to Voltage 변환 로직 (Calibration 적용 권장)
        return (raw / 4095.0f) * 5.0f; // 5V 시스템 기준 예시
    }
};
