/* ============================================================================
 * File: T310_Config_001.h
 * Version: 1.0 (Full Version)
 * [유형 1 원칙] SIMD 가속용 16바이트 패딩 규격 유지
 * [기능 누락 복원] TARGET_TEST_NO = [1, 2] 로직 및 ANC 파라미터 완전 반영
 * ========================================================================== */
#pragma once
#include <Arduino.h>

namespace Config {
    constexpr uint32_t SAMPLE_RATE = 42000;
    constexpr int FFT_SIZE = 8192; // ESP-DSP Radix-2 최적화 (파이썬 8400 패딩 대체)
    constexpr float EPSILON = 1e-6f;
    constexpr float DB_REF = 1e-6f;
    constexpr float ADC_V_TRG = 4.0f;

    // PSRAM 링버퍼 설정 (3초)
    constexpr size_t RAW_BUF_SAMP = SAMPLE_RATE * 3;
    constexpr size_t PSRAM_CACHE_SIZE = RAW_BUF_SAMP * sizeof(float);

    // 구간 샘플 수 계산
    constexpr int NOISE_SAMP_LEN  = 6300;  // 0.15s
    constexpr int VALID_SAMP_LEN  = 8400;  // 0.2s
    constexpr int VALID_OFFSET    = 12390; // 0.295s
    constexpr int TRIG_SAMP_LEN   = 8400;  // 0.2s

    // 타겟 테스트 번호 (0-based 인덱스)
    constexpr int TARGET_TEST_1 = 1;
    constexpr int TARGET_TEST_2 = 2;

    struct Pins {
        static constexpr int I2S_BCK = 14;
        static constexpr int I2S_WS  = 15;
        static constexpr int I2S_DI  = 16;
        static constexpr int ADC_TRG = 4;
    };

    // [기능 복원] ANC Spectral Gating 상세 파라미터
    struct ANC {
        static constexpr int N_FFT = 8192;     // 원본 5600을 8192로 패딩
        static constexpr int WIN_LEN = 420;
        static constexpr int HOP_LEN = 210;
        static constexpr float N_STD_THRESH = 1.5f;
        static constexpr float ALPHA = 0.97f;  // Pre-emphasis
    };

    struct MLScaler {
        static constexpr float MEAN[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; // 파이썬 학습 스케일러 값 필요
        static constexpr float SCALE[12] = {1,1,1,1,1,1,1,1,1,1,1,1};
    };

    struct Thresholds {
        static constexpr float ENERGY = 0.00003f;
        static constexpr float STDDEV = 0.12f;
        static constexpr int MIN_TRIGGER_COUNT = 1;
    };

    struct LidConfig {
        int lid; float anc_prop; float ml_cutoff;
    };
    static constexpr LidConfig LID_TABLE[] = {
        {1, 1.0f, 0.5f}, {2, 1.0f, 0.5f}, {3, 1.0f, 0.5f}, {4, 1.0f, 0.6f},
        {5, 0.8f, 0.6f}, {6, 1.0f, 0.47f}, {7, 1.0f, 0.5f}, {8, 1.0f, 0.5f}
    };
}
