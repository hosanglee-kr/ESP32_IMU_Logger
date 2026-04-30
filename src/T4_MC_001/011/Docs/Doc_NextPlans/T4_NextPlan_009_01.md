


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

