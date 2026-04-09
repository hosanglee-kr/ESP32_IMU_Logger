/* ============================================================================
 * File: T232_Sensor_Engine_218.cpp
 * Summary: BMI270 Driver & Data Acquisition Engine Implementation (v217)
 * Compiler: gnu++17 / ESP32-S3 Optimized
 * ========================================================================== */

#include "T232_Sensor_Engine_218.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

CL_T20_SensorEngine::CL_T20_SensorEngine(SPIClass& spi_bus) : _spi(spi_bus), _bmi() {
    memset(_status_text, 0, sizeof(_status_text));
    strlcpy(_status_text, "idle", sizeof(_status_text));
}

bool CL_T20_SensorEngine::begin(const ST_T20_ConfigSensor_t& s_cfg) {
    // [1] SPI 통신 시작 (Library beginSPI)
    int8_t rslt = _bmi.beginSPI(T20::C10_Pin::BMI_CS, T20::C10_BMI::SPI_FREQ_HZ, _spi);
    if (rslt != BMI2_OK) {
        strlcpy(_status_text, "spi_init_fail", sizeof(_status_text));
        return false;
    }

    // [2] 가속도계 설정 (1600Hz, Normal Avg4, Range 적용)
    bmi2_sens_config accelConfig;
    accelConfig.type = BMI2_ACCEL;
    accelConfig.cfg.acc.odr = BMI2_ACC_ODR_1600HZ;
    accelConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    accelConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE; 
    accelConfig.cfg.acc.range = _mapAccelRange(s_cfg.accel_range);
    _bmi.setConfig(accelConfig);

    // [3] 자이로스코프 설정 (1600Hz, Normal, Range 적용)
    bmi2_sens_config gyroConfig;
    gyroConfig.type = BMI2_GYRO;
    gyroConfig.cfg.gyr.odr = BMI2_GYR_ODR_1600HZ;
    gyroConfig.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    gyroConfig.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    gyroConfig.cfg.gyr.noise_perf = BMI2_PERF_OPT_MODE;
    gyroConfig.cfg.gyr.range = _mapGyroRange(s_cfg.gyro_range);
    _bmi.setConfig(gyroConfig);

    // [4] FIFO 설정 (Accel + Gyro 통합 활성화)
    BMI270_FIFOConfig fifoConfig;
    fifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN;
    fifoConfig.watermark = 16; // 16샘플(약 10ms) 마다 인터럽트 발생 트리거
    fifoConfig.accelFilter = BMI2_ENABLE;
    fifoConfig.gyroFilter = BMI2_ENABLE;
    _bmi.setFIFOConfig(fifoConfig);

    // [5] 인터럽트 핀 설정 (INT1 - Active High, Push-Pull)
    _bmi.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1); 
    bmi2_int_pin_config intPinConfig;
    intPinConfig.pin_type = BMI2_INT1;
    intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    _bmi.setInterruptPinConfig(intPinConfig);

    // [6] 기존 보정값이 있다면 주입
    applyStoredCalibration();

    _initialized = true;
    strlcpy(_status_text, "1600Hz_active", sizeof(_status_text));
    return true;
}

uint16_t CL_T20_SensorEngine::readFifoBatch(float* p_out_buffer, uint16_t max_frames, EM_T20_SensorAxis_t target_axis) {
    if (!_initialized || !p_out_buffer) return 0;

    uint16_t fifo_bytes = 0;
    if (_bmi.getFIFOLength(&fifo_bytes) != BMI2_OK || fifo_bytes == 0) return 0;

    // FIFO 프레임 일괄 획득 (Library internal scales to float)
    static BMI270_SensorData fifo_raw[32]; // 정적 버퍼로 스택 절약
    uint16_t frames_to_read = (fifo_bytes / 12); // Accel(6) + Gyro(6) = 12 bytes per frame
    if (frames_to_read > 32) frames_to_read = 32;
    if (frames_to_read > max_frames) frames_to_read = max_frames;

    if (_bmi.getFIFOData(fifo_raw, &frames_to_read) != BMI2_OK) return 0;

    // 요청된 축(Axis)에 맞춰 데이터 추출
    for (uint16_t i = 0; i < frames_to_read; i++) {
        switch (target_axis) {
            case EN_T20_AXIS_ACCEL_X: p_out_buffer[i] = fifo_raw[i].accelX; break;
            case EN_T20_AXIS_ACCEL_Y: p_out_buffer[i] = fifo_raw[i].accelY; break;
            case EN_T20_AXIS_ACCEL_Z: p_out_buffer[i] = fifo_raw[i].accelZ; break;
            case EN_T20_AXIS_GYRO_X:  p_out_buffer[i] = fifo_raw[i].gyroX;  break;
            case EN_T20_AXIS_GYRO_Y:  p_out_buffer[i] = fifo_raw[i].gyroY;  break;
            case EN_T20_AXIS_GYRO_Z:  p_out_buffer[i] = fifo_raw[i].gyroZ;  break;
            default:                  p_out_buffer[i] = fifo_raw[i].accelZ; break;
        }
    }

    return frames_to_read;
}

bool CL_T20_SensorEngine::runCalibration() {
    // 센서 내장 캘리브레이션 엔진 구동
    _bmi.performComponentRetrim();
    _bmi.performAccelOffsetCalibration(BMI2_GRAVITY_POS_Z); 
    _bmi.performGyroOffsetCalibration();

    // Direct SPI로 7바이트 오프셋 추출 (0x71 ~ 0x77)
    uint8_t offsets[7] = {0};
    if (!_readRegs(T20::C10_BMI::REG_CALIB_OFFSET_START, offsets, 7)) return false;

    // 결과를 LittleFS에 저장
    JsonDocument doc;
    JsonArray arr = doc["offsets"].to<JsonArray>();
    for (int i = 0; i < 7; i++) arr.add(offsets[i]);

    File f = LittleFS.open(T20::C10_Path::FILE_BMI_CALIB, "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
    }
    return true;
}

// 파일 하단에 다음 함수를 추가합니다.
bool CL_T20_SensorEngine::enableWakeOnMotion(float threshold_g) {
    if (!_initialized) return false;

    // 1. 센서 동작 모드를 저전력으로 변경
    _bmi.setConfigMode(BMI2_POWER_OPT_MODE);

    // 2. Any-Motion 기능 설정
    bmi2_sens_config any_mo_cfg;
    any_mo_cfg.type = BMI2_ANY_MOTION;
    
    // threshold_g 값을 센서 레지스터 스케일에 맞게 변환 (5.14mg / LSB)
    uint16_t th_reg = (uint16_t)(threshold_g * 1000.0f / 5.14f);
    if(th_reg > 0x03FF) th_reg = 0x03FF; // 10-bit max

    any_mo_cfg.cfg.any_motion.duration = 2; // 연속 2개 샘플 초과 시 트리거
    any_mo_cfg.cfg.any_motion.threshold = th_reg;
    any_mo_cfg.cfg.any_motion.out_conf = 0x00; // 모든 축 활성화
    any_mo_cfg.cfg.any_motion.axes_en = BMI2_EN_ALL_AXIS;

    if (_bmi.setConfig(any_mo_cfg) != BMI2_OK) return false;

    // 3. 기존 FIFO 인터럽트를 해제하고 Any-Motion 인터럽트를 INT1 핀에 매핑
    _bmi.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT_NONE); 
    _bmi.mapInterruptToPin(BMI2_ANY_MOT_INT, BMI2_INT1);

    strlcpy(_status_text, "wake_on_motion_ready", sizeof(_status_text));
    return true;
}



bool CL_T20_SensorEngine::applyStoredCalibration() {
    if (!LittleFS.exists(T20::C10_Path::FILE_BMI_CALIB)) return false;

    File f = LittleFS.open(T20::C10_Path::FILE_BMI_CALIB, "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();

    uint8_t offsets[7];
    JsonArray arr = doc["offsets"].as<JsonArray>();
    for (int i = 0; i < 7; i++) offsets[i] = arr[i];

    return _writeRegs(T20::C10_BMI::REG_CALIB_OFFSET_START, offsets, 7);
}

void CL_T20_SensorEngine::resetHardware() {
    _bmi.reset();
    _initialized = false;
    strlcpy(_status_text, "reset", sizeof(_status_text));
}

// --- Private Helpers ---

bool CL_T20_SensorEngine::_readRegs(uint8_t reg, uint8_t* data, uint16_t len) {
    _spi.beginTransaction(SPISettings(T20::C10_BMI::SPI_FREQ_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(T20::C10_Pin::BMI_CS, LOW);
    _spi.transfer(reg | 0x80); // Read Flag
    _spi.transfer(0x00);       // Dummy
    for (uint16_t i = 0; i < len; i++) data[i] = _spi.transfer(0x00);
    digitalWrite(T20::C10_Pin::BMI_CS, HIGH);
    _spi.endTransaction();
    return true;
}

bool CL_T20_SensorEngine::_writeRegs(uint8_t reg, const uint8_t* data, uint16_t len) {
    _spi.beginTransaction(SPISettings(T20::C10_BMI::SPI_FREQ_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(T20::C10_Pin::BMI_CS, LOW);
    _spi.transfer(reg & 0x7F); // Write Flag
    for (uint16_t i = 0; i < len; i++) _spi.transfer(data[i]);
    digitalWrite(T20::C10_Pin::BMI_CS, HIGH);
    _spi.endTransaction();
    return true;
}

uint8_t CL_T20_SensorEngine::_mapAccelRange(EM_T20_AccelRange_t r) {
    switch(r) {
        case EN_T20_ACCEL_2G: return BMI2_ACC_RANGE_2G;
        case EN_T20_ACCEL_4G: return BMI2_ACC_RANGE_4G;
        case EN_T20_ACCEL_16G: return BMI2_ACC_RANGE_16G;
        default: return BMI2_ACC_RANGE_8G;
    }
}

uint8_t CL_T20_SensorEngine::_mapGyroRange(EM_T20_GyroRange_t r) {
    switch(r) {
        case EN_T20_GYRO_125: return BMI2_GYR_RANGE_125;
        case EN_T20_GYRO_250: return BMI2_GYR_RANGE_250;
        case EN_T20_GYRO_500: return BMI2_GYR_RANGE_500;
        case EN_T20_GYRO_1000: return BMI2_GYR_RANGE_1000;
        default: return BMI2_GYR_RANGE_2000;
    }
}
