// ... 상단 include 생략 ...
#include "SD10_SDMMC_008.h"

// [추가] 외부 태스크 핸들 선언 (순환 참조 에러 해결)
extern TaskHandle_t g_A10_Que_Sensor; 

// 데이터 페이로드 정의
struct ST_FullSensorPayload_t {
// ... 이하 동일 ...

    // [수정된 enterIdleMode]
    void enterIdleMode() {
        Serial.println(">>> Sleep Mode: Suspend Tasks & Light Sleep Start");
        
        // 태스크 일시 정지 (외부 핸들 참조)
        if (g_A10_Que_Sensor != NULL) vTaskSuspend(g_A10_Que_Sensor);

        if (_opts.useSD) {
            vTaskDelay(pdMS_TO_TICKS(50)); // 로깅 태스크가 flush할 시간을 줌
            _sd->end();
        }

        _imu.setAccelPowerMode(BMI2_POWER_OPT_MODE);
        _imu.enableAdvancedPowerSave();

        esp_sleep_enable_ext0_wakeup((gpio_num_t)C10_Config::BMI_INT1, 1);
        esp_light_sleep_start();

        resumeFromIdle();

        // 태스크 재개
        if (g_A10_Que_Sensor != NULL) vTaskResume(g_A10_Que_Sensor);
        Serial.println("<<< Wakeup: Tasks Resumed");
    }
// ... 이하 동일 ...




// [Task 2] SD 로깅 태스크 (성능 최적화 및 안정성 강화)
void A10_loggingTask(void* pv) {
    ST_FullSensorPayload_t data;
    int flushCounter = 0;
    
    // 파일 핸들을 루프 밖에서 한 번만 오픈
    File file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);
    
    if (!file) {
        Serial.println("!!! SD: Failed to open log file for appending");
    }

    for (;;) {
        if (xQueueReceive(g_A10_Que_SD, &data, portMAX_DELAY)) {
            if (g_A10_ImuOptions.useSD && file) {
                file.printf("%lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%lu,%d\n",
                    data.timestamp, data.acc[0], data.acc[1], data.acc[2],
                    data.gyro[0], data.gyro[1], data.gyro[2],
                    data.quat[0], data.quat[1], data.quat[2], data.quat[3],
                    data.euler[0], data.euler[1], data.euler[2],
                    data.stepCount, data.motion);

                // 20개 레코드(약 100ms)마다 실제 데이터 물리 저장
                if (++flushCounter >= 20) {
                    file.flush();
                    flushCounter = 0;
                }
            }
        }
    }
    // 태스크 종료 시(정상적으론 미발생) 파일 닫기
    if (file) file.close();
}


