/* ============================================================================
 * [SMEA-100 핵심 구현 원칙 및 AI 셀프 회고 바이블] 
 * 1. (실수) 거대한 버퍼를 지역 변수로 선언하여 RTOS Task Stack Overflow 패닉 유발.
 * -> (방어) FFT_SIZE 단위가 넘어가는 배열은 반드시 클래스 멤버 포인터로 선언하고 
 * init() 시점에 MALLOC_CAP_SPIRAM으로 힙에 할당한다.
 * 2. [네이밍 컨벤션 엄수]: private(_), 매개변수(p_), 로컬변수(v_)
 * 3. [동적/정적 상수 승격]: MFCC 상수, 윈도우 사이즈, 켑스트럼 타겟 주파수 등 
 * 추론에 영향을 미치는 모든 매직 넘버를 제거하고 T410/T415 중앙 설정으로 위임한다.
 * 4. [SIMD 정렬 방어]: ESP-DSP 가속기의 128-bit(16 Byte) 연산 정합성을 위해 
 * 배열 선언 시 alignas(16) 속성을 부여하고, 크기 역시 16바이트 배수(PADDED)로 강제한다.
 * ============================================================================
 * File: T440_FeatExtra_008.hpp
 * Summary: 39D MFCC, Cepstrum & Spatial Feature Extraction
 * ========================================================================== */
#pragma once

// [현행화 완료] 최신 버전의 설정 매니저 및 통합 데이터 구조체 인클루드
#include "T410_Def_009.hpp"     // 시스템 정적 상수 및 동적 기본값
#include "T420_Types_011.hpp"      // FeatureSlot 및 TelemetryPacket 통신 규격
#include "T415_ConfigMgr_009.hpp"  // 런타임 JSON 동적 설정 매니저 (Singleton)

class T440_FeatureExtractor {
private:
    // ========================================================================
    // [1] SIMD 메모리 정렬(Alignment) 패딩 상수
    // ESP-DSP의 dsp_XXX_f32 함수들은 배열의 크기가 4의 배수(Float 4개 = 16바이트)일 때 
    // 최대의 가속 성능을 발휘하며, 그렇지 않을 경우 오작동하거나 뻗을 수 있습니다.
    // 비트 연산(`+3) & ~3`)을 통해 항상 4의 배수로 올림 연산(Ceil)을 수행합니다.
    // ========================================================================
    
    // FFT 수행 후 생성되는 주파수 빈(Bins)의 개수 (예: 1024 -> 513 -> 패딩 후 516)
    static constexpr uint16_t BINS_PADDED = ((SmeaConfig::System::FFT_SIZE_CONST / 2) + 1 + 3) & ~3;
    
    // 멜 필터뱅크의 개수 (예: 26 -> 패딩 후 28)
    static constexpr uint16_t MEL_PADDED  = (SmeaConfig::System::MEL_BANDS_CONST + 3) & ~3;
    
    // 추출할 기본 MFCC 계수의 개수 (예: 13 -> 패딩 후 16)
    static constexpr uint16_t MFCC_PADDED = (SmeaConfig::System::MFCC_COEFFS_CONST + 3) & ~3;

    // ========================================================================
    // [2] 링버퍼 (Ring Buffers) - 시간 미분(Delta) 연산용
    // N=2 (총 5프레임) 단위의 변화량을 계산하기 위해 과거 프레임의 특징량을 보존합니다.
    // 접근 속도를 위해 내부 SRAM에 할당하되, alignas(16)으로 정렬을 강제합니다.
    // ========================================================================
    
    // 과거 N프레임의 Static MFCC 계수(13차원) 보존용 배열
    alignas(16) float         _mfccHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST];
    
    // 과거 N프레임의 RMS 에너지 레벨 보존용 배열
    alignas(16) float         _rmsHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST];                           
    
    // 링버퍼 삽입 위치를 추적하는 인덱스 카운터 (0 ~ 4 순환)
    uint8_t                   _historyCount;
    
    // 과거 N프레임의 1차 미분(Delta MFCC) 보존용 배열 (2차 미분인 Delta-Delta 연산 시 사용)
    alignas(16) float         _deltaHistory[SmeaConfig::FeatureLimit::DELTA_HISTORY_FRAMES_CONST][SmeaConfig::System::MFCC_COEFFS_CONST]; 

    // ========================================================================
    // [3] PSRAM 동적 할당 버퍼 (RTOS Stack Overflow 원천 차단 방어)
    // 크기가 수 KB를 넘어가는 행렬 및 FFT 버퍼들은 스택 파괴를 막기 위해
    // 포인터로만 선언하고 init() 호출 시 MALLOC_CAP_SPIRAM으로 힙에 생성합니다.
    // ========================================================================
    
    // 멜 필터뱅크 2D 가중치 행렬을 1D로 평탄화(Flatten)한 버퍼 (연산 최적화용)
    float* _melBankFlat;
    
    // 이산 코사인 변환(DCT-II) 2D 가중치 행렬을 1D로 평탄화한 버퍼
    float* _dctMatrixFlat;
    
    // 공간 위상(Spatial/Phase) 차이 분석을 위해 L채널의 FFT 복소수(Complex) 결과를 담는 버퍼
    float* _fftSpatialL;
    
    // 공간 위상(Spatial/Phase) 차이 분석을 위해 R채널의 FFT 복소수 결과를 담는 버퍼
    float* _fftSpatialR;

    // ========================================================================
    // [4] Internal SRAM 워크 버퍼 (극초고속 접근용)
    // 10ms(100Hz) 마다 반복되는 핵심 파이프라인이므로, PSRAM 병목을 피하기 위해 
    // ESP32 내부 고속 메모리에 할당하여 In-place 연산을 수행합니다.
    // ========================================================================
    
    // FFT 입출력 공용 버퍼 (실수부/허수부 교차 저장 규격이므로 FFT_SIZE * 2 크기 요구)
    alignas(16) float         _fftWorkBuf[SmeaConfig::System::FFT_SIZE_CONST * 2];
    
    // FFT 복소수 결과로부터 도출된 실수부 절대 파워(Magnitude Squared) 스펙트럼 버퍼
    alignas(16) float         _powerSpectrum[BINS_PADDED];
    
    // 스펙트럼 누출(Spectral Leakage)을 막기 위해 원본 파형에 곱할 윈도우 함수 (Hann 등) 배열
    alignas(16) float         _window[SmeaConfig::System::FFT_SIZE_CONST];
    
    // ========================================================================
    // [5] 능동 배경소음 제거 (Active Noise Learning & Subtraction)
    // ========================================================================
    
    // 학습된 배경소음의 평균 파워 스펙트럼 보존 배열
    alignas(16) float         _noiseProfile[BINS_PADDED]; 
    
    // 현재까지 학습된 누적 프레임 수
    uint16_t                  _noiseLearnedFrames = 0;
    
    // 배경소음 학습 모드 가동 상태 플래그 (true 시 차감하지 않고 프로파일 누적만 수행)
    bool                      _isNoiseLearning = false;

public:
    // ------------------------------------------------------------------------
    // [생성자 및 초기화]
    // ------------------------------------------------------------------------
    T440_FeatureExtractor();
    ~T440_FeatureExtractor();

    // 초기화: PSRAM 메모리 할당 및 윈도우 함수(Hann Window), DCT 행렬 사전 계산 수행
    // 반환값: 메모리 할당 실패 시 false
    bool init();

    // ------------------------------------------------------------------------
    // [메인 파이프라인 인터페이스]
    // ------------------------------------------------------------------------
    
    // 전처리가 완료된 신호(L/R 병합)와 원본 채널 신호를 입력받아 39차원 지표를 구조체에 추출
    // @param p_cleanSignal : 빔포밍 및 노치 필터링이 완료된 1채널 분석용 파형 (크기: FFT_SIZE)
    // @param p_rawL        : 공간(위상) 분석을 위한 L채널 원본 파형
    // @param p_rawR        : 공간(위상) 분석을 위한 R채널 원본 파형
    // @param p_len         : 샘플 배열의 실제 길이 (보통 FFT_SIZE_CONST와 동일)
    // @param p_outSlot     : 모든 계산 결과가 적재될 통신용/로깅용 최종 구조체
    void extract(const float* p_cleanSignal, const float* p_rawL, const float* p_rawR, 
                 uint32_t p_len, SmeaType::FeatureSlot& p_outSlot);
    
    // 노이즈 프로파일 학습 모드를 강제로 켜거나 끕니다. (웹 UI 버튼 / FSM 스케줄러 연동)
    void setNoiseLearning(bool p_active) { 
        _isNoiseLearning = p_active; 
        if(p_active) _noiseLearnedFrames = 0; // 켤 때 누적 프레임 카운트 강제 리셋
    }

private:
    // ------------------------------------------------------------------------
    // [내부 연산 서브-루틴 (Sub-routines)]
    // 각 함수는 p_outSlot 구조체의 특정 멤버 변수들을 담당하여 채워 넣습니다.
    // ------------------------------------------------------------------------
    
    // 1. 시간 도메인 지표 (RMS, Crest Factor, Kurtosis 등) 계산
    void _computeBasicFeatures(const float* p_signal, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    
    // 2. FFT 수행 및 절대 파워 스펙트럼(_powerSpectrum) 생성 (배경소음 감산 로직 포함)
    void _computePowerSpectrum(const float* p_signal, uint32_t p_len);
    
    // 3. 스펙트럼 무게 중심(Spectral Centroid) 주파수(Hz) 계산
    void _computeSpectralCentroid(SmeaType::FeatureSlot& p_slot); 
    
    // 4. 동적 설정된 다중 주파수 대역(Multi-band)의 부분 RMS 에너지 계산
    void _computeBandRMS(SmeaType::FeatureSlot& p_slot);
    
    // 5. 파워 스펙트럼에서 에너지가 가장 높은 상위 N개의 피크(주파수, 진폭) 추출
    void _computeNtopPeaks(SmeaType::FeatureSlot& p_slot);
    
    // 6. 멜-스케일 필터뱅크 통과 및 DCT-II를 통한 13차원 Static MFCC 추출
    void _computeMfcc39(SmeaType::FeatureSlot& p_slot);
    
    // 7. 파워 스펙트럼의 로그(Log)를 다시 역푸리에 변환(IFFT)하여 특정 대상(RPM)의 켑스트럼 진폭 추출
    void _computeCepstrum(SmeaType::FeatureSlot& p_slot);
    
    // 8. L/R 원본 파형을 각각 FFT 하여 채널 간 위상차(IPD) 및 공간적 일관성(Coherence) 분석
    void _computeSpatialFeatures(const float* p_L, const float* p_R, uint32_t p_len, SmeaType::FeatureSlot& p_slot);
    
    // 9. 링버퍼(_mfccHistory 등)를 참조하여 속도(Delta) 및 가속도(Delta-Delta) 미분 적용 후 39차원 배열 최종 완성
    void _applyTemporalDerivatives(SmeaType::FeatureSlot& p_slot);
};

