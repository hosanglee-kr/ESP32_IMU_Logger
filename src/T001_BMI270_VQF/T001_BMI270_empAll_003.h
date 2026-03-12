#ifndef BMI270_MANAGER_H
#define BMI270_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include <vqf.h>

/**
 * [BMI270 기능 활성화 설정 구조체]
 * 예제 01~14의 주요 기능들을 선택적으로 활성화합니다.
 */
struct BMIConfig {
    bool enableVQF = true;         // VQF 센서 융합 사용 여부
    bool enableSD = true;          // SD 카드 로깅 사용 여부
    bool useFIFO = true;           // 예제 05: FIFO 버퍼 사용 여부
    bool useStepCounter = false;    // 예제 12: 만보기 기능
    bool useWristGesture = false;   // 예제 13: 손목 제스처 감지
    bool useMotionDetect = false;   // 예제 10: 모션 감지 (Any-Motion)
    int8_t remapX = 0, remapY = 1, remapZ = 2; // 예제 08: 축 리맵핑
    float sampleRate = 200.0f;     // 샘플링 속도 (Hz)
};

// 태스크 간 데이터 전달을 위한 구조체
struct CombinedData {
    uint32_t ms;
    float acc[3], gyro[3];
    float r, p, y;
    uint32_t stepCount;
    uint8_t gesture; // 1: Push, 2: Pivot, 3: Shake 등
};

class BMI270Manager {
public:
    BMI270Manager(BMIConfig cfg) : _cfg(cfg) {}

    // 하드웨어 초기화 및 태스크 실행
    void begin(int cs, int int1, int sck, int miso, int mosi) {
        _cs = cs; _int1 = int1;
        
        // 1. SPI 및 BMI270 초기화 (예제 01, 02)
        SPI.begin(sck, miso, mosi);
        if (_imu.beginSPI(_cs) != BMI2_OK) {
            Serial.println("[-] BMI270 Init Failed!"); return;
        }

        // 2. 기능별 상세 설정
        setupFeatures();

        // 3. SDMMC 초기화 (옵션)
        if (_cfg.enableSD) {
            if (!SD_MMC.begin("/sdcard", true)) Serial.println("[-] SD Fail");
            else _logFile = SD_MMC.open("/sensor_log.csv", FILE_WRITE);
        }

        // 4. FreeRTOS 태스크 생성
        _dataQueue = xQueueCreate(30, sizeof(CombinedData));
        xTaskCreatePinnedToCore(staticSensorTask, "Sensor", 8192, this, 10, NULL, 1);
        xTaskCreatePinnedToCore(staticProcessTask, "Process", 8192, this, 5, NULL, 0);
    }

private:
    BMI270 _imu;
    BMIConfig _cfg;
    int _cs, _int1;
    QueueHandle_t _dataQueue;
    File _logFile;
    VQF* _vqf = nullptr;

    void setupFeatures() {
        // 예제 08: 축 리맵핑 (필요 시)
        // _imu.remapAxes(_cfg.remapX, _cfg.remapY, _cfg.remapZ);

        // 예제 05: FIFO 설정
        if (_cfg.useFIFO) {
            _imu.setFifoConfig(BMI2_FIFO_ACCEL_EN | BMI2_FIFO_GYRO_EN, BMI2_FIFO_HEADER_EN);
        }

        // 예제 12: 스텝 카운터 활성화
        if (_cfg.useStepCounter) _imu.enableStepCounter();

        // 예제 13: 손목 제스처 활성화
        if (_cfg.useWristGesture) _imu.enableWristGesture();

        // 예제 03: 인터럽트 설정 (데이터 준비 완료 시)
        pinMode(_int1, INPUT_PULLUP);
    }

    // 센서 읽기 루프 (Core 1)
    void sensorLoop() {
        CombinedData data;
        if (_cfg.enableVQF) _vqf = new VQF(1.0f / _cfg.sampleRate);

        for (;;) {
            // 인터럽트 체크 또는 폴링 (예제 03)
            if (digitalRead(_int1) == HIGH) {
                if (_imu.getSensorData() == BMI2_OK) {
                    data.ms = millis();
                    data.acc[0] = _imu.data.accelX;
                    data.acc[1] = _imu.data.accelY;
                    data.acc[2] = _imu.data.accelZ;
                    data.gyro[0] = _imu.data.gyroX * 0.0174533f;
                    data.gyro[1] = _imu.data.gyroY * 0.0174533f;
                    data.gyro[2] = _imu.data.gyroZ * 0.0174533f;

                    // 추가 기능 데이터 수집
                    if (_cfg.useStepCounter) data.stepCount = _imu.getStepCount();
                    if (_cfg.useWristGesture) data.gesture = _imu.getWristGesture();

                    xQueueSend(_dataQueue, &data, 0);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000/_cfg.sampleRate));
        }
    }

    // 데이터 처리 및 로깅 루프 (Core 0)
    void processLoop() {
        CombinedData d;
        while (1) {
            if (xQueueReceive(_dataQueue, &d, portMAX_DELAY)) {
                // VQF 업데이트 (예제 04 필터링 개념 확장)
                if (_cfg.enableVQF && _vqf) {
                    _vqf->update(d.gyro, d.acc);
                    float q[4];
                    _vqf->getQuaternion(q);
                    // 오일러 각 계산
                    d.r = atan2(2.0f*(q[0]*q[1]+q[2]*q[3]), 1.0f-2.0f*(q[1]*q[1]+q[2]*q[2])) * 57.295f;
                    d.p = asin(2.0f*(q[0]*q[2]-q[3]*q[1])) * 57.295f;
                    d.y = atan2(2.0f*(q[0]*q[3]+q[1]*q[2]), 1.0f-2.0f*(q[2]*q[2]+q[3]*q[3])) * 57.295f;
                }

                // 결과 출력 및 SD 저장 (예제 01, 02 확장)
                if (_logFile) {
                    _logFile.printf("%u,%.2f,%.2f,%.2f,%u\n", d.ms, d.r, d.p, d.y, d.stepCount);
                }
                printf("[IMU] R:%.1f P:%.1f Y:%.1f | Steps:%u\n", d.r, d.p, d.y, d.stepCount);
            }
        }
    }

    // FreeRTOS 정적 래퍼 함수들
    static void staticSensorTask(void* pv) { ((BMI270Manager*)pv)->sensorLoop(); }
    static void staticProcessTask(void* pv) { ((BMI270Manager*)pv)->processLoop(); }
};

#endif



///////////

/**
 * BMI270 All-in-One Manager
 * 1. BMI270Manager.h에 모든 예제 기능(FIFO, Step, Gesture 등) 포함
 * 2. 전역 변수 'config'를 통해 원하는 기능만 활성화 가능
 */
#include "BMI270Manager.h"

// 1. 센서 및 시스템 설정 (옵션 조정)
BMIConfig config = {
    .enableVQF = true,         // VQF 융합 활성
    .enableSD = true,          // SDMMC 로깅 활성
    .useFIFO = true,           // FIFO 버퍼 사용
    .useStepCounter = true,    // 스텝 카운터 사용 (예제 12)
    .useWristGesture = true,   // 손목 제스처 사용 (예제 13)
    .sampleRate = 200.0f       // 200Hz
};

// 2. 매니저 객체 생성
BMI270Manager bmiManager(config);

void setup() {
    Serial.begin(115200);

    // 3. 초기화 (CS, INT1, SCK, MISO, MOSI)
    // ESP32-S3 핀 번호에 맞춰 수정하세요.
    bmiManager.begin(10, 9, 14, 15, 12); 

    Serial.println("[+] System Ready. Tasks Started.");
}

void loop() {
    // 메인 루프는 비워두거나 시스템 상태 모니터링 용도로 사용
    vTaskDelay(pdMS_TO_TICKS(1000));
}

