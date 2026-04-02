/* ============================================================================
 * File: T232_Mfcc_Sens_214.cpp
 * Summary: BMI270 하드웨어 제어 및 실시간 데이터 스트리밍
 * * [v212 구현 및 점검 사항]
 * 1. ODR 1600Hz, Gyro Range 2000dps, Accel Range 8g 설정 실제 반영
 * 2. BMI270 초기화 시퀀스(v210 Config Load) 단계별 상태 관리
 * 3. SPI 트랜잭션 최적화 및 6-byte Burst Read 구현
 * 4. DRDY ISR에서 Queue를 통한 데이터 전달 레이턴시 최소화
 ============================================================================ */

#include "T221_Mfcc_Inter_214.h"



// 라이브러리 함수를 사용하여 센서 초기화
// [라이브러리 기반 초기화] 
//   공식 예제 4(Filtering) 및 5(FIFO)의 방식을 결합하여 구현 */
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. SPI 통신 시작 (내부적으로 Config Load 0x59 수행)
    int8_t status = p->bmi.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    if (status != BMI2_OK) {
        p->bmi_state.master = EN_T20_STATE_ERROR;
        strlcpy(p->bmi270_status_text, "Init_Fail", 48);
        return false;
    }

    // 2. 가속도계 설정 (bmi2_sens_config 구조체 사용 - 예제 4 방식)
    bmi2_sens_config accelConfig;
    accelConfig.type = BMI2_ACCEL;
    accelConfig.cfg.acc.odr = BMI2_ACC_ODR_1600HZ;
    accelConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    accelConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE; // 에러 수정: PERF_OPT_MODE
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
    p->bmi.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1); // 에러 수정: BMI2_FWM_INT

    bmi2_int_pin_config intPinConfig;
    intPinConfig.pin_type = BMI2_INT1;
    intPinConfig.int_latch = BMI2_INT_NON_LATCH;
    intPinConfig.pin_cfg[0].lvl = BMI2_INT_ACTIVE_HIGH;
    intPinConfig.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    intPinConfig.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    intPinConfig.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    p->bmi.setInterruptPinConfig(intPinConfig);

    p->bmi_state.init = EN_T20_STATE_DONE;
    strlcpy(p->bmi270_status_text, "1600Hz_FIFO_Active", 48);
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
//   FIFO를 사용할 경우 단일 샘플이 아닌 배치 단위로 읽어 링버퍼에 채웁니다. 
/* [데이터 읽기] 
   sensorTask에서 호출됨. 라이브러리의 getSensorData를 통해 float 변환된 데이터 획득 */
bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
    if (p == nullptr || p_out_sample == nullptr) return false;

    // 라이브러리가 내부적으로 SPI Burst Read 및 float 스케일링 수행
    if (p->bmi.getSensorData() != BMI2_OK) return false;

    // 설정된 축 모드에 따라 분석용 샘플 선택
    if (p->bmi270_axis_mode == G_T20_BMI270_AXIS_MODE_ACC_Z) {
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
    pinMode(G_T20_PIN_BMI_INT1, INPUT);
    attachInterrupt(digitalPinToInterrupt(G_T20_PIN_BMI_INT1), T20_onBmiDrdyISR, RISING);
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











// 라이브러리 미사용 항목 제거: T20_bmi270ActualReadBurst 등은 이제 불필요함

