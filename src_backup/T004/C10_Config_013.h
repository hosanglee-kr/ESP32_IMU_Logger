/*
 * ------------------------------------------------------
 * 소스명 : C10_Config_013.h
 * 모듈약어 : C10 (Config)
 * 모듈명 : Global System Configuration & RTOS Parameters
 * ------------------------------------------------------
 * 기능 요약
 * - 하드웨어 핀맵 (SD_MMC 1-bit & BMI270 SPI) 정의
 * - FreeRTOS Queue 및 Task 우선순위 최적화
 * - BMI270 FIFO 워터마크(200바이트) 및 샘플링(200Hz) 설정
 * - VQF 필터 튜닝 파라미터 관리
 * ------------------------------------------------------
 */

#pragma once
#include <Arduino.h>

namespace C10_Config {
    // [Hardware] BMI270 SPI 핀 (ESP32-S3 Default 또는 사용자 지정)
    static constexpr uint8_t 		BMI_CS    			= 10;
    static constexpr uint8_t		BMI_INT1  			= 9;

    // [Hardware] SD_MMC 1-bit 핀 정의 (S3 기본값 활용 권장)
    // 1-bit 모드: CLK=12, CMD=11, D0=13 (변경 필요 시 아래 수정)
    static constexpr uint8_t        SD_MMC_CLK          = 12;
    static constexpr uint8_t        SD_MMC_CMD          = 11;
    static constexpr uint8_t        SD_MMC_D0           = 13;

    static constexpr const char* SD_MOUNT 			= "/sdcard";
    static constexpr const char* CONFIG_PATH 		= "/config.json";

    // [Sensor] FIFO 설정
    static constexpr uint16_t 		FIFO_WTM 			= 200;    
    static constexpr float 			SAMPLE_RATE_ACTIVE 	= 200.0f; 
    static constexpr uint16_t       FIFO_WTM_COUNT      = 15;     

    // [VQF Tuning]
    static constexpr float 			VQF_TAU_ACC 		= 3.0f;  
    static constexpr float 			VQF_TAU_MAG 		= 0.0f;  

    // [RTOS]
    static constexpr uint16_t 		QUEUE_LEN_DEBUG 	= 20;  
    static constexpr uint32_t 		TASK_STACK_SIZE 	= 4096;

    // [Motion] 
    static constexpr uint16_t 		NO_MOTION_DURATION 	= 500; 
    static constexpr uint16_t 		NO_MOTION_THRESHOLD = 80; 
}

struct ST_BMI270_Options_t {
    bool useVQF 				= true;
    bool useSD 					= true;
    bool autoCalibrate 			= true;
    bool dynamicPowerSave 		= true;
    bool recordOnlySignificant 	= true;
    char logPrefix[16] 			= "T03";
};


