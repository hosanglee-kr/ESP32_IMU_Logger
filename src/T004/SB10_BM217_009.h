/*
 * ------------------------------------------------------
 * 소스명 : SB10_BM217_009.h
 * 모듈약어 : SB10 (BMI270)
 * 모듈명 : Advanced Motion Engine with VQF & FIFO Interrupt
 * ------------------------------------------------------
 * 기능 요약
 * - FIFO 워터마크 인터럽트를 이용한 센서 데이터 수집
 * - VQF 필터 기반 6축 데이터 융합 (자이로 바이어스 동적 보정)
 * - Any/Significant/No Motion 3단계 전력 관리 로직
 * - ESP32-S3 Light Sleep 및 하드웨어 인터럽트 복구 제어
 * - 이벤트 기반 동기화 :기존 bool 플래그 방식 대신 Binary Semaphore를 적용하여 인터럽트 발생 시에만 태스크가 동작하도록 최적화됨.
 *   장점: 200Hz 타이밍의 극심한 정확도 확보 및 불필요한 CPU 소모 방지.
 * ------------------------------------------------------
 *
 * ------------------------------------------------------
  * ------------------------------------------------------
 * [VQF Tuning Guide] - C10_Config_008.h 내 수정 시 참고
 * ------------------------------------------------------
 * 1. VQF_TAU_ACC (기본 3.0s): 가속도계 보정 속도
 * - 낮을수록(1.0s) : 기울기 변화에 빠르게 반응하지만 진동에 취약함.
 * - 높을수록(5.0s) : 동작이 부드럽고 진동에 강하지만 정적 기울기 복귀가 느림.
 * 2. FIFO_WTM (200 bytes):
 * - 한 샘플(Acc+Gyr+Header)이 약 13byte이므로, 약 15개 데이터마다 인터럽트 발생.
 * - 200Hz 기준 약 75ms마다 CPU가 깨어나 처리함 (전력 효율 최적점).
 * ------------------------------------------------------


 */

#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "C10_Config_008.h"
#include "SD10_SDMMC_008.h"

// 데이터 페이로드 정의
struct ST_FullSensorPayload_t {
    uint32_t 	timestamp;

	float 		acc[3];
	float 		gyro[3];
	float 		quat[4];
	float 		euler[3];

    uint32_t 	stepCount;
    bool 		motion;
};

// 외부에서 정의된 세마포어 핸들 참조
extern SemaphoreHandle_t g_SB10_Sem_FIFO;

void IRAM_ATTR SB10_bmi270_fifo_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // 인터럽트 발생 시 세마포어를 해제하여 태스크를 깨움
    xSemaphoreGiveFromISR(g_SB10_Sem_FIFO, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// // 인터럽트 플래그 (RAM 상주)
// DRAM_ATTR volatile bool g_SB10_sensor_data_ready = false;
// void IRAM_ATTR SB10_bmi270_fifo_isr() { g_SB10_sensor_data_ready = true; }


class CL_SB10_BMI270_Handler {
public:
    bool begin(ST_BMI270_Options_t p_opts, CL_SD10_SDMMC_Handler* p_sd) {
        _opts = p_opts; _sd = p_sd;
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

        BMI270_FIFOConfig v_FifoConfig;
        v_FifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        v_FifoConfig.watermark = C10_Config::FIFO_WTM;
        _imu.setFIFOConfig(v_FifoConfig);

        // FIFO 워터마크 인터럽트를 INT1 핀에 매핑, 라이브러리 호환 상수 사용 (FIFO WaterMark)
    	_imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);
    }

    void updateProcess(ST_FullSensorPayload_t& p_sensor_data) {
        _imu.getSensorData(); // 실제 환경에선 FIFO Read로 대체 가능
        p_sensor_data.timestamp = millis();

        // Raw 데이터 정규화 및 단위 변환 (DPS -> Rad/s)
        p_sensor_data.acc[0] 	= _imu.data.accelX;
		p_sensor_data.acc[1] 	= _imu.data.accelY;
		p_sensor_data.acc[2] 	= _imu.data.accelZ;
        p_sensor_data.gyro[0] 	= _imu.data.gyroX * 0.0174533f;
        p_sensor_data.gyro[1] 	= _imu.data.gyroY * 0.0174533f;
        p_sensor_data.gyro[2] 	= _imu.data.gyroZ * 0.0174533f;

        // VQF 융합 연산 (자이로 바이어스는 내부에서 자동 추적됨)
        if (_opts.useVQF && _vqf) {
            xyz_t v_gyr_v = {p_sensor_data.gyro[0], p_sensor_data.gyro[1], p_sensor_data.gyro[2]};
			xyz_t v_acc_v = {p_sensor_data.acc[0], p_sensor_data.acc[1], p_sensor_data.acc[2]};

			// [수정 완료] 헤더 파일 39라인의 가상 함수 이름 사용
			Quaternion v_quaternion = _vqf->updateOrientation(v_gyr_v, v_acc_v, 1.0f / C10_Config::SAMPLE_RATE_ACTIVE);

			p_sensor_data.quat[0] = v_quaternion.w;
			p_sensor_data.quat[1] = v_quaternion.x;
			p_sensor_data.quat[2] = v_quaternion.y;
			p_sensor_data.quat[3] = v_quaternion.z;
			computeEuler(p_sensor_data);

        }

        p_sensor_data.motion = _isSignificantMoving;
        _imu.getStepCount(&p_sensor_data.stepCount);
    }

    // [SB10_BM217_008.h] 내 enterIdleMode 수정 (태스크 보호 로직)
    void enterIdleMode() {
        Serial.println(">>> System: Suspend Tasks & Entering Sleep");

		// 1. 센서/디버그 태스크 일시 정지 (스택 및 데이터 보호, 외부 핸들 참조
        if (g_A10_TaskHandle_Sensor != NULL) vTaskSuspend(g_A10_TaskHandle_Sensor);

		if (_opts.useSD) {
            vTaskDelay(pdMS_TO_TICKS(50)); // 로깅 태스크가 flush가 보장되도록 잠시 대기 후 종료
            _sd->end();
        }

        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        _imu.enableAdvancedPowerSave();


        // 2. Wakeup 설정 및 슬립 진입
        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();

        // 3. 복구 시퀀스
        resumeFromIdle();

        // 4. 태스크 재개
        if (g_A10_TaskHandle_Sensor != NULL) vTaskResume(g_A10_TaskHandle_Sensor);
        Serial.println("<<< System: Tasks Resumed");


    }


    void checkMotionStatus() {
        uint16_t v_status = 0;
        _imu.getInterruptStatus(&v_status);

        if (v_status & BMI270_SIG_MOT_STATUS_MASK) {
            _isSignificantMoving = true;
        }
        if (v_status & BMI270_NO_MOT_STATUS_MASK) {
            _isSignificantMoving = false;
            enterIdleMode();
        }
    }

    bool shouldRecord() {
		return (!_opts.recordOnlySignificant) || _isSignificantMoving;
	}

private:
    BMI270 						_imu;
    ST_BMI270_Options_t 		_opts;
    CL_SD10_SDMMC_Handler* 		_sd;
    VQF* 						_vqf 					= nullptr;
    bool 						_isSignificantMoving 	= false;

    void computeEuler(ST_FullSensorPayload_t& p_sensor_data) {
        float v_quat_0 = p_sensor_data.quat[0];
		float v_quat_1 = p_sensor_data.quat[1];
		float v_quat_2 = p_sensor_data.quat[2];
		float v_quat_3 = p_sensor_data.quat[3];

        p_sensor_data.euler[0] = atan2(2.0f * (v_quat_0 * v_quat_1 + v_quat_2 * v_quat_3), 1.0f - 2.0f * (v_quat_1 * v_quat_1 + v_quat_2 * v_quat_2)) * 57.29578f;
        p_sensor_data.euler[1] = asin(2.0f * (v_quat_0 * v_quat_2 - v_quat_3 * v_quat_1)) * 57.29578f;
        p_sensor_data.euler[2] = atan2(2.0f * (v_quat_0 * v_quat_3 + v_quat_1 * v_quat_2), 1.0f - 2.0f * (v_quat_3 * v_quat_3 + v_quat_2 * v_quat_2)) * 57.29578f;
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

