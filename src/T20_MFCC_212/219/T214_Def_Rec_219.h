/* ============================================================================
 * File: T214_Def_Rec_219.h
 * Summary: System Configuration Types
 * ========================================================================== */
#pragma once
#include "T210_Def_Com_219.h"

// --- [1] Sensor Types ---
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


// FFT Size Enum 정의
typedef enum {
    EN_T20_FFT_256  = 256,
    EN_T20_FFT_512  = 512,
    EN_T20_FFT_1024 = 1024,
    EN_T20_FFT_2048 = 2048,
    EN_T20_FFT_4096 = 4096
} EM_T20_FftSize_t;


// 분석 축 개수 정의
typedef enum {
    EN_T20_AXIS_SINGLE = 1,
    EN_T20_AXIS_TRIPLE = 3
} EM_T20_AxisCount_t;


typedef struct {
	EM_T20_SensorAxis_t axis;
	EM_T20_AccelRange_t accel_range;
	EM_T20_GyroRange_t	gyro_range;
} ST_T20_ConfigSensor_t;

inline constexpr uint8_t  T20_MAX_TRIGGER_BANDS = 3; // 최대 3개의 주파수 대역 감시
// #define T20_MAX_TRIGGER_BANDS 3 // 최대 3개의 주파수 대역 감시

// 다중 밴드 설정 구조체 추가
typedef struct {
    bool  enable;
    float start_hz;
    float end_hz;
    float threshold;
} ST_T20_TriggerBand_t;

// 트리거 및 딥슬립 구조체
typedef struct {
    bool    use_threshold;
    float   threshold_rms;
    uint16_t any_motion_duration; // Any-Motion 지속 시간 (1 = 20ms)
    bool    use_deep_sleep;
    uint32_t sleep_timeout_sec;
    ST_T20_TriggerBand_t bands[T20_MAX_TRIGGER_BANDS]; // [추가] 다중 주파수 밴드 감시
} ST_T20_ConfigTrigger_t;


// 특징량 벡터 구조체 (타임스탬프 및 117차원 확장)
typedef struct {
    uint64_t timestamp_ms;  // NTP/시스템 정밀 타임스탬프 (ms)
    uint32_t frame_id;      // 프레임 시퀀스 번호
    uint8_t  active_axes;   // 1 또는 3
    uint8_t  status_flags;  // Bit 0: NTP Synced, Bit 1: Triggered
    float    rms[3];        // 각 축별 실시간 RMS 값
    float    band_energy[T20_MAX_TRIGGER_BANDS]; // [수정] 감시 중인 밴드들의 최신 에너지 (대표 축 0번 기준
    float    band_energy[3];  // [추가] 축별 특정 주파수 대역 에너지
    float    features[3][39]; // [117]; // 39 * 3 (Max 117차원)
} ST_T20_FeatureVector_t;


// 특징량 설정 구조체
typedef struct {
    uint16_t          hop_size;
    uint16_t          mfcc_coeffs;
    EM_T20_FftSize_t  fft_size;     // [추가] Enum 기반 설정
    EM_T20_AxisCount_t axis_count;  // [추가] 1축/3축 선택
} ST_T20_ConfigFeature_t;

// --- [2] DSP Preprocess Types ---
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


typedef struct {
	bool  enable;
	float alpha;
} ST_T20_PreproEmphasisConfig_t;

typedef struct {
	bool				enable;
	EM_T20_FilterType_t type;
	float				cutoff_hz_1;
	float				q_factor;
} ST_T20_PreproFilterConfig_t;

typedef struct {
	bool			   enable_gate;
	float			   gate_threshold_abs;
	EM_T20_NoiseMode_t mode;
	float			   spectral_subtract_strength;
	float			   adaptive_alpha;
	uint16_t		   noise_learn_frames;
} ST_T20_PreproNoiseConfig_t;

typedef struct {
	bool remove_dc;
	ST_T20_PreproEmphasisConfig_t preemphasis;
	ST_T20_PreproFilterConfig_t   filter;
	ST_T20_PreproNoiseConfig_t    noise;
} ST_T20_PreprocessConfig_t;

// --- [3] Network & Recorder Types ---
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

typedef struct {
	char ssid[32];
	char password[64];
	bool use_static_ip;												 // 유기별 고정 IP 여부
	char local_ip[16], gateway[16], subnet[16], dns1[16], dns2[16];
} ST_T20_WiFiCredential_t;

typedef struct {
	EM_T20_WiFiMode_t		mode;
	ST_T20_WiFiCredential_t multi_ap[T20::C10_Net::WIFI_MULTI_MAX];
	char					ap_ssid[32];
	char					ap_password[64];
	char					ap_ip[16];				// AP 모드 커스텀 IP
} ST_T20_ConfigWiFi_t;

// MQTT 설정을 위한 구조체 미리 선언 (Phase 2용)
typedef struct {
    bool     enable;
    char     broker[64];
    uint16_t port;
    char     id[16];
    char     password[16];
    char     topic_root[64];
} ST_T20_ConfigMqtt_t;


typedef struct {
	char	profile_name[32];
	bool	use_1bit_mode;
	uint8_t clk_pin, cmd_pin, d0_pin, d1_pin, d2_pin, d3_pin;
} ST_T20_SdmmcProfile_t;

// --- [3] Network & Recorder Types ---


// T214_Def_Rec_219.h 내 수정 제안
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t sample_rate_hz;
    uint16_t fft_size;
    uint16_t mfcc_dim;      // 축당 MFCC 계수 (예: 13)
    uint8_t  active_axes;   // [추가] 1 또는 3 (퓨전 여부 판별)
    uint8_t  reserved;      // 정렬용 패딩
    uint32_t record_count;
    char     config_dump[1024]; 
} ST_T20_RecorderBinaryHeader_t;



// --- 스토리지 및 로테이션 구조체 ---
typedef struct {
	uint32_t rotation_mb;
	uint32_t rotation_min;	// 시간 기반 로테이션 (분)
	bool	 save_raw;
	uint16_t rotate_keep_max; // 최대 보존 파일 수 (0 = 무제한)
	uint32_t idle_flush_ms;   // Idle Flush 대기 시간 (ms)
} ST_T20_ConfigStorage_t;


typedef struct {
		bool	auto_start;
		uint8_t button_pin;
		uint32_t watchdog_ms; // 워치독 타임아웃 (ms)
} ST_T20_ConfigSystem_t;


typedef struct {
		bool	 enabled;
		bool	 output_sequence;  // true: 시퀀스 텐서 모드 / false: 단일 벡터 모드
		uint16_t sequence_frames;
} ST_T20_ConfigOutput_t;


// --- [4] Master Config ---
typedef struct {
	ST_T20_PreprocessConfig_t preprocess;
	ST_T20_ConfigSensor_t	  sensor;
	ST_T20_ConfigWiFi_t		  wifi;
	ST_T20_ConfigMqtt_t       mqtt;  
	ST_T20_ConfigFeature_t    feature;
	ST_T20_ConfigOutput_t     output;
	ST_T20_ConfigStorage_t    storage;
	ST_T20_ConfigTrigger_t    trigger;
	ST_T20_ConfigSystem_t     system;
} ST_T20_Config_t;

