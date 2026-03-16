/*
 * ------------------------------------------------------
 * 소스명 : SB10_BM217_010.h
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
 * [VQF Tuning Guide] - C10_Config_008.h 내 수정 시 참고
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
#include "C10_Config_009.h"
#include "SD10_SDMMC_009.h"



#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 변환 상수
static constexpr float G_SB10_DEG_TO_RAD = 0.0174532925F; // M_PI / 180.0f;
static constexpr float G_SB10_RAD_TO_DEG = 57.29577951F;  // 180.0f / M_PI;   

// 인라인 함수 형태
inline float SB10_deg2rad(float deg) { return deg * G_SB10_DEG_TO_RAD; }
inline float SB10_rad2deg(float rad) { return rad * G_SB10_RAD_TO_DEG; }



// 데이터 페이로드 정의
struct ST_FullSensorPayload_t {
    uint32_t 	timestamp;

    float 		rawAcc[3];  // 원시 가속도(중력 포함) 단위 g (1g = 9.80665m/s^2 )
    float 		linAcc[3];  // 선형 가속도(중력 제거) 
	// float 		acc[3];
	
	float 		gyro[3];    // 단위 rad/s
	float 		quat[4];    //
	float 		euler[3];   // 

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
        // [개선] FIFO 인터럽트가 발생했으므로 워터마크만큼 데이터를 한 번에 가져옴
        uint16_t samplesRead = FIFO_WTM_COUNT;
        int8_t rslt = _imu.getFIFOData(_fifoBuffer, &samplesRead);
    
        if (rslt != BMI2_OK || samplesRead == 0) return;
    
        float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;
    
        // 2. FIFO 루프 처리: 쌓인 모든 데이터를 VQF에 순차 입력
        for (uint16_t i = 0; i < samplesRead; i++) {
            float ax = _fifoBuffer[i].accelX;
            float ay = _fifoBuffer[i].accelY;
            float az = _fifoBuffer[i].accelZ;
            float gx = _fifoBuffer[i].gyroX * G_SB10_DEG_TO_RAD;
            float gy = _fifoBuffer[i].gyroY * G_SB10_DEG_TO_RAD;
            float gz = _fifoBuffer[i].gyroZ * G_SB10_DEG_TO_RAD;
    
            if (_opts.useVQF && _vqf) {
                xyz_t v_gyr_v = {gx, gy, gz};
                xyz_t v_acc_v = {ax, ay, az};
    
                // 필터 업데이트 (누적된 모든 샘플 반영)
                Quaternion v_q = _vqf->update_orientation(v_gyr_v, v_acc_v, v_dt);
    
                // 마지막 샘플 결과를 최종 페이로드에 저장 (사용자에게 보고될 최종 자세)
                if (i == samplesRead - 1) {
                    p_sensor_data.rawAcc[0] = ax; 
                    p_sensor_data.rawAcc[1] = ay; 
                    p_sensor_data.rawAcc[2] = az;
                    
                    p_sensor_data.gyro[0] = gx; 
                    p_sensor_data.gyro[1] = gy; 
                    p_sensor_data.gyro[2] = gz;
                    
                    p_sensor_data.quat[0] = v_q.w; 
                    p_sensor_data.quat[1] = v_q.x;
                    p_sensor_data.quat[2] = v_q.y; 
                    p_sensor_data.quat[3] = v_q.z;
    
                    // 3. 선형 가속도 계산 (중력 보정)
                    float v_gx = 2.0f * (v_q.x * v_q.z - v_q.w * v_q.y);
                    float v_gy = 2.0f * (v_q.w * v_q.x + v_q.y * v_q.z);
                    float v_gz = v_q.w * v_q.w - v_q.x * v_q.x - v_q.y * v_q.y + v_q.z * v_q.z;
    
                    p_sensor_data.linAcc[0] = ax - v_gx;
                    p_sensor_data.linAcc[1] = ay - v_gy;
                    p_sensor_data.linAcc[2] = az - v_gz;
    
                    computeEuler(p_sensor_data);
                }
            }
        }
        p_sensor_data.timestamp = millis();
        _imu.getStepCount(&p_sensor_data.stepCount);
    }

    
    /*
    void updateProcess(ST_FullSensorPayload_t& p_sensor_data) {
        _imu.getSensorData(); // 실제 환경에선 FIFO Read로 대체 가능
        p_sensor_data.timestamp = millis();

        // 1. Raw 가속도 데이터 저장 (단위: g)
        p_sensor_data.rawAcc[0] 	= _imu.data.accelX;
		p_sensor_data.rawAcc[1] 	= _imu.data.accelY;
		p_sensor_data.rawAcc[2] 	= _imu.data.accelZ;
		
		// 2. gyro 단위 변환 및 저장 (Degrees/s -> Rad/s)
        p_sensor_data.gyro[0] 	= _imu.data.gyroX * G_SB10_DEG_TO_RAD;
        p_sensor_data.gyro[1] 	= _imu.data.gyroY * G_SB10_DEG_TO_RAD;
        p_sensor_data.gyro[2] 	= _imu.data.gyroZ * G_SB10_DEG_TO_RAD;

        // VQF 융합 연산 (자이로 바이어스는 내부에서 자동 추적됨)
        if (_opts.useVQF && _vqf) {
            xyz_t v_gyr_v = {p_sensor_data.gyro[0], p_sensor_data.gyro[1], p_sensor_data.gyro[2]};
			xyz_t v_acc_v = {p_sensor_data.rawAcc[0], p_sensor_data.rawAcc[1], p_sensor_data.rawAcc[2]};

			// 가상 함수 이름 사용
			float v_dt = 1.0f / C10_Config::SAMPLE_RATE_ACTIVE;
			// VQF를 통한 자세 추정 (6축 융합)
			Quaternion v_q = _vqf->updateOrientation(v_gyr_v, v_acc_v, v_dt);

			p_sensor_data.quat[0] = v_q.w;
			p_sensor_data.quat[1] = v_q.x;
			p_sensor_data.quat[2] = v_q.y;
			p_sensor_data.quat[3] = v_q.z;
			
			// 3. 순수 선가속도(Linear Acceleration) 계산
            // Body Frame에서의 중력 벡터 성분 계산 (단위: g)
            // QuaternionG 클래스의 gravity() 함수가 있다면 호출 가능하나, 
            // 여기서는 직접 계산식을 적용합니다 (오타 수정: q -> v_q)
            float v_gx = 2.0f * (v_q.x * v_q.z - v_q.w * v_q.y);
            float v_gy = 2.0f * (v_q.w * v_q.x + v_q.y * v_q.z);
            float v_gz = v_q.w * v_q.w - v_q.x * v_q.x - v_q.y * v_q.y + v_q.z * v_q.z;
    
            // 선형 가속도 = 전체 가속도 - 중력 성분
            p_sensor_data.linAcc[0] = p_sensor_data.rawAcc[0] - v_gx;
            p_sensor_data.linAcc[1] = p_sensor_data.rawAcc[1] - v_gy;
            p_sensor_data.linAcc[2] = p_sensor_data.rawAcc[2] - v_gz;


            // 필요 시 m/s^2으로 변환하려면 아래와 같이 9.80665f를 곱해줍니다.
            // p_sensor_data.linAcc[0] *= 9.80665f;

			computeEuler(p_sensor_data);

        }

        p_sensor_data.motion = _isSignificantMoving;
        _imu.getStepCount(&p_sensor_data.stepCount);
    }
    */
    
    void enterIdleMode() {
        Serial.println(F(">>> System: Prepare Light Sleep"));
        
        // 1. 슬립 전 FIFO 비우기 (복귀 시 엉킨 데이터 처리 방지)
        _imu.flushFIFO(); 
    
        // 2. 가속도 센서만 저전력 모드로 (Any-Motion 모니터링용)
        // 자이로는 전력 소모가 매우 크므로 중단(Suspend)
        _imu.setGyroPowerMode(BMI2_SUSPEND_POWER_MODE);
        _imu.setAccelPowerMode(BMI2_LOW_POWER_MODE);
    
        if (g_A10_TaskHandle_Sensor != NULL) vTaskSuspend(g_A10_TaskHandle_Sensor);
    
        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();
    
        // --- 슬립 복구 시점 ---
        resumeFromIdle();
        if (g_A10_TaskHandle_Sensor != NULL) vTaskResume(g_A10_TaskHandle_Sensor);
    }



    /*

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
    */


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
    
    BMI270_SensorData           _fifoBuffer[C10_Config::FIFO_WTM_COUNT];

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

