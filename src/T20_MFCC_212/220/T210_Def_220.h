/* ============================================================================
 * File: T210_Def_220.h
 * Summary: T20 MFCC 시스템 통합 정의 (v220)
 * Description: 시스템 전역 상수, 데이터 구조체, 기본 설정 생성 로직 통합
 * ========================================================================== */

#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ----------------------------------------------------------------------------
 * [PART 1] 시스템 전역 상수 (기존 T210_Def_Com)
 * ------------------------------------------------------------------------- */
namespace T20 {
    namespace C10_Sys {
        inline constexpr char const* 	VERSION_STR           	= "T20_Mfcc_v220"; 		// 시스템 버전 문자열
        inline constexpr uint16_t 		QUEUE_LEN               = 8U;              		// Task 간 메시지 큐 길이
        inline constexpr uint16_t 		CFG_PROFILE_COUNT       = 4U;              		// 설정 프로필 최대 개수
        inline constexpr uint16_t 		RAW_FRAME_BUFFERS       = 4U;              		// Raw 데이터 버퍼 개수
        inline constexpr uint16_t 		SEQUENCE_FRAMES_MAX     = 16U;             		// 시퀀스 출력 시 최대 프레임 수
        inline constexpr uint8_t  		PIN_NOT_SET             = 0xFFU;           		// 미할당 핀 정의
    }

    namespace C10_Pin {
        inline constexpr uint8_t 		BTN_CONTROL 			= 0U;                   // 제어 버튼 (Boot Pin)
							
		inline constexpr uint8_t 		RGB_LED     			= 21U;                  // 상태 표시용 RGB LED
							
        inline constexpr uint8_t 		BMI_SCK     			= 12U;                  // IMU SPI SCK
        inline constexpr uint8_t 		BMI_MISO    			= 13U;                  // IMU SPI MISO
        inline constexpr uint8_t 		BMI_MOSI    			= 11U;                  // IMU SPI MOSI
        inline constexpr uint8_t 		BMI_CS      			= 10U;                  // IMU SPI CS
        inline constexpr uint8_t 		BMI_INT1    			= 14U;                  // IMU 인터럽트 핀
							
		inline constexpr uint8_t 		SDMMC_CLK   			= 39U;                  // SDMMC CLK
        inline constexpr uint8_t 		SDMMC_CMD   			= 38U;                  // SDMMC CMD
        inline constexpr uint8_t 		SDMMC_D0    			= 40U;                  // SDMMC Data 0
        inline constexpr uint8_t 		SDMMC_D1    			= C10_Sys::PIN_NOT_SET; // SDMMC Data 1 (4-bit 미사용 시)
        inline constexpr uint8_t 		SDMMC_D2    			= C10_Sys::PIN_NOT_SET; // SDMMC Data 2
        inline constexpr uint8_t 		SDMMC_D3    			= C10_Sys::PIN_NOT_SET; // SDMMC Data 3
    }

    namespace C10_Task {
        inline constexpr uint32_t 		SENSOR_STACK   			= 6144U;                // 센서 수집 태스크 스택
        inline constexpr uint32_t 		PROCESS_STACK  			= 12288U;               // DSP/MFCC 연산 태스크 스택
        inline constexpr uint32_t 		RECORDER_STACK 			= 8192U;                // 파일 기록 태스크 스택
        inline constexpr uint8_t  		SENSOR_PRIO    			= 4U;                   // 센서 태스크 우선순위 (높음)
        inline constexpr uint8_t  		PROCESS_PRIO   			= 3U;
        inline constexpr uint8_t  		RECORDER_PRIO  			= 2U;
    }

    namespace C10_DSP {
        inline constexpr uint16_t 		FFT_SIZE         		= 256U;                 // 기본 FFT 크기
        inline constexpr uint16_t 		FFT_BINS         		= (FFT_SIZE / 2U) + 1U;
        inline constexpr float    		SAMPLE_RATE_HZ   		= 1600.0f;              // 샘플링 주파수
        inline constexpr uint16_t 		MEL_FILTERS      		= 26U;                  // Mel Filter Bank 개수
        inline constexpr uint16_t 		MFCC_COEFFS_MAX  		= 32U;                  // 최대 MFCC 계수
        inline constexpr uint16_t 		MFCC_COEFFS_DEF  		= 13U;                  // 기본 MFCC 계수
        inline constexpr uint16_t 		MFCC_HISTORY_LEN 		= 5U;                   // 과거 특징량 보존 길이
        inline constexpr float    		MEL_SCALE_CONST  		= 2595.0f;              // Mel 변환 상수
        inline constexpr float    		MEL_FREQ_CONST   		= 700.0f;               // Mel 변환 주파수 상수
		
		inline constexpr uint8_t 		TRIGGER_BANDS_MAX		= 3; 					// 최대 3개의 주파수 대역 감시(T20_MAX_TRIGGER_BANDS)
    }
	
	

    namespace C10_BMI {
        inline constexpr uint32_t 		SPI_FREQ_HZ      		= 10000000UL;			// SPI 통신 속도 (10MHz)
        inline constexpr uint8_t  		REG_CALIB_OFFSET_START 	= 0x71U;				// 캘리브레이션 레지스터 시작 주소
        inline constexpr float    		LSB_PER_G        		= 2048.0f;              // 16G 설정 시 G당 LSB (범위별 가변 필요)
    }

    namespace C10_Rec {
        inline constexpr uint32_t 		BINARY_MAGIC     	= 0x54323042UL;				// 바이너리 파일 식별자 (T20B)
        inline constexpr uint16_t 		BINARY_VERSION   	= 1U;						// 바이너리 포맷 버전
        inline constexpr uint16_t 		BATCH_WMARK_HIGH 	= 8U;						// Write 워터마크
        inline constexpr uint32_t 		BATCH_IDLE_FLUSH_MS = 250U;						// 유휴 상태 시 강제 Flush 대기시간
        inline constexpr uint16_t 		ROTATE_KEEP_MAX  	= 8U;						// 파일 로테이션 유지 개수
    }

    namespace C10_Web {
        inline constexpr char const* 	WS_URI        		= "/api/t20/ws";			// WebSocket 엔드포인트
        inline constexpr uint16_t 		JSON_BUF_SIZE    	= 2048U;					// 일반 JSON 버퍼 크기
        inline constexpr uint16_t 		LARGE_JSON_BUF_SIZE = 8192U;					// 설정용 대형 JSON 버퍼 크기
        inline constexpr uint32_t 		BTN_DEBOUNCE_MS  	= 500U;						// 버튼 디바운스 시간
    }

    namespace C10_Net {
        inline constexpr uint8_t 		WIFI_MULTI_MAX    	= 3U;						// 다중 AP 등록 최대 개수
    }

    namespace C10_Time {
        inline constexpr char const* 	NTP_SERVER_1  		= "pool.ntp.org";
        inline constexpr char const* 	NTP_SERVER_2  		= "time.nist.gov";
        inline constexpr char const* 	TZ_INFO       		= "KST-9";					// 한국 표준시
        inline constexpr uint32_t 		SYNC_TIMEOUT_MS  	= 5000U;					// NTP 동기화 타임아웃
    }

    namespace C10_NVS {
        inline constexpr char const* 	NAMESPACE    		= "t20_sys";                // NVS 네임스페이스
        inline constexpr char const* 	KEY_FILE_SEQ 		= "file_seq";               // 파일 시퀀스 번호 키
    }

    namespace C10_Ext {
        inline constexpr uint32_t 		ROTATION_MB_DEF 	= 50U;                      // 기본 로테이션 용량 (MB)
        inline constexpr float    		THRES_RMS_DEF   	= 0.5f;                     // 기본 RMS 트리거 임계값
        inline constexpr uint32_t 		SLEEP_SEC_DEF   	= 300U;                     // 기본 딥슬립 진입 시간
    }

    namespace C10_Path {
        inline constexpr char const* 	MOUNT_SD      		= "/sdcard";
        inline constexpr char const* 	DIR_SYS       		= "/sys";
        inline constexpr char const* 	DIR_WEB       		= "/www";
        inline constexpr char const* 	FILE_CFG_JSON 		= "/sys/runtime_cfg_220_001.json"; 
        inline constexpr char const* 	FILE_REC_IDX  		= "/sys/recorder_index.json";
        inline constexpr char const* 	FILE_BMI_CALIB		= "/sys/bmi_calib.json";
        inline constexpr char const* 	WEB_INDEX     		= "index_220_001.html";            
        inline constexpr char const* 	SD_DIR_BIN    		= "/t20_data/bin";
        inline constexpr char const* 	SD_PREFIX_BIN 		= "/t20_data/bin/rec_";
        inline constexpr char const* 	DIR_FALLBACK  		= "/fallback";
        inline constexpr char const* 	SD_DIR_RAW    		= "/t20_data/raw";
    }
}

/* ----------------------------------------------------------------------------
 * [PART 2] 데이터 구조체 및 열거형 정의 (기존 T214_Def_Rec)
 * ------------------------------------------------------------------------- */

// --- [2.1] Sensor & Analysis Enums ---
typedef enum {
	EN_T20_AXIS_ACCEL_X = 0,
	EN_T20_AXIS_ACCEL_Y,
	EN_T20_AXIS_ACCEL_Z,
	EN_T20_AXIS_GYRO_X,
	EN_T20_AXIS_GYRO_Y,
	EN_T20_AXIS_GYRO_Z
} EM_T20_SensorAxis_t;

typedef enum {
	EN_T20_ACCEL_2G = 0,
	EN_T20_ACCEL_4G,
	EN_T20_ACCEL_8G,
	EN_T20_ACCEL_16G
} EM_T20_AccelRange_t;

typedef enum {
	EN_T20_GYRO_125 = 0,
	EN_T20_GYRO_250,
	EN_T20_GYRO_500,
	EN_T20_GYRO_1000,
	EN_T20_GYRO_2000
} EM_T20_GyroRange_t;

typedef enum {
	EN_T20_FFT_256	= 256,
	EN_T20_FFT_512	= 512,
	EN_T20_FFT_1024 = 1024,
	EN_T20_FFT_2048 = 2048,
	EN_T20_FFT_4096 = 4096
} EM_T20_FftSize_t;

typedef enum {
	EN_T20_AXIS_SINGLE = 1,
	EN_T20_AXIS_TRIPLE = 3
} EM_T20_AxisCount_t;

// --- [2.2] Preprocess & DSP Enums ---
typedef enum {
	EN_T20_FILTER_OFF = 0,
	EN_T20_FILTER_LPF,
	EN_T20_FILTER_HPF
} EM_T20_FilterType_t;

typedef enum {
	EN_T20_NOISE_OFF = 0,
	EN_T20_NOISE_FIXED,
	EN_T20_NOISE_ADAPTIVE
} EM_T20_NoiseMode_t;

typedef enum {
	EN_T20_WIFI_STA_ONLY = 0,
	EN_T20_WIFI_AP_ONLY,
	EN_T20_WIFI_AP_STA,
	EN_T20_WIFI_AUTO_FALLBACK
} EM_T20_WiFiMode_t;

typedef enum {
	EN_T20_STORAGE_LITTLEFS = 0,
	EN_T20_STORAGE_SDMMC
} EM_T20_StorageBackend_t;

// --- [2.3] Configuration Structures ---

typedef struct {
    EM_T20_SensorAxis_t axis;
    EM_T20_AccelRange_t accel_range;
    EM_T20_GyroRange_t  gyro_range;
} ST_T20_ConfigSensor_t;


typedef struct {
	bool  enable;
	float start_hz;
	float end_hz;
	float threshold;
} ST_T20_TriggerBand_t;

typedef struct {
	bool				 use_threshold;
	float				 threshold_rms;
	uint16_t			 any_motion_duration;  // Any-Motion 지속 시간 (1 = 20ms)
	bool				 use_deep_sleep;
	uint32_t			 sleep_timeout_sec;
	ST_T20_TriggerBand_t bands[T20::C10_DSP::TRIGGER_BANDS_MAX];	// 다중 주파수 밴드 감시
} ST_T20_ConfigTrigger_t;

// 특징량 벡터 구조체 (타임스탬프 및 117차원 확장)
typedef struct {
	uint64_t timestamp_ms;						  // NTP/시스템 정밀 타임스탬프 (ms)
	uint32_t frame_id;							  // 프레임 시퀀스 번호
	uint8_t	 active_axes;						  // 1 또는 3
	uint8_t	 status_flags;						  // Bit 0: NTP Synced, Bit 1: Triggered
	float	 rms[3];							  // 각 축별 실시간 RMS 값
	float	 band_energy[T20::C10_DSP::TRIGGER_BANDS_MAX];  // 감시 중인 밴드들의 최신 에너지
	float	 features[3][39];					  // 39 * 3 (Max 117차원 특징량)
} ST_T20_FeatureVector_t;

typedef struct {
    uint16_t           hop_size;
    uint16_t           mfcc_coeffs;
    EM_T20_FftSize_t   fft_size;
    EM_T20_AxisCount_t axis_count;
} ST_T20_ConfigFeature_t;

// --- [2.4] DSP Sub-Structures ---

typedef struct {
    bool  enable;
    float alpha;
} ST_T20_PreproEmphasisConfig_t;

typedef struct {
    bool                enable;
    EM_T20_FilterType_t type;
    float               cutoff_hz_1;
    float               q_factor;
} ST_T20_PreproFilterConfig_t;

typedef struct {
    bool               enable_gate;
    float              gate_threshold_abs;
    EM_T20_NoiseMode_t mode;
    float              spectral_subtract_strength;
    float              adaptive_alpha;
    uint16_t           noise_learn_frames;
} ST_T20_PreproNoiseConfig_t;

typedef struct {
    bool remove_dc;
    ST_T20_PreproEmphasisConfig_t preemphasis;
    ST_T20_PreproFilterConfig_t   filter;
    ST_T20_PreproNoiseConfig_t    noise;
} ST_T20_PreprocessConfig_t;

// --- [2.5] Network & Storage Sub-Structures ---

typedef struct {
    char 					ssid[32];
    char 					password[64];
    bool 					use_static_ip;
    char 					local_ip[16], gateway[16], subnet[16], dns1[16], dns2[16];
} ST_T20_WiFiCredential_t;

typedef struct {
    EM_T20_WiFiMode_t       mode;
    ST_T20_WiFiCredential_t multi_ap[T20::C10_Net::WIFI_MULTI_MAX];
    char                    ap_ssid[32];
    char                    ap_password[64];
    char                    ap_ip[16];
} ST_T20_ConfigWiFi_t;

typedef struct {
    bool     				enable;
    char     				broker[64];
    uint16_t 				port;
    char     				id[16];
    char     				password[16];
    char     				topic_root[64];
} ST_T20_ConfigMqtt_t;

typedef struct {
    char    				profile_name[32];
    bool    				use_1bit_mode;
    uint8_t 				clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
} ST_T20_SdmmcProfile_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;      // 축당 MFCC 계수
    uint8_t  active_axes;   // 1 또는 3
    uint8_t  reserved;
    uint32_t record_count;
    char     config_dump[1024];
} ST_T20_RecorderBinaryHeader_t;

typedef struct {
    uint32_t rotation_mb;
    uint32_t rotation_min;
    bool     save_raw;
    uint16_t rotate_keep_max;
    uint32_t idle_flush_ms;
} ST_T20_ConfigStorage_t;

typedef struct {
    bool     auto_start;
    uint8_t  button_pin;
    uint32_t watchdog_ms;
} ST_T20_ConfigSystem_t;

typedef struct {
    bool     enabled;
    bool     output_sequence;  // true: 시퀀스 텐서 모드 / false: 단일 벡터 모드
    uint16_t sequence_frames;
} ST_T20_ConfigOutput_t;

// --- [2.6] Master Configuration Root ---
typedef struct {
    ST_T20_PreprocessConfig_t preprocess;
    ST_T20_ConfigSensor_t     sensor;
    ST_T20_ConfigWiFi_t       wifi;
    ST_T20_ConfigMqtt_t       mqtt;
    ST_T20_ConfigFeature_t    feature;
    ST_T20_ConfigOutput_t     output;
    ST_T20_ConfigStorage_t    storage;
    ST_T20_ConfigTrigger_t    trigger;
    ST_T20_ConfigSystem_t     system;
} ST_T20_Config_t;

/* ----------------------------------------------------------------------------
 * [PART 3] 기본 설정 빌더 (기존 T218_Def_Main)
 * ------------------------------------------------------------------------- */

/**
 * @brief 시스템 초기 설정을 기본값으로 생성합니다.
 * @return ST_T20_Config_t 초기화된 설정 구조체
 */
static inline ST_T20_Config_t T20_makeDefaultConfig() {
    ST_T20_Config_t cfg;
    memset(&cfg, 0, sizeof(cfg));

    // [1] Preprocess (DSP) - DC 제거 및 High-Pass Filter 기본 적용
    cfg.preprocess.remove_dc                        = true;
    cfg.preprocess.preemphasis.enable               = true;
    cfg.preprocess.preemphasis.alpha                = 0.97f;
    cfg.preprocess.filter.enable                    = true;
    cfg.preprocess.filter.type                      = EN_T20_FILTER_HPF;
    cfg.preprocess.filter.cutoff_hz_1               = 15.0f;
    cfg.preprocess.filter.q_factor                  = 0.707f;

    // 노이즈 게이트 및 적응형 노이즈 제거 설정
    cfg.preprocess.noise.enable_gate                = true;
    cfg.preprocess.noise.gate_threshold_abs         = 0.002f;
    cfg.preprocess.noise.mode                       = EN_T20_NOISE_ADAPTIVE;
    cfg.preprocess.noise.spectral_subtract_strength = 1.0f;
    cfg.preprocess.noise.adaptive_alpha             = 0.05f;
    cfg.preprocess.noise.noise_learn_frames         = 8U;

    // [2] Sensor - 기본 Z축, 8G 범위
    cfg.sensor.axis                                 = EN_T20_AXIS_ACCEL_Z;
    cfg.sensor.accel_range                          = EN_T20_ACCEL_8G;
    cfg.sensor.gyro_range                           = EN_T20_GYRO_2000;

	// [3] WiFi - Auto Fallback 모드 및 기본 AP 정보
	cfg.wifi.mode									= EN_T20_WIFI_AUTO_FALLBACK;
	strlcpy(cfg.wifi.ap_ssid						, "T20_MFCC_AP", 32);
	strlcpy(cfg.wifi.ap_password					, "12345678", 64);
	strlcpy(cfg.wifi.ap_ip							, "192.168.4.1", 16);

	// MQTT 기본 비활성화
	cfg.mqtt.enable 								= false;
	strlcpy(cfg.mqtt.broker							, "broker.hivemq.com", 64);
	cfg.mqtt.port 									= 1883;
	strlcpy(cfg.mqtt.id								, "T20_DEVICE", 16);
	strlcpy(cfg.mqtt.topic_root						, "t20/sensor", 64);

	// [4] Feature & Output - 기본 1축, MFCC 13차원
    cfg.feature.hop_size          					= T20::C10_DSP::FFT_SIZE;
    cfg.feature.mfcc_coeffs       					= T20::C10_DSP::MFCC_COEFFS_DEF;
    cfg.feature.fft_size          					= EN_T20_FFT_256;
    cfg.feature.axis_count        					= EN_T20_AXIS_SINGLE;
					
    cfg.output.sequence_frames    					= T20::C10_Sys::SEQUENCE_FRAMES_MAX;
    cfg.output.enabled            					= true;
    cfg.output.output_sequence    					= false; // 단일 벡터 모드

    // [5] Storage - SDMMC 로테이션 설정
    cfg.storage.save_raw          					= false;
    cfg.storage.rotation_mb       					= 10;
    cfg.storage.rotation_min      					= 60;
    cfg.storage.rotate_keep_max   					= 8;
    cfg.storage.idle_flush_ms     					= 250;

    // [6] Trigger & Power - 스마트 트리거 밴드 초기화
    cfg.trigger.use_threshold     					= false;
    cfg.trigger.threshold_rms     					= 0.5f;
    cfg.trigger.any_motion_duration 				= 5; // (100ms: 5 * 20ms)
    cfg.trigger.use_deep_sleep     					= false;
    cfg.trigger.sleep_timeout_sec  					= 300;

    for (int i = 0; i < T20::C10_DSP::TRIGGER_BANDS_MAX; i++) {
        cfg.trigger.bands[i].enable   				= false;
        cfg.trigger.bands[i].start_hz 				= 0.0f;
        cfg.trigger.bands[i].end_hz   				= 0.0f;
        cfg.trigger.bands[i].threshold 				= 0.0f;
    }

    // [7] System - 자동 시작 및 워치독 설정
    cfg.system.auto_start         					= true;
    cfg.system.button_pin         					= T20::C10_Pin::BTN_CONTROL;
    cfg.system.watchdog_ms        					= 2000; // (2초)

    return cfg;
}

