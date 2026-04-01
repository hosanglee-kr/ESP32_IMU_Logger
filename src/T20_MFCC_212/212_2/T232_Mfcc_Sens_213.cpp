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

// BMI270 핵심 레지스터 정의 (v210 복구)
#define BMI2_REG_ACC_CONF      0x40
#define BMI2_REG_ACC_RANGE     0x41
#define BMI2_REG_GYR_CONF      0x42
#define BMI2_REG_GYR_RANGE     0x43
#define BMI2_REG_INT1_IO_CTRL  0x53
#define BMI2_REG_INT_MAP_DATA  0x58
#define BMI2_REG_PWR_CONF      0x7C
#define BMI2_REG_PWR_CTRL      0x7D

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
bool T20_initBMI270_SPI(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. SPI 통신 시작 (CS=10, 4MHz, SPI_MODE0)
    // 라이브러리 내부에서 Bosch API 초기화 및 Config 로드(0x59)를 수행합니다.
    int8_t status = p->bmi.beginSPI(G_T20_PIN_BMI_CS, G_T20_SPI_FREQ_HZ, p->spi);
    if (status != BMI2_OK) {
        p->bmi_state.master = EN_T20_STATE_ERROR;
        return false;
    }

    // 2. ODR 및 Range 설정 (라이브러리 전용 매크로 사용)
    p->bmi.setAccelODR(BMI2_ACC_ODR_1600HZ);
    p->bmi.setAccelPowerMode(BMI2_ACC_PERF_MODE); // 고성능 모드
    
    p->bmi.setGyroODR(BMI2_GYR_ODR_1600HZ);
    p->bmi.setGyroPowerMode(BMI2_GYR_PERF_MODE, BMI2_GYR_NOISE_PERF);

    // 3. 인터럽트 설정 (라이브러리 mapInterruptToPin 사용)
    // 데이터 준비 완료(DRDY) 신호를 INT1 핀으로 출력
    p->bmi.mapInterruptToPin(BMI2_ACC_DRDY_INT, BMI2_INT1);
    p->bmi.mapInterruptToPin(BMI2_GYR_DRDY_INT, BMI2_INT1);

    p->bmi_state.init = EN_T20_STATE_DONE;
    p->bmi_state.spi = EN_T20_STATE_READY;
    return true;
}

bool T20_bmi270ActualReadBurst(CL_T20_Mfcc::ST_Impl* p, uint8_t* p_buf, uint16_t p_len) {
    if (p == nullptr || p_buf == nullptr || p_len < 6) return false;

    // 6바이트 Burst Read (Accel 또는 Gyro 축 데이터 고속 수집)
    uint8_t start_reg = (p->bmi270_axis_mode == G_T20_BMI270_AXIS_MODE_ACC_Z) ? G_T20_BMI270_REG_ACC_X_LSB : G_T20_BMI270_REG_GYR_X_LSB;

    digitalWrite(G_T20_PIN_BMI_CS, LOW);
    p->spi.transfer(start_reg | G_T20_BMI270_REG_READ_FLAG);
    p->spi.transfer(0x00); // Dummy byte for BMI270
    for(int i=0; i<6; i++) {
        p_buf[i] = p->spi.transfer(0x00);
    }
    digitalWrite(G_T20_PIN_BMI_CS, HIGH);

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
bool T20_bmi270ReadVectorSample(CL_T20_Mfcc::ST_Impl* p, float* p_out_sample) {
    if (p == nullptr || p_out_sample == nullptr) return false;

    // 라이브러리 내부에서 SPI 버스트 읽기 및 float 변환 수행
    if (p->bmi.getSensorData() != BMI2_OK) return false;

    // 현재 설정된 축 모드에 따라 데이터 선택
    switch (p->bmi270_axis_mode) {
        case G_T20_BMI270_AXIS_MODE_ACC_Z:
            *p_out_sample = p->bmi.data.accelZ;
            break;
        case G_T20_BMI270_AXIS_MODE_GYRO_Z:
        default:
            *p_out_sample = p->bmi.data.gyroZ;
            break;
    }

    // 뷰어용 실시간 값 업데이트
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
bool T20_tryBMI270Reinit(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    p->bmi_state.master = EN_T20_STATE_BUSY;
    T20_recorderWriteEvent(p, "sensor_reinit_attempt");

    // SPI 버스 리셋 시도
    p->spi.end();
    delay(10);
    p->spi.begin(G_T20_PIN_SPI_SCK, G_T20_PIN_SPI_MISO, G_T20_PIN_SPI_MOSI, G_T20_PIN_BMI_CS);

    // BMI270 소프트 리셋 명령
    p->spi.writeRegister(0x7E, 0xB6); 
    delay(50); // 리셋 대기

    // 다시 초기화 시퀀스 호출
    if (T20_initBMI270_SPI(p)) {
        T20_recorderWriteEvent(p, "sensor_recovered");
        p->bmi_state.master = EN_T20_STATE_RUNNING;
        return true;
    }

    p->bmi_state.master = EN_T20_STATE_ERROR;
    return false;
}


// BMI270 가속도/자이로 고정밀 보정 및 설정값
bool T20_bmi270_LoadProductionConfig(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    // 1. 센서 초기화 모드 진입 (Preparation)
    p->spi.writeRegister(0x7C, 0x00); // Power Save Off
    delay(1);
    p->spi.writeRegister(0x59, 0x00); // Init Control 시작
    
    // 2. 가속도계 설정: 1600Hz ODR, 800Hz Filter BW, Normal Mode
    // 0xAC = 1010 1100 (BWP=2, ODR=1600Hz)
    p->spi.writeRegister(BMI2_REG_ACC_CONF, 0xAC); 
    p->spi.writeRegister(BMI2_REG_ACC_RANGE, 0x02); // +/- 8g (Range=2)

    // 3. 자이로스코프 설정: 1600Hz ODR, 800Hz Filter BW, Normal Mode
    // 자이로 역시 1600Hz로 동기화하여 분석 정밀도 확보
    p->spi.writeRegister(BMI2_REG_GYR_CONF, 0xAC);
    p->spi.writeRegister(BMI2_REG_GYR_RANGE, 0x00); // +/- 2000 dps (Range=0)

    // 4. 인터럽트 및 핀 설정 (Active High, Push-pull)
    p->spi.writeRegister(BMI2_REG_INT1_IO_CTRL, 0x0A); 
    p->spi.writeRegister(BMI2_REG_INT_MAP_DATA, 0x01); // DRDY 신호만 INT1으로 출력

    // 5. 최종 웨이크업
    p->spi.writeRegister(0x7D, 0x0E); // Acc, Gyr, Temp 전원 투입
    delay(20); // 안정화 대기

    strlcpy(p->bmi270_status_text, "1600Hz_Sync_Ready", sizeof(p->bmi270_status_text));
    return true;
}

