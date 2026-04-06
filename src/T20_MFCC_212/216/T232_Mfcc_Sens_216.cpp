/* ============================================================================
 * File: T232_Mfcc_Sens_216.cpp
 * Summary: BMI270 하드웨어 제어 및 실시간 데이터 스트리밍 (C++17 Namespace & SIMD 핑퐁 반영)
 * ========================================================================== */

 /**
 * [v216 튜닝 가이드 - Sensor]
 * 1. FIFO Watermark: 현재 16샘플(10ms 주기). 시스템 지터가 심할 경우 32로 상향 조정 가능.
 * 2. SPI Speed: 10MHz 사용. 배선 길이에 따라 안정적이지 않으면 4~8MHz로 하향할 것.
 */
 
#include "T221_Mfcc_Inter_216.h" // 216 버전 헤더 포함

// 라이브러리 함수를 사용하여 센서 초기화
// [라이브러리 기반 초기화] 
// 공식 예제 4(Filtering) 및 5(FIFO)의 방식을 결합하여 구현
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. SPI 통신 시작 (내부적으로 Config Load 0x59 수행)
    int8_t status = p->bmi.beginSPI(T20::C10_Pin::BMI_CS, T20::C10_BMI::SPI_FREQ_HZ, p->spi);
    if (status != BMI2_OK) {
        p->bmi_state.master = EN_T20_STATE_ERROR;
        strlcpy(p->bmi270_status_text, "Init_Fail", T20::C10_BMI::STATUS_TEXT_MAX);
        return false;
    }

    // 2. 가속도계 설정 (bmi2_sens_config 구조체 사용 - 예제 4 방식)
    bmi2_sens_config accelConfig;
    accelConfig.type = BMI2_ACCEL;
    accelConfig.cfg.acc.odr = BMI2_ACC_ODR_1600HZ;
    accelConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    accelConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE; 
    accelConfig.cfg.acc.range = BMI2_ACC_RANGE_8G;
    p->bmi.setConfig(accelConfig);

    // 3. 자이로스코프 설정
    bmi2_sens_config gyroConfig;
    gyroConfig.type = BMI2_GYRO;
    gyroConfig.cfg.gyr.odr = BMI2_GYR_ODR_1600HZ;
    gyroConfig.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    gyroConfig.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    gyroConfig.cfg.gyr.noise_perf = BMI2_PERF_OPT_MODE;
    gyroConfig.cfg.gyr.range = BMI2_GYR_RANGE_2000;
    p->bmi.setConfig(gyroConfig);

    // 4. FIFO 설정 (예제 5 방식)
    // 1600Hz 고속 샘플링 시 인터럽트 부하를 줄이기 위해 FIFO 사용
    BMI270_FIFOConfig fifoConfig;
    fifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN;
    fifoConfig.watermark = 16; // 16개 샘플마다 인터럽트 발생
    fifoConfig.accelFilter = BMI2_ENABLE;
    fifoConfig.gyroFilter = BMI2_ENABLE;
    fifoConfig.selfWakeUp = BMI2_ENABLE;
    p->bmi.setFIFOConfig(fifoConfig);

    // 5. 인터럽트 핀 설정 (예제 3 방식)
    // FIFO 워터마크 인터럽트를 INT1 핀에 매핑
    p->bmi.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1); 

    bmi2_int_pin_config intPinConfig;
    intPinConfig.pin_type = BMI2_INT1;
    intPinConfig.int_latch = BMI2_INT_NON_LATCH;
    intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    intPinConfig.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    intPinConfig.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    p->bmi.setInterruptPinConfig(intPinConfig);
    
    
    // 6. Calibration 보정값이 있으면 센서에 보정값
    T20_bmi270_ApplyStoredCalibration(p);


    p->bmi_state.init = EN_T20_STATE_DONE;
    strlcpy(p->bmi270_status_text, "1600Hz_FIFO_Active", T20::C10_BMI::STATUS_TEXT_MAX);
    return true;
}

void IRAM_ATTR T20_onBmiDrdyISR() {
    if (g_t20_instance && g_t20_instance->_impl) {
        auto p = g_t20_instance->_impl;
        p->bmi270_drdy_isr_flag = 1;
        
        // Polling 방식 대신 Task Notification으로 SensorTask를 즉시 깨움
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (p->sensor_task_handle != nullptr) {
            vTaskNotifyGiveFromISR(p->sensor_task_handle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

// 라이브러리 데이터 구조를 사용하여 샘플 읽기
// [최적화된 데이터 읽기] 
// FIFO를 사용할 경우 단일 샘플이 아닌 배치 단위로 읽어 링버퍼에 채웁니다. 
/* [데이터 읽기] 
   sensorTask에서 호출됨. 라이브러리의 getSensorData를 통해 float 변환된 데이터 획득 */
bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
    if (p == nullptr || p_out_sample == nullptr) return false;

    // 라이브러리가 내부적으로 SPI Burst Read 및 float 스케일링 수행
    if (p->bmi.getSensorData() != BMI2_OK) return false;

    // 설정된 축 모드에 따라 분석용 샘플 선택
    if (p->bmi270_axis_mode == T20::C10_BMI::AXIS_MODE_ACC_Z) {
        *p_out_sample = p->bmi.data.accelZ;
    } else {
        *p_out_sample = p->bmi.data.gyroZ; // 기본값: Gyro Z
    }

    // 웹 모니터링용 실시간 값 갱신
    p->bmi270_last_axis_values[0] = p->bmi.data.gyroX;
    p->bmi270_last_axis_values[1] = p->bmi.data.gyroY;
    p->bmi270_last_axis_values[2] = p->bmi.data.gyroZ;
    
    return true;
}

bool T20_bmi270InstallDrdyHook(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    pinMode(T20::C10_Pin::BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(T20::C10_Pin::BMI_INT1), T20_onBmiDrdyISR, RISING);
    p->bmi_runtime.isr_hook = EN_T20_STATE_DONE;
    return true;
}

// [4-3] 센서 헬스 체크 및 자동 재초기화
// 직접 레지스터에 쓰던 Reset 명령을 라이브러리 API로 대체 
bool T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    p->bmi_state.master = EN_T20_STATE_BUSY;
    if (p->bmi.reset() == BMI2_OK) {
        delay(50);
        return T20_initBMI270_SPI(p);
    }
    return false;
}

// 빌드 호환성을 위한 래퍼 함수
bool T20_bmi270_LoadProductionConfig(CL_T20_Mfcc::ST_Impl* p) {
    return T20_initBMI270_SPI(p);
}

// [v216] FIFO 일괄 읽기 로직 보완 (핑퐁 버퍼 스위칭 연동)
/* ============================================================================
 * Function: T20_bmi270ReadFifoBatch
 * Summary: BMI270 FIFO 버스트 읽기 및 핑퐁 버퍼 투입 (v216 최적화)
 * ========================================================================== */
bool T20_bmi270ReadFifoBatch(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. FIFO에 쌓인 데이터 길이(Bytes) 확인
    uint16_t fifo_bytes = 0;
    if (p->bmi.getFIFOLength(&fifo_bytes) != BMI2_OK || fifo_bytes == 0) {
        return false;
    }
    
    // 2. FIFO 데이터 일괄 획득을 위한 임시 버퍼 선언
    static constexpr uint16_t MAX_FIFO_FRAMES = 32U;
    BMI270_SensorData fifo_buffer[MAX_FIFO_FRAMES];
    uint16_t frames_to_read = MAX_FIFO_FRAMES; 

    // SparkFun API: getFIFOData(데이터배열, 읽을개수포인터)
    if (p->bmi.getFIFOData(fifo_buffer, &frames_to_read) != BMI2_OK) {
        return false;
    }

    // 3. 획득한 프레임들 순차 처리
    for (uint16_t i = 0; i < frames_to_read; ++i) {
        float sample = 0.0f;

        // 설정된 분석 축에 따라 샘플 추출
        if (p->bmi270_axis_mode == T20::C10_BMI::AXIS_MODE_ACC_Z) {
            sample = fifo_buffer[i].accelZ;
        } else {
            sample = fifo_buffer[i].gyroZ;
        }

        /* [v216 최적화] 핑퐁 버퍼 연동 및 인덱스 관리 */
        uint8_t write_idx = p->active_fill_buffer; // 핑퐁 버퍼 쓰기 인덱스
        uint16_t sample_idx = p->active_sample_index % T20::C10_DSP::FFT_SIZE;
        
        p->frame_buffer[write_idx][sample_idx] = sample;
        
        p->active_sample_index++;
        p->bmi270_sample_counter++; 

        // 4. Hop Size 도달 시 분석 태스크 알림 및 버퍼 스위칭
        if ((p->active_sample_index % p->cfg.feature.hop_size) == 0 && 
             p->active_sample_index >= T20::C10_DSP::FFT_SIZE) {
            
            ST_T20_FrameMessage_t msg;
            msg.frame_index = write_idx;
            
            if (xQueueSend(p->frame_queue, &msg, 0) == pdPASS) {
                // 데이터 전달 성공 시 핑퐁 스위칭 (0 -> 1 -> 2 -> 3 -> 0)
                p->active_fill_buffer = (write_idx + 1) % T20::C10_Sys::RAW_FRAME_BUFFERS;
            } else {
                p->dropped_frames++; 
            }
        }
    }

    return (frames_to_read > 0);
}


/* ============================================================================
 * [Direct SPI Helpers] 라이브러리 private 은닉 우회를 위한 직접 통신 함수
 * ========================================================================== */

// BMI270 SPI Read 규격: (주소 | 0x80) 전송 -> Dummy 바이트(0x00) 전송 -> 수신
static bool T20_bmi270_ReadRegs_Direct(CL_T20_Mfcc::ST_Impl* p, uint8_t reg, uint8_t* data, uint16_t len) {
    if (p == nullptr) return false;
    
    p->spi.beginTransaction(SPISettings(T20::C10_BMI::SPI_FREQ_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(T20::C10_Pin::BMI_CS, LOW);
    
    p->spi.transfer(reg | 0x80); // Read 플래그 (MSB 1)
    p->spi.transfer(0x00);       // Dummy 바이트 필수
    
    for (uint16_t i = 0; i < len; i++) {
        data[i] = p->spi.transfer(0x00);
    }
    
    digitalWrite(T20::C10_Pin::BMI_CS, HIGH);
    p->spi.endTransaction();
    return true;
}

// BMI270 SPI Write 규격: (주소 & 0x7F) 전송 -> 데이터 연속 송신
static bool T20_bmi270_WriteRegs_Direct(CL_T20_Mfcc::ST_Impl* p, uint8_t reg, const uint8_t* data, uint16_t len) {
    if (p == nullptr) return false;

    p->spi.beginTransaction(SPISettings(T20::C10_BMI::SPI_FREQ_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(T20::C10_Pin::BMI_CS, LOW);
    
    p->spi.transfer(reg & 0x7F); // Write 플래그 (MSB 0)
    
    for (uint16_t i = 0; i < len; i++) {
        p->spi.transfer(data[i]);
    }
    
    digitalWrite(T20::C10_Pin::BMI_CS, HIGH);
    p->spi.endTransaction();
    return true;
}

/* ============================================================================
 * [1] 캘리브레이션 실행 및 결과를 LittleFS에 JSON으로 저장 (수동 트리거)
 * ========================================================================== */
bool T20_bmi270_RunAndSaveCalibration(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 측정 중단 및 센서 안정화 대기
    bool was_measuring = p->measurement_active;
    p->measurement_active = false;
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // 1. 센서 내장 캘리브레이션 엔진 구동
    p->bmi.performComponentRetrim();
    p->bmi.performAccelOffsetCalibration(BMI2_GRAVITY_POS_Z); // Z축을 중력 방향으로 상정
    p->bmi.performGyroOffsetCalibration();

    // 2. Direct SPI로 7바이트 오프셋 추출
    uint8_t offsets[T20::C10_BMI::REG_OFFSET_LEN] = {0};
    if (!T20_bmi270_ReadRegs_Direct(p, T20::C10_BMI::REG_OFFSET_START, offsets, T20::C10_BMI::REG_OFFSET_LEN)) {
        p->measurement_active = was_measuring;
        return false;
    }

    // 3. 추출된 값을 LittleFS에 영구 저장
    JsonDocument doc;
    doc["calibrated"] = true;
    doc["timestamp"] = millis();
    JsonArray offset_array = doc["offsets"].to<JsonArray>();
    for (uint8_t i = 0; i < T20::C10_BMI::REG_OFFSET_LEN; i++) {
        offset_array.add(offsets[i]);
    }

    File file = LittleFS.open(T20::C10_Path::LFS_FILE_BMI_CALIB, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    }

    p->measurement_active = was_measuring;
    return true;
}

/* ============================================================================
 * [2] LittleFS에서 보정값을 읽어와 BMI270 센서에 주입 (부팅 시 자동 실행)
 * ========================================================================== */
bool T20_bmi270_ApplyStoredCalibration(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;
    if (!LittleFS.exists(T20::C10_Path::LFS_FILE_BMI_CALIB)) return false;

    File file = LittleFS.open(T20::C10_Path::LFS_FILE_BMI_CALIB, "r");
    if (!file) return false;

    String json_text = file.readString();
    file.close();

    JsonDocument doc;
    if (deserializeJson(doc, json_text)) return false;

    JsonArray offset_array = doc["offsets"].as<JsonArray>();
    if (offset_array.size() != T20::C10_BMI::REG_OFFSET_LEN) return false;

    uint8_t offsets[T20::C10_BMI::REG_OFFSET_LEN] = {0};
    for (uint8_t i = 0; i < T20::C10_BMI::REG_OFFSET_LEN; i++) {
        offsets[i] = (uint8_t)offset_array[i];
    }

    // Direct SPI를 통해 센서 레지스터에 강제 주입
    if (T20_bmi270_WriteRegs_Direct(p, T20::C10_BMI::REG_OFFSET_START, offsets, T20::C10_BMI::REG_OFFSET_LEN)) {
        strlcpy(p->bmi270_status_text, "Calib_Injected", T20::C10_BMI::STATUS_TEXT_MAX);
        return true;
    }
    return false;
}
