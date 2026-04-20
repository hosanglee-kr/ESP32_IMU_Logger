/* ============================================================================
 * File: T300_Main_001.cpp
 * Version: 1.0 (Full Version)
 * [사용자 지시] ArduinoJson V7.4.x 적용, TARGET_TEST_NO 로직 및 BOM 완벽 제거
 * [Phase 1/2 방어] xQueueReset 및 FSM 기반 유실 없는 수집
 * ========================================================================== */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <driver/adc.h>

#include "T310_Config_001.h"
#include "T320_Types_001.h"
#include "T330_AudioProc_001.h"
#include "T340_FeatExtr_001.h"

static AudioProcessor audioProc;
static FeatureExtractor extractor;
static InferenceEngine engine;

static TaskHandle_t xCaptureTaskHndl = nullptr;
static TaskHandle_t xProcessTaskHndl = nullptr;
static QueueHandle_t xTrgQueue = nullptr;

static float dma_rx_buf[1024];

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
        return (raw / 4095.0f) * 5.0f; 
    }
};

void TaskCapture(void* pv) {
    bool is_waiting_valid = false;
    int target_wait_idx = 0;
    int current_trigger_idx = 0;
    int trigger_seq_count = 0; 

    for(;;) {
        size_t bytes_read;
        i2s_read(I2S_NUM_0, dma_rx_buf, sizeof(dma_rx_buf), &bytes_read, portMAX_DELAY);
        int samples_read = bytes_read / sizeof(float);

        audioProc.writeToRingBuffer(dma_rx_buf, samples_read);
        float v = HardwareManager::getTriggerVoltage();
        
        if (!is_waiting_valid && v >= Config::ADC_V_TRG) {
            current_trigger_idx = audioProc.getCurrentWriteIdx();
            target_wait_idx = (current_trigger_idx + Config::VALID_OFFSET + Config::VALID_SAMP_LEN) % Config::RAW_BUF_SAMP;
            is_waiting_valid = true;
        }

        if (is_waiting_valid) {
            int cur_idx = audioProc.getCurrentWriteIdx();
            bool reached = (target_wait_idx > current_trigger_idx) ? 
                           (cur_idx >= target_wait_idx) : 
                           (cur_idx >= target_wait_idx && cur_idx < current_trigger_idx);

            if (reached) {
                if (trigger_seq_count == Config::TARGET_TEST_1 || trigger_seq_count == Config::TARGET_TEST_2) {
                    TriggerPayload payload = {current_trigger_idx, trigger_seq_count};
                    xQueueSend(xTrgQueue, &payload, 0);
                }
                trigger_seq_count++;
                if (trigger_seq_count >= 3) trigger_seq_count = 0; 
                
                is_waiting_valid = false;
            }
        }
    }
}

void TaskProcessing(void* pv) {
    if (xTrgQueue) xQueueReset(xTrgQueue);

    static float noise_buf[Config::NOISE_SAMP_LEN];
    static float valid_buf[Config::VALID_SAMP_LEN];
    static float trg_buf[Config::TRIG_SAMP_LEN];

    for(;;) {
        TriggerPayload payload;
        if (xQueueReceive(xTrgQueue, &payload, portMAX_DELAY)) {
            int n_start = payload.trigger_idx - Config::NOISE_SAMP_LEN;
            int v_start = payload.trigger_idx + Config::VALID_OFFSET;
            int t_start = payload.trigger_idx;

            audioProc.extractLinearBuffer(n_start, Config::NOISE_SAMP_LEN, noise_buf);
            audioProc.extractLinearBuffer(v_start, Config::VALID_SAMP_LEN, valid_buf);
            audioProc.extractLinearBuffer(t_start, Config::TRIG_SAMP_LEN, trg_buf);

            int lid = 1; // Default LID
            audioProc.applyPreEmphasis(valid_buf, Config::VALID_SAMP_LEN);
            
            // [기능 복원 완전 적용] 능동 소음 제거 알고리즘 실행
            audioProc.processANC(valid_buf, noise_buf, lid); 

            SignalFeatures ft = extractor.extractAll(valid_buf, trg_buf);
            InferenceOutput out = engine.runHybridDecision(ft, lid);

            JsonDocument doc;
            doc["test_num"] = payload.test_number;
            doc["lid"] = lid;
            doc["final_result"] = (int)out.final_r;
            doc["prob"] = out.ml_probability;
            doc["energy"] = ft.energy;
            doc["stddev"] = ft.wvfm_stddev;
            doc["trg_cnt"] = ft.trigger_count;
            
            serializeJson(doc, Serial);
            Serial.println();
        }
    }
}

void T3_init() {
    Serial.begin(115200);
    xTrgQueue = xQueueCreate(10, sizeof(TriggerPayload));
    
    HardwareManager::init();
    if(!audioProc.begin()) {
        Serial.println("{\"error\":\"PSRAM init fail\"}");
        while(1);
    }

    xTaskCreatePinnedToCore(TaskCapture, "Cap", 4096, NULL, 5, &xCaptureTaskHndl, 1);
    xTaskCreatePinnedToCore(TaskProcessing, "Proc", 16384, NULL, 2, &xProcessTaskHndl, 0);
}

void T3_run() {
    vTaskDelete(NULL); 
}
