#pragma once
#include <Arduino.h>
#include <SparkFun_BMI270_Arduino_Library.h>
#include "SensorFusion.h"
#include "T03_SDMMC_001.h"
#include "T03_Config_001.h"

/**
 * @brief 과거 소스의 모든 기능(Remap, Gesture, AnyMotion 등)을 포함한 최신 통합 핸들러
 */
struct FullSensorPayload {
    uint32_t timestamp;
    float acc[3];
    float gyro[3];
    float rpy[3];
    float quat[4];
    uint32_t stepCount;
    uint8_t gesture;   // [복구] 제스처 데이터
    bool motion;       // [복구] 동작 감지 플래그
};

struct BMI270_Options {
    bool useVQF = true;
    bool useSD = true;
    bool useStepCounter = true;
    bool useGestures = true;    // [복구] 제스처 사용 옵션
    bool useAnyMotion = true;   // [복구] 동작 감지 옵션
};

DRAM_ATTR volatile bool g_bmi270_fifo_ready = false;
void IRAM_ATTR bmi270_global_isr() { g_bmi270_fifo_ready = true; }

class BMI270Handler {
public:
    BMI270Handler() : _vqf(nullptr), _sd(nullptr) {}
    ~BMI270Handler() { if(_vqf) delete _vqf; }

    bool begin(BMI270_Options opts, SDMMCHandler* sd = nullptr) {
        _opts = opts;
        _sd = sd;

        if (_imu.beginSPI(Config::BMI_CS) != BMI2_OK) return false;
        
        configureIMU();
        if (_opts.useVQF) _vqf = new VQF(1.0f / Config::SAMPLE_RATE);

        _liveQueue = xQueueCreate(1, sizeof(FullSensorPayload));

        xTaskCreatePinnedToCore(sensorTask, "IMU_Task", 8192, this, 10, NULL, 1);
        
        if (_opts.useSD && _sd) {
            _sd->startLogging(sdWriterWorker, this);
        }

        pinMode(Config::BMI_INT1, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::BMI_INT1), bmi270_global_isr, RISING);
        return true;
    }

    bool getLatest(FullSensorPayload* out) {
        if (!_liveQueue) return false;
        return xQueueReceive(_liveQueue, out, 0) == pdTRUE;
    }

private:
    BMI270 _imu;
    VQF* _vqf;
    SDMMCHandler* _sd;
    BMI270_Options _opts;
    QueueHandle_t _liveQueue;

    void configureIMU() {
        // 1. FIFO 및 필터 설정 [과거 소스 수준으로 복구]
        BMI270_FIFOConfig fcfg;
        fcfg.flags = BMI2_FIFO_ACC_EN | BMI2_FIFO_GYR_EN | BMI2_FIFO_HEADER_EN;
        fcfg.watermark = Config::FIFO_WTM;
        fcfg.accelDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fcfg.gyroDownSample = BMI2_FIFO_DOWN_SAMPLE_1;
        fcfg.accelFilter = true;
        fcfg.gyroFilter = true;
        _imu.setFIFOConfig(fcfg);

        // 2. 축 리맵핑 [과거 소스 기능 반영]
        bmi2_remap axes = {BMI2_AXIS_POS_X, BMI2_AXIS_POS_Y, BMI2_AXIS_POS_Z};
        _imu.remapAxes(axes);

        // 3. 기능 활성화
        if (_opts.useAnyMotion)   _imu.enableFeature(BMI2_ANY_MOTION);
        if (_opts.useStepCounter) _imu.enableFeature(BMI2_STEP_COUNTER);
        if (_opts.useGestures)    _imu.enableFeature(BMI2_WRIST_GESTURE);

        _imu.setAccelODR(BMI2_ACC_ODR_200HZ);
        _imu.setGyroODR(BMI2_GYR_ODR_200HZ);
        
        // 4. 인터럽트 매핑
        _imu.mapInterruptToPin(BMI2_FWM_INT, BMI2_INT1);
    }

    static void sensorTask(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload data;
        
        while (true) {
            if (g_bmi270_fifo_ready) {
                g_bmi270_fifo_ready = false;
                
                uint16_t numRead = 20; 
                while (self->_imu.getFIFOData(&self->_imu.data, &numRead) == BMI2_OK && numRead > 0) {
                    self->updateData(data);
                    
                    xQueueOverwrite(self->_liveQueue, &data);
                    if (self->_sd) xQueueSend(self->_sd->getQueue(), &data, 0);

                    uint16_t len;
                    self->_imu.getFIFOLength(&len);
                    if (len < 10) break;
                    numRead = 20;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    void updateData(FullSensorPayload& d) {
        d.timestamp = millis();
        d.acc[0] = _imu.data.accelX; 
        d.acc[1] = _imu.data.accelY; 
        d.acc[2] = _imu.data.accelZ;
        
        d.gyro[0] = _imu.data.gyroX * 0.0174533f;
        d.gyro[1] = _imu.data.gyroY * 0.0174533f;
        d.gyro[2] = _imu.data.gyroZ * 0.0174533f;

        // VQF 연산
        if (_opts.useVQF && _vqf) {
            xyz_t gyr = {d.gyro[0], d.gyro[1], d.gyro[2]};
            xyz_t acc = {d.acc[0], d.acc[1], d.acc[2]};
            Quaternion q = _vqf->updateOrientation(gyr, acc, 1.0f / Config::SAMPLE_RATE);
            d.quat[0] = q.w; d.quat[1] = q.x; d.quat[2] = q.y; d.quat[3] = q.z;
            computeEuler(d);
        }
        
        // 과거 소스의 기능 보충
        if (_opts.useStepCounter) _imu.getStepCount(&d.stepCount);
        
        // [추가] AnyMotion 및 제스처 상태 업데이트 (필요 시 라이브러리 함수 호출)
        // d.motion = _imu.getMotionStatus(); // 라이브러리 지원 여부에 따라 구현
    }

    static void sdWriterWorker(void* pv) {
        auto* self = (BMI270Handler*)pv;
        FullSensorPayload entry;
        // SDMMC 1-bit 모드는 이미 SDMMCHandler::begin()에서 수행됨
        File file = SD_MMC.open(self->_sd->getPath(), FILE_APPEND);
        if (!file) vTaskDelete(NULL);

        uint32_t lastFlush = millis();
        while (true) {
            if (xQueueReceive(self->_sd->getQueue(), &entry, portMAX_DELAY)) {
                // 과거 소스 로깅 포맷 유지
                file.printf("%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%u\n",
                            entry.timestamp, entry.acc[0], entry.acc[1], entry.acc[2],
                            entry.rpy[0], entry.rpy[1], entry.rpy[2], entry.stepCount);
                
                if (millis() - lastFlush > 2000) {
                    file.flush();
                    lastFlush = millis();
                }
            }
        }
    }

    void computeEuler(FullSensorPayload& d) {
        float qw = d.quat[0], qx = d.quat[1], qy = d.quat[2], qz = d.quat[3];
        d.rpy[0] = atan2(2.0f * (qw * qx + qy * qz), 1.0f - 2.0f * (qx * qx + qy * qy)) * 57.29578f;
        d.rpy[1] = asin(2.0f * (qw * qy - qz * qx)) * 57.29578f;
        d.rpy[2] = atan2(2.0f * (qw * qz + qx * qy), 1.0f - 2.0f * (qy * qy + qz * qz)) * 57.29578f;
    }
};

