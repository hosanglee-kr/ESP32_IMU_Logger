/*
 * ------------------------------------------------------
 * [VQF Tuning Guide] - C10_Config_008.h 내 수정 시 참고
 * ------------------------------------------------------
 * 1. VQF_TAU_ACC (기본 3.0s): 가속도계 보정 속도
 * - 낮을수록(1.0s) : 기울기 변화에 빠르게 반응하지만 진동에 취약함.
 * - 높을수록(5.0s) : 동작이 부드럽고 진동에 강하지만 정적 기울기 복귀가 느림.
 * 2. FIFO_WTM (200 bytes): 
 * - 한 샘플(Acc+Gyr+Header)이 약 13byte이므로, 약 15개 데이터마다 인터럽트 발생.
 * - 200Hz 기준 약 75ms마다 CPU가 깨어나 처리함 (전력 효율 최적점).
 * ------------------------------------------------------
 */

// [Task 2] SD 로깅 태스크 (성능 최적화 버전)
void A10_loggingTask(void* pv) {
    ST_FullSensorPayload_t data;
    int flushCounter = 0;
    
    // 파일 핸들을 루프 밖에서 관리하여 성능 향상
    File file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);
    
    for (;;) {
        if (xQueueReceive(g_A10_Que_SD, &data, portMAX_DELAY)) {
            if (g_A10_ImuOptions.useSD && file) {
                file.printf("%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%lu,%d\n",
                    data.timestamp, data.acc[0], data.acc[1], data.acc[2],
                    data.gyro[0], data.gyro[1], data.gyro[2],
                    data.quat[0], data.quat[1], data.quat[2], data.quat[3],
                    data.euler[0], data.euler[1], data.euler[2],
                    data.stepCount, data.motion);

                // 20개 레코드마다 실제 SD 쓰기 수행 (지연 최소화)
                if (++flushCounter >= 20) {
                    file.flush();
                    flushCounter = 0;
                }
            }
        }
    }
}

// [SB10_BM217_008.h] 내 enterIdleMode 수정 (태스크 보호 로직)
void enterIdleMode() {
    Serial.println(">>> System: Suspend Tasks & Entering Sleep");
    
    // 1. 센서/디버그 태스크 일시 정지 (스택 및 데이터 보호)
    if (g_A10_Que_Sensor) vTaskSuspend(g_A10_Que_Sensor);
    
    if (_opts.useSD) {
        // flush가 보장되도록 잠시 대기 후 종료
        vTaskDelay(pdMS_TO_TICKS(50));
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
    if (g_A10_Que_Sensor) vTaskResume(g_A10_Que_Sensor);
    Serial.println("<<< System: Tasks Resumed");
}


