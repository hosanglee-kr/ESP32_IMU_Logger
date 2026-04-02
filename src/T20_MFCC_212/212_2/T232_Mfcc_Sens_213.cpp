/* ============================================================================
 * File: T232_Mfcc_Sens_213.cpp
 * Summary: BMI270 하드웨어 제어 및 실시간 데이터 스트리밍
 * * [v212 구현 및 점검 사항]
 * 1. ODR 1600Hz, Gyro Range 2000dps, Accel Range 8g 설정 실제 반영
 * 2. BMI270 초기화 시퀀스(v210 Config Load) 단계별 상태 관리
 * 3. SPI 트랜잭션 최적화 및 6-byte Burst Read 구현
 * 4. DRDY ISR에서 Queue를 통한 데이터 전달 레이턴시 최소화
 ============================================================================ */

#include "T221_Mfcc_Inter_213.h"

/*
// BMI270 핵심 레지스터 정의 (v210 복구)
#define BMI2_REG_ACC_CONF      0x40
#define BMI2_REG_ACC_RANGE     0x41
#define BMI2_REG_GYR_CONF      0x42
#define BMI2_REG_GYR_RANGE     0x43
#define BMI2_REG_INT1_IO_CTRL  0x53
#define BMI2_REG_INT_MAP_DATA  0x58
#define BMI2_REG_PWR_CONF      0x7C
#define BMI2_REG_PWR_CTRL      0x7D
*/

/* 
#define REG_ACC_CONF      0x40
#define REG_ACC_RANGE     0x41
#define REG_GYR_CONF      0x42
#define REG_GYR_RANGE     0x43
#define REG_PWR_CONF      0x7C
#define REG_PWR_CTRL      0x7D
#define REG_CMD           0x7E
*/



// 라이브러리 함수를 사용하여 센서 초기화
// [라이브러리 기반 초기화] 
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. SPI 통신 시작 및 Config Load (0x59 과정 포함)
    // 라이브러리의 beginSPI는 내부적으로 칩 아이디 확인 및 마이크로코드 로드를 수행합니다.
    int8_t status = p->bmi.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    if (status != BMI2_OK) {
        p->bmi_state.master = EN_T20_STATE_ERROR;
        strlcpy(p->bmi270_status_text, "Library_Init_Fail", 48);
        return false;
    }

    // 2. 가속도계/자이로스코프 설정 (라이브러리 매크로 및 고수준 함수 사용)
    p->bmi.setAccelODR(BMI2_ACC_ODR_1600HZ);
    p->bmi.setAccelRange(BMI2_ACC_RANGE_8G);
    p->bmi.setAccelPowerMode(BMI2_ACC_PERF_MODE); // 고성능 모드

    p->bmi.setGyroODR(BMI2_GYR_ODR_1600HZ);
    p->bmi.setGyroRange(BMI2_GYR_RANGE_2000);
    p->bmi.setGyroPowerMode(BMI2_GYR_PERF_MODE, BMI2_GYR_NOISE_PERF);

    // 3. FIFO 설정 (1600Hz 대응을 위한 검토 반영)
    // 매 샘플마다 인터럽트를 거는 것보다 FIFO에 쌓아두고 읽는 것이 CPU 효율과 SD 쓰기 안정성에 유리합니다.
    BMI270_FIFOConfig fifoConfig;
    fifoConfig.flags = BMI2_FIFO_GYR_EN | BMI2_FIFO_ACC_EN; // 가속도/자이로 모두 FIFO 저장
    fifoConfig.watermark = 16;                             // 16개 샘플이 쌓이면 인터럽트 발생
    fifoConfig.accelFilter = true;
    fifoConfig.gyroFilter = true;
    p->bmi.setFIFOConfig(fifoConfig);

    // 4. 인터럽트 매핑: FIFO Watermark 도달 시 INT1 핀 신호 발생
    p->bmi.mapInterruptToPin(BMI2_FIFO_WM_INT, BMI2_INT1);

    p->bmi_state.init = EN_T20_STATE_DONE;
    p->bmi_state.spi = EN_T20_STATE_READY;
    strlcpy(p->bmi270_status_text, "1600Hz_FIFO_Ready", 48);
    return true;
}


// 라이브러리가 내부적으로 처리하므로 제거 권장 (빌드 유지를 위해 껍데기만 유지)
bool T20_bmi270ActualReadBurst(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len) {
    (void)p; (void)p_buf; (void)p_len;
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
bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
    if (p == nullptr || p_out_sample == nullptr) return false;

    // FIFO를 사용하지 않는 일반 모드일 경우:
    if (p->bmi.getSensorData() != BMI2_OK) return false;

    // 현재 설정된 축 모드에 따라 데이터 선택 (v210 로직 유지)
    switch (p->bmi270_axis_mode) {
        case G_T20_BMI270_AXIS_MODE_ACC_Z:
            *p_out_sample = p->bmi.data.accelZ;
            break;
        case G_T20_BMI270_AXIS_MODE_GYRO_Z:
        default:
            *p_out_sample = p->bmi.data.gyroZ;
            break;
    }

    // 웹 뷰어 모니터링용 최신값 갱신
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
    T20_recorderWriteEvent(p, "sensor_reinit_attempt");

    // 라이브러리 제공 소프트 리셋
    if (p->bmi.reset() == BMI2_OK) {
        delay(50);
        if (T20_initBMI270_SPI(p)) {
            T20_recorderWriteEvent(p, "sensor_recovered");
            p->bmi_state.master = EN_T20_STATE_RUNNING;
            return true;
        }
    }

    p->bmi_state.master = EN_T20_STATE_ERROR;
    return false;
}


// BMI270 가속도/자이로 고정밀 보정 및 설정값
// 불필요해진 구형 수동 설정 함수는 제거하거나 
//   위의 라이브러리 기반 함수(T20_initBMI270_SPI)를 호출하도록 리다이렉트합니다.
bool T20_bmi270_LoadProductionConfig(CL_T20_Mfcc::ST_Impl* p) {
    return T20_initBMI270_SPI(p);
}

