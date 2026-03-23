

#include <Arduino.h>
#include <Wire.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "esp_dsp.h" // ESP-DSP 핵심 라이브러리

/*
/**
  * 소스명: T20_Mfcc_001.h
 * [기능 명세 (Specification)]
 * 1. 센서 제어: BMI270 (SparkFun 라이브러리) 활용, 1.6kHz ODR 설정.
 * 2. 필터 처리: ESP-DSP 가속 IIR Biquad 필터 (LPF, HPF, BPF 선택 가능).
 * 3. 특징 추출: 256 샘플 단위 FFT 및 13개 계수 MFCC 변환.
 * 4. 가속 엔진: ESP32-S3 Xtensa PIE(SIMD) 명령어 세트 활용 (esp-dsp 최적화).
 * * [튜닝 가이드 (Tuning Guide)]
 * 1. 샘플링 속도: BMI270_ODR_1600HZ 설정 시, Nyquist 이론에 따라 800Hz 미만 분석 가능.
 * 2. 필터 설정: 주파수 분석 목적에 따라 FILTER_TYPE (0:LPF, 1:HPF, 2:BPF) 변경.
 * 3. MFCC 윈도우: FFT_SIZE는 2의 거듭제곱(128, 256, 512 등)이어야 가속 엔진이 작동함.
 * 4. 메모리: 모든 버퍼는 __attribute__((aligned(16)))를 사용하여 SIMD 로드 효율 극대화.
 * * to-do
 * 고유 주파수를 더 세밀하게 보고 싶다면 Mel-Filterbank 레이어를 mag_sq와 DCT 사이에 추가
 */

*/
// --- 환경 설정 ---
#define FFT_SIZE 256          // FFT 샘플 수 (2의 거듭제곱 필수)
#define SAMPLING_FREQ 1600.0f // BMI270 ODR 설정값
#define MFCC_COEFFS 13        // 추출할 MFCC 계수 개수

// 필터 타입 정의
enum FilterType { LPF, HPF, BPF };
FilterType currentFilter = LPF;

// --- 메모리 정렬 (S3 SIMD 가속을 위해 16바이트 정렬 필수) ---
float __attribute__((aligned(16))) s_input[FFT_SIZE];      // 원시 데이터
float __attribute__((aligned(16))) s_window[FFT_SIZE];     // 윈도우 함수용
float __attribute__((aligned(16))) s_fft_buffer[FFT_SIZE * 2]; // FFT 복소수 버퍼
float __attribute__((aligned(16))) s_mfcc_out[MFCC_COEFFS]; // 최종 MFCC 결과

// 필터 계수 및 상태
float filter_coeffs[5]; 
float filter_state[2] = {0, 0};

BMI270 imu;

// --- 함수 선언 ---
void setupFilter(FilterType type, float f1, float f2 = 0);
void processVibrationData();
void calculateMFCC(float* input, float* output);

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 1. BMI270 초기화
    if (imu.beginI2C() != BMI2_OK) {
        Serial.println("BMI270 연결 실패!");
        while (1);
    }
    
    // 센서 설정: 1.6kHz ODR, 고성능 모드
    imu.setDriveMode(BMI2_DRV_STR_2); 
    
    // 2. ESP-DSP 초기화 (S3 전용 최적화 루틴 로드)
    esp_err_t res = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (res != ESP_OK) {
        Serial.println("DSP 초기화 에러");
    }

    // 3. 필터 설정 (예: 50Hz 미만 제거를 위한 HPF 또는 200Hz LPF)
    setupFilter(LPF, 200.0f);

    // 4. Hamming Window 미리 생성 (가속)
    dsps_hamming_f32(s_window, FFT_SIZE);

    Serial.println("시스템 시작: 진동 분석 대기 중...");
}

void loop() {
    static int sample_idx = 0;
    
    // 센서 데이터 읽기 (Z축 기준 진동 분석)
    if (imu.getSensorData() == BMI2_OK) {
        s_input[sample_idx++] = imu.data.accelX; // 필요에 따라 X,Y,Z 선택

        if (sample_idx >= FFT_SIZE) {
            processVibrationData();
            sample_idx = 0;
        }
    }
    // ODR 1.6kHz에 맞춘 샘플링 주기 확보 (실제 환경에선 인터럽트 권장)
    delayMicroseconds(625); 
}

/**
 * 필터 계수 설정 함수
 * @param type 필터 종류 (LPF, HPF, BPF)
 * @param f1 차단 주파수 (BPF일 경우 Low Cut)
 * @param f2 BPF일 경우 High Cut 주파수
 */
void setupFilter(FilterType type, float f1, float f2) {
    float norm_f1 = f1 / SAMPLING_FREQ;
    float norm_f2 = f2 / SAMPLING_FREQ;
    float q = 0.707f; // Butterworth 응답

    if (type == LPF) dsps_biquad_gen_lpf_f32(filter_coeffs, norm_f1, q);
    else if (type == HPF) dsps_biquad_gen_hpf_f32(filter_coeffs, norm_f1, q);
    else if (type == BPF) dsps_biquad_gen_bpf_f32(filter_coeffs, (norm_f1 + norm_f2)/2, q);
    
    memset(filter_state, 0, sizeof(filter_state)); // 상태 초기화
}

/**
 * 전체 프로세싱 흐름: Filter -> Window -> FFT -> MFCC
 */
void processVibrationData() {
    float filtered[FFT_SIZE] __attribute__((aligned(16)));

    // 1. IIR 필터 적용 (S3 SIMD 가속)
    dsps_biquad_f32(s_input, filtered, FFT_SIZE, filter_coeffs, filter_state);

    // 2. Windowing (노이즈 감소)
    dsps_mul_f32(filtered, s_window, filtered, FFT_SIZE);

    // 3. MFCC 변환 수행
    calculateMFCC(filtered, s_mfcc_out);

    // 결과 출력 (첫 번째 계수 위주)
    Serial.print("MFCC Coefficients: ");
    for(int i=0; i<5; i++) { // 상위 5개만 출력
        Serial.printf("%.2f ", s_mfcc_out[i]);
    }
    Serial.println();
}

/**
 * MFCC 특징 추출 로직 (S3 가속 버전)
 */
void calculateMFCC(float* input, float* output) {
    // 1. FFT 입력 버퍼 구성 (Real -> Complex 변환)
    for (int i = 0; i < FFT_SIZE; i++) {
        s_fft_buffer[i * 2] = input[i];
        s_fft_buffer[i * 2 + 1] = 0;
    }

    // 2. FFT 실행 (Radix-2 전용 가속기 사용)
    dsps_fft2r_f32(s_fft_buffer, FFT_SIZE);
    dsps_bit_rev_f32(s_fft_buffer, FFT_SIZE);

    // 3. Power Spectrum (Magnitude Squared) 계산
    // S3-SIMD는 벡터 연산을 통해 이 루프를 매우 빠르게 처리합니다.
    float mag_sq[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float re = s_fft_buffer[i * 2];
        float im = s_fft_buffer[i * 2 + 1];
        mag_sq[i] = (re * re + im * im) / FFT_SIZE;
    }

    // 4. DCT (Discrete Cosine Transform) 적용
    // 실제 MFCC는 Mel-Filterbank를 거쳐야 하지만, 
    // 진동 데이터의 경우 FFT 에너지 값에 직접 DCT를 적용하여 스펙트럼 포락선을 추출합니다.
    dsps_dct_f32(mag_sq, output, MFCC_COEFFS);
}


