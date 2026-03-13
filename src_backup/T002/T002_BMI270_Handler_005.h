/**
 * @file T002_BMI270_Handler_005.h
 * @brief BMI270 Full Features Integration + VQF + SDMMC 1-bit Logger
 * * [시스템 설계 개요]
 * 1. Task 분리: Core 1(센서 읽기/필터), Core 0(SDMMC 쓰기) - 쓰기 지연 차단
 * 2. 누락 방지: 대용량 FreeRTOS Queue(100 slots) 사용
 * 3. BMI270 Full: FIFO, Interrupts, Steps, Gestures, Remap, LowPower 등 통합
 * 4. SDMMC: 1-bit 모드 사용으로 SPI 대비 고속 데이터 처리
 */

#ifndef BMI270_INTEGRATED_HANDLER_H
#define BMI270_INTEGRATED_HANDLER_H

#include <Arduino.h>
#include <SPI.h>
#include <SD_MMC.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include <SensorFusion.h>
// #include <vqf.h>

// 전역 공유 데이터 구조체
struct FullSensorPayload {
    uint32_t  timestamp;
    float     acc[3];      // g
    float     gyro[3];     // rad/s
    float     rpy[3];      // Roll, Pitch, Yaw (Degrees)
    float     quat[4];     // Quaternion (w, x, y, z)
    uint32_t  stepCount;
    uint8_t   gesture;     // 1: Push, 2: Pull, 3: Flick, etc.
    bool      motion;       // Any-motion detection
};

// 제어 옵션 (Variable 설정)
struct BMI270_Options {
    bool     useVQF         = true;
    bool     useSD          = true;
    bool     useStepCounter = true;
    bool     useGestures    = true;
    bool     useAnyMotion   = true;
    float    sampleRate     = 200.0f;   // 25Hz to 1600Hz 지원
    uint16_t fifoThreshold  = 200;      // FIFO 인터럽트 발생 임계값 (바이트)
    int8_t   remapConfig    = 0;        // 0: Default, 1: Remap1, ...
};

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr) {}

    // 시스템 초기화
    bool begin(int cs, int int1, BMI270_Options opts) {
        _opts    = opts;
        _int1Pin = int1;

        // 1. BMI270 SPI 초기화
        if (_imu.beginSPI(cs) != BMI2_OK) return false;

        // 2. BMI270 모든 기능 설정 (Examples 01-14 통합)
        configureSensor();

        // 3. VQF 초기화
        if (_opts.useVQF) {
            _vqf = new VQF(1.0f / _opts.sampleRate);
        }

        // 4. Zero-Loss를 위한 RTOS 리소스 생성
        _dataQueue = xQueueCreate(100, sizeof(FullSensorPayload));
        _sdQueue   = xQueueCreate(150, sizeof(FullSensorPayload)); // SD 큐를 더 크게 설정

        // 5. 태스크 생성
        // Core 1: 센서 읽기 및 융합 (최고 우선순위)
        xTaskCreatePinnedToCore(
              sensorTask
            , "IMU_Task"
            , 8192
            , this
            , 10
            , &_sensorTaskHandle
            , 1
        );

        // Core 0: SD 저장 (입출력 지연 대응용 독립 태스크)
        if (_opts.useSD) {
            xTaskCreatePinnedToCore(
                  sdTask
                , "SD_Task"
                , 8192
                , this
                , 1
                , &_sdTaskHandle
                , 0
            );
        }

        // 6. 인터럽트 설정
        pinMode(_int1Pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(_int1Pin), isrStatic, RISING);

        return true;
    }

    bool getLatestData(FullSensorPayload* out) {
        return xQueueReceive(_dataQueue, out, 0) == pdTRUE;
    }

private:
    BMI270         _imu;
    VQF*           _vqf;
    BMI270_Options _opts;
    int            _int1Pin;

    QueueHandle_t  _dataQueue;
    QueueHandle_t  _sdQueue;

    TaskHandle_t   _sensorTaskHandle;
    TaskHandle_t   _sdTaskHandle;

    static volatile bool _fifoFlag;
    static void IRAM_ATTR isrStatic() {
        _fifoFlag = true;
    }

    // BMI270 상세 설정 (Example 01-14 통합 적용)
    void configureSensor() {
        // Ex 05: FIFO 설정 (가속도 + 자이로 + 헤더) etFIFOConfig: 구조체 사용
        // [수정 1] FIFO 설정: 구조체 방식 적용
        BMI270_FIFOConfig fifoConfig;
        fifoConfig.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        fifoConfig.watermark = _opts.fifoThreshold;
        fifoConfig.accelDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fifoConfig.gyroDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fifoConfig.accelFilter = true;
        fifoConfig.gyroFilter = true;
        _imu.setFIFOConfig(fifoConfig);



        // Ex 08: 축 매핑 (기본값)
		bmi2_remap axes;
        axes.x = BMI2_AXIS_POS_X;
        axes.y = BMI2_AXIS_POS_Y;
        axes.z = BMI2_AXIS_POS_Z;
        _imu.remapAxes(axes);



        // Ex 10, 11: 모션 감지 설정
        if (_opts.useAnyMotion) {
            _imu.enableFeature(BMI2_ANY_MOTION);
        }

        // Ex 12: 스텝 카운터
        if (_opts.useStepCounter) {
            _imu.enableFeature(BMI2_STEP_COUNTER);
        }

        // Ex 13: 손목 제스처
        if (_opts.useGestures) {
            _imu.enableFeature(BMI2_WRIST_GESTURE);
        }

        // Ex 04: 필터 및 ODR 설정
        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);
    }

    // [태스크] 센서 데이터 획득 (Core 1)
    static void sensorTask(void* pv) {
        BMI270Handler*    self = (BMI270Handler*)pv;
        FullSensorPayload data;
		uint16_t fifoLen = 0;

        for (;;) {
            if (_fifoFlag) {
                _fifoFlag = false;
				uint16_t numData = 10; // 한 번에 읽을 프레임 수

                // FIFO에서 가용한 모든 데이터를 읽음
                // [수정 3] FIFO 데이터 읽기 및 VQF 업데이트
                // getFIFOData는 numData를 포인터로 받아 실제로 읽은 수를 반환함
                while (self->_imu.getFIFOData(&self->_imu.data, &numData) == BMI2_OK && numData > 0) {
                    data.timestamp = millis();
                    data.acc[0] = self->_imu.data.accelX;
                    data.acc[1] = self->_imu.data.accelY;
                    data.acc[2] = self->_imu.data.accelZ;
                    data.gyro[0] = self->_imu.data.gyroX * 0.0174533f;
                    data.gyro[1] = self->_imu.data.gyroY * 0.0174533f;
                    data.gyro[2] = self->_imu.data.gyroZ * 0.0174533f;

                    // VQF 융합 연산 (에러 해결 핵심)
                    if (self->_opts.useVQF && self->_vqf) {
                        // 1. 센서 데이터를 라이브러리가 요구하는 xyz_t 형식으로 준비
                        xyz_t gyroRPS = { data.gyro[0], data.gyro[1], data.gyro[2] };
                        xyz_t accG = { data.acc[0], data.acc[1], data.acc[2] };

                        // 2. updateOrientation 호출 (deltaT는 1/sampleRate)
                        float dt = 1.0f / self->_opts.sampleRate;
                        Quaternion q = self->_vqf->updateOrientation(gyroRPS, accG, dt);

                        // 3. 결과값 복사 (VQF 쿼터니언 멤버: w, x, y, z)
                        data.quat[0] = q.w;
                        data.quat[1] = q.x;
                        data.quat[2] = q.y;
                        data.quat[3] = q.z;

                        self->updateEuler(data);
                    }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // [태스크] SDMMC 로깅 (Core 0)
    static void sdTask(void* pv) {
        BMI270Handler* self = (BMI270Handler*)pv;
        FullSensorPayload entry;

        // SDMMC 1-bit 초기화 (ESP32-S3 전용 핀 사용)
        if (!SD_MMC.begin("/sdcard", true)) {
            Serial.println("SDMMC Init Fail");
            vTaskDelete(NULL);
        }

        File file = SD_MMC.open("/sensor_log.csv", FILE_WRITE);
        if (!file) vTaskDelete(NULL);

        // 헤더 작성
        file.println("Time,Ax,Ay,Az,Gx,Gy,Gz,Roll,Pitch,Yaw,Steps");

        for (;;) {
            // 큐에 데이터가 쌓여있으면 SD 카드 지연과 상관없이 계속 처리
            if (xQueueReceive(self->_sdQueue, &entry, portMAX_DELAY)) {
                file.printf("%u,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u\n",
                      entry.timestamp
                    , entry.acc[0]
                    , entry.acc[1]
                    , entry.acc[2]
                    , entry.gyro[0]
                    , entry.gyro[1]
                    , entry.gyro[2]
                    , entry.rpy[0]
                    , entry.rpy[1]
                    , entry.rpy[2]
                    , entry.stepCount
                );
            }
        }
    }

    void updateEuler(FullSensorPayload& d) {
        d.rpy[0] = atan2(2.0f * (d.quat[0]*d.quat[1] + d.quat[2]*d.quat[3]), 1.0f - 2.0f*(d.quat[1]*d.quat[1] + d.quat[2]*d.quat[2])) * 57.29578f;
        d.rpy[1] = asin(2.0f * (d.quat[0]*d.quat[2] - d.quat[3]*d.quat[1])) * 57.29578f;
        d.rpy[2] = atan2(2.0f * (d.quat[0]*d.quat[3] + d.quat[1]*d.quat[2]), 1.0f - 2.0f*(d.quat[2]*d.quat[2] + d.quat[3]*d.quat[3])) * 57.29578f;
    }
};

volatile bool BMI270Handler::_fifoFlag = false;

#endif
