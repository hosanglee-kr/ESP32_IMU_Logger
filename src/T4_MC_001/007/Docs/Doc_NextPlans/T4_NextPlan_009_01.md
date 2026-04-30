
# _computeNtopPeaks 함수 수정/보완 검토
  - 특정 진폭 이하의 값은 무시하는 MIN_AMPLITUDE_THRESHOLD를 추가
  - 중복 피크 검출 방지 (Peak Proximity)
    - 문제: FFT 특성상 실제 피크 주변의 빈(Bin)들도 덩달아 높게 측정되는 'Spectral Leakage' 현상이 발생합니다. 이로 인해 하나의 큰 봉우리 옆에 붙은 작은 어깨 부분이 별도의 피크로 오인될 수 있습니다.
    - 보완: 피크 간의 최소 주파수 간격을 두거나, 가장 큰 피크 주변의 일정 범위를 제외하는 로직을 검토해 보세요.

# _computeCepstrum 함수

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




- 자동/수동 시작모드
- 시험구간 input 및 신호 자동인지 모드
- raw data wave 저장옵션 추가
- 웹 데이터 유형별 전송