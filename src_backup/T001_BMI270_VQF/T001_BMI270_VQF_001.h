#ifndef BMI270_VQF_HANDLER_H
#define BMI270_VQF_HANDLER_H

#include <Arduino.h>
#include <SPI.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include <sensor_fusion.h> // VQF 클래스가 포함된 라이브러리
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// --- 핀 설정 (ESP32-S3 기준) ---
#define BMI270_CS_PIN   10
#define BMI270_INT_PIN  11
#define SPI_SCK_PIN     12
#define SPI_MISO_PIN    13
#define SPI_MOSI_PIN    11 // 실제 하드웨어 배선에 맞춰 수정 필요

// --- 상수 설정 ---
#define SENSOR_SAMPLE_RATE 200.0f // Hz
#define DELTA_T (1.0f / SENSOR_SAMPLE_RATE)

// 데이터 구조체
struct IMUData {
    xyz_t gyro;
    xyz_t accel;
};

// 전역 객체 및 핸들
BMI270 imu;
VQF vqfFilter(DELTA_T); // VQF 필터 인스턴스
QueueHandle_t imuEventQueue;
TaskHandle_t vqfTaskHandle;

// 인터럽트 서비스 루틴 (ISR)
void IRAM_ATTR bmi270_isr() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // 인터럽트 발생 시 최소한의 신호만 전달 (데이터는 태스크에서 읽음)
    uint8_t dummy = 1;
    xQueueSendFromISR(imuEventQueue, &dummy, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// VQF 처리 태스크
void vqfProcessingTask(void *pvParameters) {
    uint8_t sig;
    IMUData rawData;
    
    while (true) {
        // 큐 대기 (인터럽트 신호 대기)
        if (xQueueReceive(imuEventQueue, &sig, portMAX_DELAY)) {
            // BMI270 데이터 읽기
            if (imu.getSensorData()) {
                // 단위 변환: BMI270 라이브러리 출력값 확인 필요
                // VQF는 일반적으로 자이로(rad/s), 가속도(m/s^2 또는 g)를 입력으로 받음
                rawData.gyro.x = imu.data.gyroX * (PI / 180.0f); // deg/s -> rad/s
                rawData.gyro.y = imu.data.gyroY * (PI / 180.0f);
                rawData.gyro.z = imu.data.gyroZ * (PI / 180.0f);

                rawData.accel.x = imu.data.accelX; // g 단위
                rawData.accel.y = imu.data.accelY;
                rawData.accel.z = imu.data.accelZ;

                // VQF 업데이트
                vqfFilter.update(rawData.gyro, rawData.accel);

                // 결과 출력 (필요 시 별도 큐로 전송 가능)
                Quaternion q = vqfFilter.getQuaternion();
                /* Serial.printf("Roll: %.2f Pitch: %.2f Yaw: %.2f\n", 
                               q.calculate_roll_degrees(), 
                               q.calculate_pitch_degrees(), 
                               q.calculate_yaw_degrees());
                */
            }
        }
    }
}

// 초기화 함수
bool initBMI270_VQF() {
    // 1. SPI 초기화
    SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, BMI270_CS_PIN);

    // 2. BMI270 초기화 (SPI 모드)
    if (imu.beginSPI(BMI270_CS_PIN) != BMI2_OK) {
        return false;
    }

    // 3. BMI270 인터럽트 설정 (Data Ready Interrupt)
    // SparkFun 라이브러리 설정을 통해 데이터 준비 시 INT 핀이 HIGH가 되도록 설정
    imu.enableDataReadyInterrupt(); 

    // 4. FreeRTOS 구성
    imuEventQueue = xQueueCreate(10, sizeof(uint8_t));
    
    xTaskCreatePinnedToCore(
        vqfProcessingTask,   // 함수
        "VQF_Task",          // 이름
        4096,                // 스택 크기
        NULL,                // 파라미터
        2,                   // 우선순위 (높음)
        &vqfTaskHandle,      // 핸들
        1                    // 코어 번호 (Core 1 추천)
    );

    // 5. 하드웨어 인터럽트 연결
    pinMode(BMI270_INT_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(BMI270_INT_PIN), bmi270_isr, RISING);

    return true;
}

#endif
