
/**
 * @file main.cpp
 * @brief ESP32-S3 BMI270 High-Speed Data Logger with VQF Sensor Fusion
 * * [시스템 구성]
 * 1. 센서: BMI270 (SPI 통신, INT1 인터럽트, FIFO 모드 사용)
 * 2. 필터: VQF (Versatile Quaternion Filter) - 고속/고정밀 융합
 * 3. 저장: SDMMC 1-bit 모드 (SD 카드 고속 로깅)
 * 4. OS: FreeRTOS 듀얼 코어 활용 (데이터 획득 / 계산 / 저장 태스크 분리)
 * * [주의사항]
 * - SDMMC 1-bit 사용 시 GPIO 핀 할당 확인 필요 (S3 기본핀: CMD(11), CLK(12), D0(13))
 * - BMI270 SPI 핀과 중첩되지 않도록 주의
 */
 
 /*
 ESP32-S3의 듀얼 코어 성능을 극대화하여 고속 샘플링(BMI270 FIFO 모드), VQF 센서 융합, 그리고 SDMMC(1-bit) 데이터 로깅을 동시에 수행하는 구조로 설계해 드립니다.
 
1.시스템 설계 개요
- BMI270 FIFO & Interrupt: 센서 내부 버퍼(FIFO)를 사용하여 100Hz 이상의 고속 데이터를 묶어서 읽어 CPU 부하를 줄입니다.
2.Triple Task 구조:
- Core 1 (High Priority): 센서 FIFO 읽기 및 큐 전송.
- Core 0 (Medium Priority): VQF 계산 및 결과 생성.
- Core 0 (Low Priority): SDMMC를 이용한 SD 카드 쓰기 (I/O 지연 대응).
  
3.SDMMC 1-bit: ESP32-S3의 전용 주변장치를 사용하여 SPI보다 빠른 속도로 데이터를 저장합니다.


4. 주요 상세 설명
 - VQF 샘플링 타임: VQF 생성 시 인자로 들어가는 값은 샘플링 주파수의 역수(1/f)인 초 단위 시간입니다. 200Hz라면 0.005초가 됩니다.
 - FIFO 모드: BMI270 내부 FIFO를 사용하면 센서가 데이터를 모아두었다가 한 번에 CPU에 인터럽트를 보냅니다. 이는 CPU가 매 샘플마다 깨어날 필요가 없게 하여 전력 소모와 데이터 유실 가능성을 줄여줍니다.
 - SDMMC 1-bit 로깅: SD_MMC.begin("/sdcard", true) 옵션을 통해 1-bit 모드로 동작합니다. SPI보다 버스 속도가 빨라 고속 로깅 시 병목 현상이 적습니다
 - 태스크 우선순위(Priority):
    - sensorReadTask (10): 데이터 유실 방지를 위해 가장 높음.
    - fusionProcessTask (5): 계산 처리.
    - sdWriteTask (1): SD 카드의 쓰기 속도는 가변적이므로, 큐가 쌓이더라도 다른 태스크를 방해하지 않도록 낮게 설정했습니다. 대신 sdQueue의 길이를 넉넉하게(50) 잡았습니다.

 
 */




#include <Arduino.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "VQFHandler.h"

// --- 핀 설정 (ESP32-S3 예시) ---
#define BMI_CS    10
#define BMI_INT1  9
#define BMI_SCK   14
#define BMI_MISO  15
#define BMI_MOSI  16

// --- 설정 상수 ---
#define SAMPLE_RATE 200.0f  // 200Hz 샘플링
#define FIFO_THRESHOLD 10   // FIFO에 10개 쌓일 때마다 인터럽트 발생

// 전역 객체 및 핸들
BMI270 imu;
QueueHandle_t fusionQueue;
QueueHandle_t sdQueue;
File logFile;
volatile bool fifoFull = false;

// BMI270 FIFO 인터럽트 서비스 루틴
void IRAM_ATTR onFifoInterrupt() {
    fifoFull = true;
}

/**
 * Task 1: 센서 데이터 획득 (Core 1)
 * BMI270의 FIFO 버퍼로부터 데이터를 읽어 Fusion 태스크로 전달
 */
void sensorReadTask(void *pv) {
    SensorDataPayload raw;
    while(1) {
        if (fifoFull) {
            fifoFull = false;
            // FIFO에서 가용한 샘플 수 확인 후 일괄 읽기
            // (라이브러리의 FIFO 구현 방식에 따라 루프 처리)
            while(imu.getSensorData() == BMI2_OK) {
                raw.timestamp = millis();
                raw.acc[0] = imu.data.accelX;
                raw.acc[1] = imu.data.accelY;
                raw.acc[2] = imu.data.accelZ;
                raw.gyro[0] = imu.data.gyroX * 0.0174533f; // deg to rad
                raw.gyro[1] = imu.data.gyroY * 0.0174533f;
                raw.gyro[2] = imu.data.gyroZ * 0.0174533f;

                xQueueSend(fusionQueue, &raw, 0);
                if (imu.getFifoLength() == 0) break; 
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * Task 2: VQF 계산 (Core 0)
 * 큐에서 데이터를 꺼내 VQF 필터를 적용하고 SD 저장 태스크로 전달
 */
void fusionProcessTask(void *pv) {
    VQFWrapper vqf(SAMPLE_RATE);
    SensorDataPayload data;
    while(1) {
        if (xQueueReceive(fusionQueue, &data, portMAX_DELAY)) {
            vqf.process(data); // VQF 계산 및 쿼터니언/오일러 채우기
            xQueueSend(sdQueue, &data, 0);
        }
    }
}

/**
 * Task 3: SD 카드 데이터 로깅 (Core 0, Low Priority)
 * SDMMC의 Write 지연(Latency)이 전체 시스템을 멈추지 않도록 별도 분리
 */
void sdWriteTask(void *pv) {
    SensorDataPayload data;
    char buffer[128];
    while(1) {
        if (xQueueReceive(sdQueue, &data, portMAX_DELAY)) {
            // CSV 형식으로 데이터 구성
            int len = snprintf(buffer, sizeof(buffer), 
                "%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f\n",
                data.timestamp, data.acc[0], data.acc[1], data.acc[2],
                data.roll, data.pitch, data.yaw);
            
            if (logFile) {
                logFile.write((uint8_t*)buffer, len);
                // 성능을 위해 매번 flush() 하지 않고 태스크가 한가할 때 수행 검토
            }
        }
    }
}

void setup() {
    Serial.begin(115200);

    // 1. SPI 및 BMI270 초기화
    SPI.begin(BMI_SCK, BMI_MISO, BMI_MOSI);
    if (imu.beginSPI(BMI_CS) != BMI2_OK) {
        Serial.println("BMI270 Init Failed");
        while(1);
    }
    
    // BMI270 FIFO 설정 (가속도+자이로, 인터럽트 임계값 설정)
    imu.setFifoConfig(BMI2_FIFO_ACCEL_EN | BMI2_FIFO_GYRO_EN, BMI2_FIFO_HEADER_EN);
    pinMode(BMI_INT1, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BMI_INT1), onFifoInterrupt, RISING);

    // 2. SDMMC 1-bit 초기화
    // ESP32-S3에서 1-bit 모드는 전용 핀을 사용하며 전송 속도가 SPI보다 빠름
    if (!SD_MMC.begin("/sdcard", true)) { // true = 1-bit mode
        Serial.println("SD Card Mount Failed");
    } else {
        logFile = SD_MMC.open("/data.csv", FILE_WRITE);
        if(logFile) logFile.println("Time,Ax,Ay,Az,Roll,Pitch,Yaw");
    }

    // 3. FreeRTOS 큐 및 태스크 생성
    fusionQueue = xQueueCreate(20, sizeof(SensorDataPayload));
    sdQueue = xQueueCreate(50, sizeof(SensorDataPayload));

    // Core 1: 센서 읽기 (최고 우선순위)
    xTaskCreatePinnedToCore(sensorReadTask, "Sensor", 4096, NULL, 10, NULL, 1);
    // Core 0: 필터 계산
    xTaskCreatePinnedToCore(fusionProcessTask, "Fusion", 4096, NULL, 5, NULL, 0);
    // Core 0: SD 저장 (낮은 우선순위)
    xTaskCreatePinnedToCore(sdWriteTask, "SD", 4096, NULL, 1, NULL, 0);
}

void loop() {
    // FreeRTOS 기반이므로 loop는 비워둠
    vTaskDelay(pdMS_TO_TICKS(1000));
}


/////////////////////////

#ifndef VQF_HANDLER_H
#define VQF_HANDLER_H

#include <vqf.h>

// 센서 데이터 및 융합 결과를 담는 구조체
struct SensorDataPayload {
    uint32_t timestamp;
    float acc[3];
    float gyro[3];
    float roll, pitch, yaw;
    float q[4];
};

class VQFWrapper {
public:
    VQFWrapper(float sampleRate) {
        _vqf = new VQF(1.0f / sampleRate); // VQF는 ts(sampling time)를 인자로 받음
    }

    ~VQFWrapper() { delete _vqf; }

    // 데이터를 업데이트하고 결과를 구조체에 채움
    void process(SensorDataPayload& data) {
        // Gyro: rad/s, Acc: g(또는 m/s^2)
        _vqf->update(data.gyro, data.acc);
        
        _vqf->getQuaternion(data.q);
        
        // 오일러 각 변환 (Degree)
        data.roll  = atan2(2.0f * (data.q[0] * data.q[1] + data.q[2] * data.q[3]), 1.0f - 2.0f * (data.q[1] * data.q[1] + data.q[2] * data.q[2])) * 57.29578f;
        data.pitch = asin(2.0f * (data.q[0] * data.q[2] - data.q[3] * data.q[1])) * 57.29578f;
        data.yaw   = atan2(2.0f * (data.q[0] * data.q[3] + data.q[1] * data.q[2]), 1.0f - 2.0f * (data.q[2] * data.q[2] + data.q[3] * data.q[3])) * 57.29578f;
    }

private:
    VQF* _vqf;
};

#endif


