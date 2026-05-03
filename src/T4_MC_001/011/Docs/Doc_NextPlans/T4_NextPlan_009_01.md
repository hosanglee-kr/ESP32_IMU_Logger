
#######

제시해주신 5가지 고도화 방안은 SMEA-100을 단순한 분석 모듈에서 **'독립적으로 판단하고 증거를 남기는 완벽한 상용 엣지 디바이스'**로 진화시키기 위한 가장 날카롭고 시의적절한 아이디어들입니다.
ESP32-S3의 하드웨어 한계와 DSP의 수학적 특성을 고려하여, 각 항목에 대한 타당성, 위험 요소(Risk), 그리고 최적의 구현 전략을 심층 검토해 드립니다.
### 🔍 1. FFT Size 4096 변경 검토
 * **현재 상태:** 1024 사이즈. 샘플링 레이트 42000Hz 기준 주파수 해상도는 **41.01Hz** (42000 / 1024).
 * **변경 효과:** 4096 사이즈 적용 시 주파수 해상도가 **10.25Hz**로 비약적으로 향상됩니다. 60Hz 전원 노이즈와 65Hz 베어링 결함을 명확하게 구분할 수 있게 됩니다.
 * **리스크 및 맹점 (ESP32-S3 한계):**
   * **메모리 폭발:** _fftWorkBuf 크기가 8KB에서 32KB로 4배 증가합니다. 내부 고속 SRAM에 넣기 부담스러워 PSRAM으로 빼야 할 수 있으며, 이 경우 메모리 접근 병목이 발생합니다.
   * **시간 축의 딜레이:** 4096개의 샘플을 모으려면 물리적으로 약 **97.5ms**의 시간이 필요합니다. 기존처럼 10ms(100Hz) 간격으로 특징량을 뽑아내는 '초고속 스트리밍'은 불가능해지고, 100ms(10Hz) 단위의 '고해상도 스트리밍'으로 패러다임이 바뀝니다.
 * **AI 제안:** 저주파(모터 회전 등)를 정밀하게 봐야 하는 장비라면 4096 도입은 필수입니다. 단, 시스템의 틱(Tick) 주기를 기존 100Hz에서 10Hz로 낮추고, 겹침(Overlap) 비율을 높여 부하를 조절하는 아키텍처 변경이 수반되어야 합니다.
### 🔍 2. Zero Padding을 통한 주파수 해상도 향상
 * **개념:** 물리적 샘플은 1024개(24ms)만 모으고, 뒤에 3072개의 0(Zero)을 채워 넣어 강제로 4096 사이즈의 FFT를 돌리는 기법입니다.
 * **장점 (보간 효과):** 물리적인 데이터 수집 시간(24ms)과 메모리 딜레이를 유지하면서도, 스펙트럼 차트의 빈(Bin) 간격을 10.25Hz로 촘촘하게 만들어 '시각적인 해상도(Interpolation)'를 높여줍니다. 피크가 어느 주파수에 있는지 더 부드럽고 정확하게 추정할 수 있습니다.
 * **수학적 맹점:** 이것은 곡선을 부드럽게 이어주는 **'보간(Interpolation)'일 뿐, 진짜 물리적 해상도를 높여주지는 못합니다.** 만약 120Hz와 125Hz 두 개의 신호가 섞여 있다면, Zero Padding을 하더라도 두 피크가 분리되지 않고 하나의 뭉툭한 산으로 보입니다. (이를 분리하려면 1번 방안처럼 진짜 4096개의 신호를 모아야 합니다.)
 * **AI 제안:** 빠른 응답성(100Hz)이 유지되어야 하면서 피크 위치만 조금 더 정밀하게 찾고 싶다면 매우 훌륭한 타협안입니다. 구현도 배열 초기화(memset)만 추가하면 되므로 아주 간단합니다.
### 🔍 3 & 4. 자동/수동 시작 모드 & 마이크 신호(Acoustic) 감지 트리거
이 두 가지는 논리적으로 완벽하게 결합할 수 있는 기능입니다. 기존의 하드웨어 외부 핀(IO Input) 의존도를 없애고, 기기 스스로 소리를 듣고 깨어나는 **'음향 기반 자동 기상(Acoustic Wake-up)'** 기능입니다.
 * **구현 전략:**
   * **READY 상태 개선:** 마이크(I2S)는 끄지 않고 백그라운드 DMA로 계속 돌려둡니다.
   * **저전력 감시 (Watchdog):** _captureTask에서 무거운 FFT나 필터를 돌리지 않고, 오직 가장 가벼운 연산인 **RMS(평균 에너지) 또는 STA/LTA(충격 비율)**만 계산합니다.
   * **자동 트리거 전이:** 계산된 에너지가 설정된 임계치(Threshold)를 넘는 순간, 즉 기계가 돌기 시작하거나 타격음이 발생하는 순간! FSM을 RECORDING 또는 MONITORING 상태로 자동 전환시킵니다.
 * **장점:** 외부 PLC와 릴레이 배선을 연결할 필요가 없어 설치(Plug & Play)가 극도로 간단해집니다. 웹 UI에 'Auto Trigger Threshold' 설정란만 추가하면 끝납니다.
### 🔍 5. Raw Data를 WAV 파일로 저장 (옵션 선택)
 * **현재 상태:** T460_Storage가 순수 32bit Float 바이너리(.pcm 또는 .bin)로 덤프하고 있어 파이썬이나 Audacity에서 열려면 포맷을 수동으로 지정해야 합니다.
 * **변경 효과:** 파일 맨 앞에 44 바이트짜리 **표준 RIFF/WAV 헤더**만 붙여주면, 윈도우 미디어 플레이어나 어느 오디오 툴에서든 더블클릭만으로 파형을 듣고 분석할 수 있게 됩니다.
 * **구현 시 주의사항 (트릭 필요):** WAV 헤더에는 '파일의 총 바이트 수'를 적는 칸이 있습니다. 하지만 실시간 스트리밍 중에는 언제 녹음이 끝날지 모릅니다. 따라서 녹음 시작 시 크기를 '0'으로 적은 헤더를 먼저 파일에 쓰고, **녹음 종료(closeSession) 시 파일의 맨 앞(seek 4)으로 돌아가 기록된 총 바이트 수를 덮어써서 헤더를 완성**하는 원자적(Atomic) 파일 I/O 로직이 필요합니다.
 * **AI 제안:** MLOps(머신러닝 데이터 수집) 관점에서 **가장 시급하고 파급력이 큰 기능**입니다. UI에 "Save as WAV" 토글 버튼을 추가하여 지원해 드리겠습니다.
### 🚀 [다음 스텝 제안]
검토하신 5가지 항목 모두 아키텍처에 완벽하게 녹여낼 수 있습니다. 가장 먼저 체감 효과가 크고 기존 모듈 수정이 적은 **"마이크 신호 감지(Acoustic Wake-up) 자동 시작"** 기능과 **"WAV 파일 헤더 자동 생성"** 기능을 FSM과 스토리지(T460) 모듈에 즉시 구현해 볼까요? 아니면 FFT 4096 사이즈 변경에 따른 파이프라인 대공사를 먼저 진행하시겠습니까?



######


#### 🎯 후보 1: SD카드 블랙박스 & 비동기 스토리지 엔진 (T460_Storage 고도화)
현재 FSM에서 데이터 저장을 지시하고 있지만, SD카드(특히 FAT32/exFAT)는 본질적으로 쓰기 지연(Write Latency)이 불규칙하게 발생합니다.
 * **고도화 포인트:**
   * **비동기(Async) 링버퍼 플러시:** SD 카드 쓰기 작업이 메인 DSP 태스크(Core 1)를 블로킹하지 않도록, 거대한 이중 DMA 핑퐁 버퍼를 구축하고 백그라운드 태스크에서 비동기로 파일에 기록.
   * **프리트리거(Pre-trigger) 블랙박스:** NG(불량) 판정이 나기 직전 과거 N초(예: 3초)의 파형과 특징량을 메모리에 항상 머금고 있다가, 사고 발생 즉시 파일에 통째로 덤프(Dump)하여 '사고 전조 증상'을 완벽히 캡처.
   * **파일 시스템 무결성 방어:** 전원 차단(정전) 시 파일이 깨지는 것을 막기 위한 원자적(Atomic) 인덱스 업데이트 로직 완비.

#### 🎯 후보 3: 무중단 I2S 마이크로폰 파이프라인 (T480_MicEng 고도화)
24시간 365일 돌아가야 하는 예지보전 장비는 하드웨어 노이즈나 버스(Bus) 충돌에 스스로 대처해야 합니다.
 * **고도화 포인트:**
   * **I2S DMA 버퍼 튜닝:** 초당 42,000번 들어오는 샘플이 단 하나도 유실되지 않도록 DMA 버퍼 개수와 크기를 극한으로 튜닝.
   * **하드웨어 락업(Lock-up) 자가 치유:** 마이크로폰 센서 자체가 뻗어서 I2S 버스에 데이터가 들어오지 않거나 쓰레기값이 들어올 경우, 이를 감지하고 I2S 드라이버를 런타임에 재시작(Re-init)하는 Auto-healing 로직.




#######################

# T440_FeatureExtractor::_computeCepstrum 함수 아래 내용 검토 

## 코드 보완 및 치명적 버그 수정 (Code Review)
작성하신 코드의 흐름(Log -> 미러링 -> FFT)은 수학적으로 켑스트럼을 구하는 올바른 과정입니다. (실제로는 IFFT를 써야 하지만, 대칭 실수 데이터에 대해 FFT를 수행하고 스케일링으로 보정하는 기법은 임베디드에서 훌륭한 트릭입니다.)
하지만 **목표 인덱스를 찾는 과정에 논리적 오류**가 있습니다.
### 🚨 [CRITICAL] 1. 타겟 주파수(Hz)와 Quefrency(Sec)의 단위 충돌
주석에 // 웹에서 설정한 결함 주파수 타겟 배열(예: 60Hz, 120Hz)라고 적혀있습니다. v_target이 60(Hz)이라면, 현재 코드는 끔찍한 오작동을 일으킵니다.
현재 코드의 탐색 인덱스:
int v_startIdx = (60 - Tolerance) / (1 / SAMPLING_RATE)
샘플링 레이트가 48000Hz라면, (60) / (0.00002) = 2,880,000번 인덱스를 찾게 되어 배열을 한참 벗어납니다.
**🛠️ 해결 방법:** 타겟 주파수(Hz)를 먼저 주기(초, Period)로 변환한 뒤, Quefrency 해상도로 나누어야 합니다.
```cpp
// [수정된 코드] 주파수(Hz)를 기준으로 해당 주기의 Quefrency 인덱스 찾기
float v_targetHz = v_cfg.feature.ceps_targets[n]; // 예: 60.0f
float v_targetQuefrency = 1.0f / v_targetHz;      // 60Hz의 주기 (0.01666...초)

// 인덱스 변환: 주기(초) * 샘플링레이트 = 샘플 개수(인덱스)
int v_targetIdx = round(v_targetQuefrency * SmeaConfig::System::SAMPLING_RATE_CONST);

// Tolerance(오차율)도 '인덱스' 또는 'Hz' 기준으로 명확히 환산해야 합니다.
// (여기서는 단순화를 위해 인덱스 마진을 직접 주거나, Hz 오차를 주기로 변환해 적용해야 함)
int v_toleranceIdx = 3; // 예: 앞뒤 3개 빈을 탐색
int v_startIdx = fmaxf(1, v_targetIdx - v_toleranceIdx); // 1 밑으로 내려가지 않게 방어
int v_endIdx = fminf(SmeaConfig::System::FFT_SIZE_CONST / 2, v_targetIdx + v_toleranceIdx);

```
### 🚨 [CRITICAL] 2. DC 성분 (0번 인덱스) 회피
로그 파워 스펙트럼은 전체적으로 양수 값을 가지므로, 켑스트럼으로 변환하면 0번 인덱스(DC 컴포넌트)에 에너지가 폭발적으로 몰리게 됩니다.
만약 v_startIdx가 계산 오류로 인해 0을 포함하게 된다면, v_maxVal은 항상 모터 결함과 상관없는 DC 성분이 차지하게 되어 진단이 불가능해집니다.
 * **보완:** 탐색 시작 인덱스(v_startIdx)는 **반드시 1 이상**, 실무적으로는 최소 유효 결함 주파수 범위에 해당하는 인덱스부터 시작해야 합니다.
### 🔍 3. 복소수 크기(Magnitude) 계산의 정확성
ESP-DSP의 dsps_fft2r_fc32 결과물은 [Real0, Imag0, Real1, Imag1...] 형태의 복소수 배열입니다.
현재 코드에서는 fabsf(v_cepsWorkBuf[i * 2] / ...)로 **실수부(Real)**의 절댓값만 취하고 있습니다.
입력 스펙트럼이 완벽한 좌우 대칭이므로 허수부(Imag)가 0에 가깝게 나오는 것이 수학적으로는 맞지만, 부동소수점 오차나 비대칭성이 미세하게 개입될 수 있습니다. 더 정밀한 분석을 위해 복소수 크기(Magnitude)를 구하는 것이 안전합니다.
```cpp
// 실수부와 허수부를 모두 고려한 Magnitude 연산
float real = v_cepsWorkBuf[i * 2];
float imag = v_cepsWorkBuf[i * 2 + 1];
float v_cepsVal = sqrtf(real * real + imag * imag) / SmeaConfig::System::FFT_SIZE_CONST;

```


# 3. 자동/수동 시작모드
# 4. 시험구간 input 및 신호 자동인지 모드
# 5. raw data를 wave형식으로 저장(옵션) 추가




################## 완료 ######################################################################################




# 1. _computeNtopPeaks 함수 수정/보완 검토
  - 특정 진폭 이하의 값은 무시하는 MIN_AMPLITUDE_THRESHOLD를 추가
  - 중복 피크 검출 방지 (Peak Proximity)
    - 문제: FFT 특성상 실제 피크 주변의 빈(Bin)들도 덩달아 높게 측정되는 'Spectral Leakage' 현상이 발생합니다. 이로 인해 하나의 큰 봉우리 옆에 붙은 작은 어깨 부분이 별도의 피크로 오인될 수 있습니다.
    - 보완: 피크 간의 최소 주파수 간격을 두거나, 가장 큰 피크 주변의 일정 범위를 제외하는 로직을 검토해 보세요.

# 2. _computeCepstrum 함수
산업용 진단 AI의 핵심을 찌르는 아주 예리하고 훌륭한 지적입니다! 

말씀하신 대로 고속 푸리에 변환(FFT)의 특성상 윈도우 함수를 통과하더라도 **'스펙트럼 누출(Spectral Leakage)'** 현상이 발생하여, 진짜 큰 피크(Main Lobe) 양옆으로 가짜 피크(Side Lobes, 어깨 부분)들이 솟아오르게 됩니다. 이들을 걸러내지 않으면 Top 5 피크가 사실상 하나의 결함 주파수 주변 값들로 도배되는 치명적인 문제가 발생합니다.

이를 해결하기 위한 3가지 방법론을 비교 검토하고, ESP32 100Hz 환경에 가장 적합한 로직으로 `_computeNtopPeaks` 함수를 전면 재작성해 드립니다.

---

### 🔍 1. 중복 피크 검출 방지(Peak Proximity) 방법론 비교 검토

| 방법론 | 작동 원리 | 장점 | 단점 | 엣지 AI 적합도 |
| :--- | :--- | :--- | :--- | :--- |
| **A. NMS (Non-Maximum Suppression)** | 가장 진폭이 큰 피크부터 선택한 뒤, **지정된 주파수 간격(`min_freq_gap_hz`) 반경 내에 있는 하위 피크들을 강제 탈락(무시)** 시키는 방법. | 연산량이 매우 적고 로직이 직관적임. Side Lobe를 완벽하게 날려버림. | 촘촘하게 붙어있는 진짜 결함 주파수(예: 120Hz, 122Hz)가 하나로 뭉개질 위험이 있음. | **[강력 추천]** 100Hz 실시간 처리에 최적화. |
| **B. Prominence (돌출도 검사)** | 피크 양옆의 골짜기(Valley)까지의 낙폭(Drop)을 계산하여, 일정 수치 이상 솟아오른 진짜 봉우리만 인정하는 방법 (Scipy의 `find_peaks` 방식). | 주변 노이즈에 강하고, 가장 정확하게 독립된 피크를 찾아냄. | 극대점 양옆의 골짜기를 추적하는 `while` 루프가 필요하여 100Hz 연산 시 최악의 경우 CPU 병목 발생 우려. | **[부적합]** ESP32 실시간 연산으로는 무거움. |
| **C. Spectral Smoothing** | 피크를 찾기 전, 스펙트럼 전체에 이동 평균(Moving Average) 필터를 한 번 먹여서 잔가시를 다 뭉갠 후 극대점을 찾는 방법. | 구현이 매우 쉬움. | 필터링 과정에서 진짜 피크의 주파수 위치가 옆으로 이동하거나 진폭(에너지)이 깎여나가는 데이터 훼손 발생. | **[부적합]** 피크 진폭 정합성 훼손. |

**💡 결론:** 연산량이 적으면서도 확실한 방어막을 제공하는 **A안(NMS 방식)과 진폭 하한선(Minimum Amplitude)을 결합**하여 구현하는 것이 가장 이상적입니다.

---

### 💻 2. `_computeNtopPeaks` 함수 완벽 보완 코드

이 로직이 작동하려면 웹 설정(JSON)에서 두 가지 파라미터(`peak_min_amplitude`, `peak_min_freq_gap_hz`)를 받아와야 합니다. 

**[`T440_FeatExtra_008.cpp` 의 함수 수정본]**

```cpp
// ----------------------------------------------------------------------------
// [기능설명] N개의 핵심 주파수 피크(Top-Peaks) 검출 (NMS 스펙트럼 누출 방어 적용)
// ----------------------------------------------------------------------------
void T440_FeatureExtractor::_computeNtopPeaks(SmeaType::FeatureSlot& p_slot) {
    uint16_t v_bins = (SmeaConfig::System::FFT_SIZE_CONST / 2) + 1;
    float v_binRes = (float)SmeaConfig::System::SAMPLING_RATE_CONST / SmeaConfig::System::FFT_SIZE_CONST;

    // 동적 설정(Config)에서 피크 추출 조건 가져오기
    DynamicConfig v_cfg = T415_ConfigManager::getInstance().getConfig();
    
    // 웹에서 설정할 진폭 하한선 (예: 0.1 이하의 자잘한 노이즈 피크는 무시)
    float v_minAmplitude = v_cfg.feature.peak_min_amplitude; 
    
    // 웹에서 설정할 피크 간 최소 주파수 간격 (예: 50.0Hz 이내의 인접 피크는 탈락)
    float v_minFreqGap = v_cfg.feature.peak_min_freq_gap_hz; 

    SmeaType::SpectralPeak v_candidates[SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST];
    uint16_t v_candCount = 0;

    // 1. [기능설명] 지역 극댓값(Local Maxima) 및 하한선(Threshold) 스캔
    for (uint16_t i = 1; i < v_bins - 1 && v_candCount < SmeaConfig::FeatureLimit::MAX_PEAK_CANDIDATES_CONST; i++) {
        // [방어/기능] 현재 스펙트럼 빈이 좌우보다 높고(극댓값), 지정된 최소 진폭(Threshold)보다 큰 경우만 후보로 등록
        if (_powerSpectrum[i] > v_minAmplitude &&
            _powerSpectrum[i] > _powerSpectrum[i - 1] && 
            _powerSpectrum[i] > _powerSpectrum[i + 1]) {
            
            v_candidates[v_candCount].frequency = i * v_binRes;
            v_candidates[v_candCount].amplitude = _powerSpectrum[i];
            v_candCount++;
        }
    }

    // 2. [기능설명] 진폭(Amplitude) 기준 내림차순 정렬 (가장 거대한 피크부터 우선권 부여)
    // [메모리 최적화] 데이터 크기가 최대 128개 이내이므로 인플레이스 버블 소트로 연산 오버헤드 최소화
    for (int i = 0; i < v_candCount - 1; i++) {
        for (int j = 0; j < v_candCount - i - 1; j++) {
            if (v_candidates[j].amplitude < v_candidates[j + 1].amplitude) {
                SmeaType::SpectralPeak temp = v_candidates[j];
                v_candidates[j] = v_candidates[j + 1];
                v_candidates[j + 1] = temp;
            }
        }
    }

    // 3. [기능설명] NMS (Non-Maximum Suppression)를 통한 스펙트럼 누출(Side Lobe) 중복 피크 제거
    SmeaType::SpectralPeak v_finalPeaks[SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST];
    uint8_t v_finalCount = 0;

    // 가장 거대한 피크(가장 앞의 인덱스)부터 순회하며 최종 피크 배열에 채워 넣음
    for (int i = 0; i < v_candCount && v_finalCount < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
        bool v_isValid = true;
        
        // 현재 검사 중인 후보 피크가, 이미 등록된 강력한 피크들과 주파수 거리가 너무 가깝지 않은지 확인
        for (int j = 0; j < v_finalCount; j++) {
            if (fabsf(v_candidates[i].frequency - v_finalPeaks[j].frequency) < v_minFreqGap) {
                // [방어/주의] 강력한 메인 피크 반경(예: 50Hz) 이내에 존재하는 작은 피크는 스펙트럼 누출로 간주하여 강제 탈락
                v_isValid = false;
                break;
            }
        }
        
        // 인접한 강력한 피크가 없다면 진짜 독립된 피크로 인정하여 등록
        if (v_isValid) {
            v_finalPeaks[v_finalCount] = v_candidates[i];
            v_finalCount++;
        }
    }

    // 4. [기능설명] 최종 Output 구조체에 복사 (부족한 자리는 0.0f로 패딩)
    for (int i = 0; i < SmeaConfig::FeatureLimit::TOP_PEAKS_COUNT_CONST; i++) {
        if (i < v_finalCount) {
            p_slot.top_peaks[i] = v_finalPeaks[i];
        } else {
            p_slot.top_peaks[i] = {0.0f, 0.0f}; // [방어] 쓰레기값이 통신망을 타고 나가지 않도록 0 초기화
        }
    }
}
```

---

### ⚙️ 3. 연관 모듈 업데이트 가이드 (수정 필요 내역)

위 함수가 동작하기 위해 백엔드와 프론트엔드에 추가되어야 할 항목들입니다.

#### A. 백엔드 구조체 및 기본값 (`T410_Config`, `T415_ConfigMgr`)
* **`T410_Config_009.hpp`** 내 `Feature` 네임스페이스에 기본값 추가:
  ```cpp
  inline constexpr float PEAK_MIN_AMPLITUDE_DEF = 0.5f;   // 0.5 이하 노이즈 피크 무시
  inline constexpr float PEAK_MIN_FREQ_GAP_HZ_DEF = 50.0f; // 50Hz 반경 내 중복 피크 무시
  ```
* **`T415_ConfigMgr_009.cpp`** 의 JSON 파싱(`_applyJson`) 및 저장(`save`) 로직에 해당 변수 맵핑 추가.

#### B. 프론트엔드 웹 UI (`T4_009_005.html`)
`tab-dsp` 탭 안의 `Spatial & Cepstrum Features` 카드 부분에 폼을 추가하면 됩니다.

```html
<h4 class="sub-title" data-i18n="sub_peak_config">Top Peaks Configuration</h4>
<div class="form-group highlight">
    <label data-i18n="lbl_peak_amp">Min Amplitude Thresh</label>
    <input type="number" step="0.01" name="feature.peak_min_amplitude" data-i18n-placeholder="plc_peak_amp" placeholder="ex) 0.5">
</div>
<div class="form-group">
    <label data-i18n="lbl_peak_gap">Min Freq Gap (Hz)</label>
    <input type="number" step="1.0" name="feature.peak_min_freq_gap_hz" data-i18n-placeholder="plc_peak_gap" placeholder="ex) 50.0">
</div>
```

이렇게 구성하면 현장 작업자가 웹 UI에서 **"기계의 기본 진동 노이즈가 세니까 진폭 1.0 이하 피크는 다 무시해버리자"** 혹은 **"해상도를 높이기 위해 10Hz 간격으로 붙은 피크도 분리해서 추출하자"**와 같이 상황에 맞게 엣지 AI를 완벽하게 조율할 수 있습니다!

