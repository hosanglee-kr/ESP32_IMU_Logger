// 1. 필요한 버퍼 정의 (클래스 멤버 혹은 정적 변수)
static constexpr uint16_t FIFO_WTM_COUNT = 15; // 15개 샘플마다 처리 (C10_Config 연동 가능)
BMI270_SensorData _fifoBuffer[FIFO_WTM_COUNT];

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

// [Light Sleep 주의사항 반영]
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
