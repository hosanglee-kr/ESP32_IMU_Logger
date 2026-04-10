/* ============================================================================
 * File: T232_Sensor_Engine_219.h
 * Summary: BMI270 센서 드라이버 및 데이터 수집 엔진 (v217)
 * ========================================================================== */
#pragma once

#include "SparkFun_BMI270_Arduino_Library.h"
#include <SPI.h>

#include "T210_Def_Com_219.h"
#include "T214_Def_Rec_219.h"


class CL_T20_SensorEngine {
public:
    CL_T20_SensorEngine(SPIClass& spi_bus);
    ~CL_T20_SensorEngine() = default;

    // 센서 초기화 및 하드웨어 설정
    bool begin(const ST_T20_ConfigSensor_t& s_cfg);

    // FIFO 데이터 일괄 획득 (배치 처리용)
    uint16_t readFifoBatch(float* p_out_buffer, uint16_t max_frames, EM_T20_SensorAxis_t target_axis);

    // 캘리브레이션 관리
    bool runCalibration();
    bool applyStoredCalibration();

    // 딥슬립 전 Any-Motion (충격 감지) 인터럽트 활성화
    bool enableWakeOnMotion(float threshold_g, uint16_t duration);

    // 상태 조회
    const char* getStatusText() const { return _status_text; }
    void resetHardware();

private:
    // Direct SPI Helpers (캡슐화됨)
    bool _readRegs(uint8_t reg, uint8_t* data, uint16_t len);
    bool _writeRegs(uint8_t reg, const uint8_t* data, uint16_t len);


    uint8_t _mapAccelRange(EM_T20_AccelRange_t r);
    uint8_t _mapGyroRange(EM_T20_GyroRange_t r);


private:
    SPIClass& _spi;
    BMI270    _bmi;
    char      _status_text[32];
    bool      _initialized = false;
};

