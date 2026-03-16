/*
 * ------------------------------------------------------
 * 소스명 : T03_BM217_008.h
 * 모듈약어 : T03 (BMI270)
 * 모듈명 : Advanced Motion Engine with VQF & FIFO Interrupt
 * ------------------------------------------------------
 * 기능 요약
 * - FIFO 워터마크 인터럽트를 이용한 센서 데이터 수집
 * - VQF 필터 기반 6축 데이터 융합 (자이로 바이어스 동적 보정)
 * - Any/Significant/No Motion 3단계 전력 관리 로직
 * - ESP32-S3 Light Sleep 및 하드웨어 인터럽트 복구 제어
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "C10_Config_008.h"
#include "SD10_SDMMC_008.h"

// 데이터 페이로드 정의
struct ST_FullSensorPayload_t {
    uint32_t timestamp;
    float acc[3], gyro[3], quat[4], euler[3];
    uint32_t stepCount;
    bool motion;
};

// 인터럽트 플래그 (RAM 상주)
DRAM_ATTR volatile bool g_SB10_sensor_data_ready = false;
void IRAM_ATTR SB10_bmi270_fifo_isr() { g_SB10_sensor_data_ready = true; }

class CL_SB10_BMI270_Handler {
public:
    bool begin(ST_BMI270_Options_t opts, CL_SD10_SDMMC_Handler* sd) {
        _opts = opts; _sd = sd;
        if (_imu.beginSPI(C10_Config::BMI_CS) != BMI2_OK) return false;

        // 1. VQF 초기화 및 튜닝 (자이로 바이어스 추적 속도 설정)
        if (_opts.useVQF) {
			float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;

			// 생성자는 헤더의 VQF(float gyro_delta_t, float acc_delta_t, float mag_delta_t)와 일치
			_vqf = new VQF(v_dt, v_dt, v_dt);

			// SensorFusionFilterBase 클래스의 가상 함수 이름 사용
			_vqf->setFreeParameters(C10_Config::VQF_TAU_ACC, 0.0f);

        }

        // 2. FIFO 및 인터럽트 설정
        configureIMU();
        setupMotionInterrupts();

        pinMode(C10_Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(C10_Config::BMI_INT1), SB10_bmi270_fifo_isr, RISING);

        return true;
    }

    // [기능 추가] FIFO 및 센서 하드웨어 설정
    void configureIMU() {
        // FIFO 워터마크 및 가속도/자이로 동시 활성화
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);

        BMI270_FIFOConfig fcfg;
        fcfg.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        fcfg.watermark = C10_Config::FIFO_WTM;
        _imu.setFIFOConfig(fcfg);

        // FIFO 워터마크 인터럽트를 INT1 핀에 매핑, 라이브러리 호환 상수 사용 (FIFO WaterMark)
    	_imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);
    }

    void updateProcess(ST_FullSensorPayload_t& d) {
        _imu.getSensorData(); // 실제 환경에선 FIFO Read로 대체 가능
        d.timestamp = millis();

        // Raw 데이터 정규화 및 단위 변환 (DPS -> Rad/s)
        d.acc[0] = _imu.data.accelX; d.acc[1] = _imu.data.accelY; d.acc[2] = _imu.data.accelZ;
        d.gyro[0] = _imu.data.gyroX * 0.0174533f;
        d.gyro[1] = _imu.data.gyroY * 0.0174533f;
        d.gyro[2] = _imu.data.gyroZ * 0.0174533f;

        // VQF 융합 연산 (자이로 바이어스는 내부에서 자동 추적됨)
        if (_opts.useVQF && _vqf) {
            xyz_t gyr_v = {d.gyro[0], d.gyro[1], d.gyro[2]};
			xyz_t acc_v = {d.acc[0], d.acc[1], d.acc[2]};

			// [수정 완료] 헤더 파일 39라인의 가상 함수 이름 사용
			Quaternion q = _vqf->updateOrientation(gyr_v, acc_v, 1.0f / C10_Config::SAMPLE_RATE_ACTIVE);

			d.quat[0] = q.w; d.quat[1] = q.x; d.quat[2] = q.y; d.quat[3] = q.z;
			computeEuler(d);

        }

        d.motion = _isSignificantMoving;
        _imu.getStepCount(&d.stepCount);
    }

    void enterIdleMode() {
        Serial.println(">>> Sleep Mode: SD End & Light Sleep Start");
        if (_opts.useSD) _sd->end();

        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE); // 가속도계만 저전력 가동
        _imu.enableAdvancedPowerSave();

        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();

        resumeFromIdle();
    }

    void checkMotionStatus() {
        uint16_t status = 0;
        _imu.getInterruptStatus(&status);

        if (status & BMI270_SIG_MOT_STATUS_MASK) {
            _isSignificantMoving = true;
        }
        if (status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            enterIdleMode();
        }
    }

    bool shouldRecord() { return (!_opts.recordOnlySignificant) || _isSignificantMoving; }

private:
    BMI270 _imu;
    ST_BMI270_Options_t _opts;
    CL_SD10_SDMMC_Handler* _sd;
    VQF* _vqf = nullptr;
    bool _isSignificantMoving = false;

    void computeEuler(ST_FullSensorPayload_t& d) {
        float q0 = d.quat[0], q1 = d.quat[1], q2 = d.quat[2], q3 = d.quat[3];
        d.euler[0] = atan2(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * 57.29578f;
        d.euler[1] = asin(2.0f * (q0 * q2 - q3 * q1)) * 57.29578f;
        d.euler[2] = atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q3 * q3 + q2 * q2)) * 57.29578f;
    }

    void resumeFromIdle() {
        Serial.println("<<< Wakeup: Restoring Sensor & SD");
        _imu.disableAdvancedPowerSave();
        _imu.setAccelPowerMode(BMI2_PERF_OPT_MODE);
        configureIMU();

        if (_opts.useSD) {
            _sd->begin();
            _sd->createLogFile(_opts.logPrefix, "Time,Ax,Ay,Az,Gx,Gy,Gz,QW,QX,QY,QZ,Roll,Pitch,Yaw,Steps,Sig");
        }
    }

    void setupMotionInterrupts() {
        _imu.enableFeature(BMI2_ANY_MOTION);
        _imu.enableFeature(BMI2_NO_MOTION);
        _imu.enableFeature(BMI2_SIG_MOTION);
        _imu.mapInterruptToPin(BMI2_ANY_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_SIG_MOTION_INT, BMI2_INT1);
        _imu.mapInterruptToPin(BMI2_NO_MOTION_INT, BMI2_INT1);
    }
};

