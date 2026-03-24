# 🔊 MFCC 및 피쳐 벡터(Feature Vector) 상세 정리

MFCC는 오디오 신호에서 인간의 청각 특성을 반영하여 핵심적인 '소리의 지문'을 추출하는 기법입니다.

---

## 1. 데이터의 형태: (Frames × Coefficients)
MFCC 변환 결과는 2차원 행렬(Matrix) 구조를 가집니다.

* **행(Row) - Frames:** 시간의 흐름을 나타냅니다. 
    * 소리 신호를 약 20~40ms의 짧은 단위로 쪼개어 분석한 결과물입니다.
    * 프레임 간의 연속성을 위해 보통 50% 정도 겹치게(Overlap) 설정합니다.
* **열(Column) - Coefficients:** 각 시간대별 소리의 특징값입니다.
    * 보통 13개에서 20개의 계수를 사용하며, 숫자가 작을수록 저차원의 완만한 특징을 의미합니다.

---

## 2. 포먼트(Formant)와 스펙트럼 포락선(Envelope)
MFCC는 소리의 미세한 떨림을 제거하고 **전체적인 윤곽**에 집중합니다.

* **포먼트(Formant):** 성도(Vocal Tract)나 기계 구조물에 의해 특정 주파수 대역의 에너지가 증폭된 '봉우리'입니다. 
* **스펙트럼 포락선:** MFCC는 미세한 주파수 성분(Fine Structure)을 무시하고, 이 봉우리들을 매끄럽게 연결한 **외곽선(Envelope)** 정보를 추출합니다.
* **효과:** 배경 노이즈에 강인하며, 소리의 '질감'과 '종류'를 파악하는 데 최적화되어 있습니다.



---

## 3. 계수(Coefficients)의 구성과 의미
추출된 MFCC 계수들은 그 순서에 따라 담고 있는 정보가 다릅니다.

1.  **0번 계수 ($c_0$):** 해당 프레임의 **로그 에너지(Log Energy)**. 소리의 총 크기를 나타냅니다.
2.  **하위 계수 ($c_1 \sim c_{12}$):** 스펙트럼의 거시적인 형태. 소리의 **음색(Timbre)**을 결정하는 가장 핵심적인 구간입니다.
3.  **상위 계수 ($c_{13} \dots$):** 스펙트럼의 미세한 변화. 정보량이 적고 노이즈를 포함할 확률이 높아 분석 목적에 따라 제외하기도 합니다.

---

## 4. 동적 특성: Delta($\Delta$)와 Delta-Delta($\Delta^2$)
정적인 MFCC 값만으로는 소리의 '시간적 변화'를 설명하기 부족하여 미분 개념을 도입합니다.

* **Delta ($\Delta$):** MFCC의 1차 미분값 (속도). 
    * 직전/직후 프레임 간의 변화를 계산하여 소리의 **방향성**을 파악합니다.
* **Delta-Delta ($\Delta^2$):** Delta의 변화량 (가속도). 
    * 소리의 변화가 얼마나 **급격하게(가속도)** 일어나는지 나타냅니다. 

---

## 5. 최종 피쳐 벡터(Feature Vector) 구성
AI 모델이 학습하는 최종 데이터는 이 모든 성분을 결합(Concatenate)한 형태입니다.

> **표준 39차원 피쳐 벡터 구성:**
> * **MFCC (13개):** 현재 소리의 형태 (Static)
> * **Delta (13개):** 소리의 변화량 (Velocity)
> * **Delta-Delta (13개):** 소리 변화의 변화량 (Acceleration)
> * **Total:** $13 + 13 + 13 = 39$차원의 벡터가 매 프레임마다 생성됩니다.



---

## 6. 차원의 저주파화 및 적정 개수 판단
* **왜 저주파인가?:** 여기서 저주파는 낮은 음을 뜻하는 게 아니라, **변화가 완만한 데이터**를 뜻합니다. DCT 과정을 거치며 고속으로 변하는 노이즈 성분을 제거하고 핵심적인 흐름만 남기기 때문입니다.
* **개수 결정:** * **음성 인식:** 표준 13개 사용.
    * **복합 소음 및 기계 진단:** 더 세밀한 분석을 위해 20개 이상의 계수와 Delta 성분을 적극 활용합니다.


***

***

좋습니다. 지금 만든 T20_Mfcc_008 구조를 기준으로, 질문에 주신 MFCC 이론을 현재 코드가 실제로 어떻게 구현하고 있는지 정확히 매핑해서 설명드리겠습니다.


---

🔊 1. 데이터 형태: (Frames × Coefficients)

📌 이론

MFCC는 (시간 프레임 × 계수) 구조

보통 20~40ms 프레임 + overlap



---

✅ 현재 구현 (T20)

👉 Frame = 256 샘플

#define G_T20_FFT_SIZE 256
#define G_T20_SAMPLE_RATE_HZ 1600.0f

👉 시간으로 환산:

256 / 1600Hz = 0.16초 (160ms)

즉:

항목	현재 구현

Frame 길이	160ms
Overlap	❌ 없음
구조	단일 프레임 처리



---

🔥 중요한 차이

👉 현재 구조는:

[Frame1] → [Frame2] → [Frame3]

👉 일반 음성 MFCC:

[Frame1]
    [Frame2]
        [Frame3]
(Overlap 50%)


---

📌 결론

👉 지금은 진동/센서용 MFCC (non-overlap batch) 구조
👉 음성용 MFCC와 다름 (하지만 진동 분석에는 적합)


---

🔊 2. 포먼트 & 스펙트럼 포락선

📌 이론

MFCC는 fine structure 제거

envelope만 남김



---

✅ 현재 구현

코드 흐름:

FFT → Power Spectrum → Mel Filterbank → log → DCT

핵심 코드

T20_applyMelFilterbank()
T20_computeDCT2()


---

🔍 실제 의미

1️⃣ FFT

T20_computePowerSpectrum()

→ 모든 주파수 정보 (fine structure 포함)


---

2️⃣ Mel Filterbank

sum += power[k] * mel_bank[m][k];

👉 여기서 발생:

좁은 주파수 → 넓은 대역으로 합침

sharp peak 제거



---

3️⃣ log + DCT

logf(sum)
cosf(...)

👉 결과:

스펙트럼 envelope만 남음

고주파 변동 제거



---

📌 결론

👉 현재 구현은 정석 MFCC 파이프라인 👉 실제로 포먼트/Envelope 추출 제대로 수행 중


---

🔊 3. 계수 구조 (c0 ~ c12)

📌 이론

계수	의미

c0	에너지
c1~c12	음색
고차	노이즈



---

✅ 현재 구현

#define G_T20_MFCC_COEFFS 13

T20_computeDCT2(..., 13)


---

🔍 중요한 포인트

❗ c0 포함 여부

현재 코드:

p_out[n] = sum;

👉 별도 energy 계산 없음
👉 즉:

c0 = "Mel energy 기반 값"


---

📌 결론

👉 현재 구조:

계수	의미

c0	log Mel energy
c1~c12	spectral envelope


👉 표준 MFCC와 동일 구조


---

🔊 4. Delta / Delta-Delta

📌 이론

Δ → 속도

Δ² → 가속도



---

✅ 현재 구현

1️⃣ 히스토리 저장

mfcc_history[G_T20_MFCC_HISTORY][...]


---

2️⃣ Delta

num += n * (plus - minus);

👉 정석 formula:

Δ = Σ n*(c(t+n) - c(t-n)) / (2Σn²)


---

3️⃣ Delta-Delta

next - 2*curr + prev

👉 2차 미분


---

📌 결론

👉 이론과 100% 동일 구현


---

🔊 5. 최종 Feature Vector

📌 이론

MFCC + Delta + Delta2 = 39차원


---

✅ 현재 구현

T20_buildVector39()

mfcc → delta → delta2


---

실제 구조

[ c0 ... c12 | Δ0 ... Δ12 | Δ²0 ... Δ²12 ]

👉 정확히 39차원


---

📌 추가 특징 (중요)

✔ 2가지 출력 모드 존재

EN_T20_OUTPUT_VECTOR
EN_T20_OUTPUT_SEQUENCE


---

1️⃣ Vector 모드

[39]


---

2️⃣ Sequence 모드

[T × 39]

👉 ring buffer:

seq_rb.data[frames][39]


---

📌 결론

👉 이론의 Frame × Feature 구조까지 구현 완료


---

🔊 6. 저주파화 (DCT 의미)

📌 이론

고주파 제거

저차 계수만 유지



---

✅ 현재 구현

cosf(...)

👉 DCT-II 직접 구현


---

🔍 실제 효과

빠르게 변하는 노이즈 제거

envelope만 유지



---

📌 추가 포인트

현재는:

13개만 사용

👉 이미 저주파 영역만 사용 중


---

🔊 7. 현재 구조의 "현실적 해석"

이걸 한 줄로 정리하면:


---

🎯 현재 T20 MFCC는

👉 "음성용 MFCC + 진동 신호 최적화 버전"


---

특징 요약

항목	상태

MFCC pipeline	✔ 정석
Mel filterbank	✔ 구현됨
DCT	✔ 구현됨
Delta / Δ²	✔ 구현됨
Vector 39	✔ 구현됨
Sequence	✔ 추가됨
Overlap	❌ 없음
Frame 길이	길다 (160ms)
Noise 처리	✔ 강화됨
Filter	✔ (LPF/HPF/BPF)



---

🔥 핵심 차별점 (중요)

이건 일반 MFCC랑 다릅니다:

1️⃣ 전처리 강화

DC 제거
Pre-emphasis
Noise gate
Spectral subtraction
Biquad filter

👉 일반 MFCC보다 훨씬 강력


---

2️⃣ 센서 기반 설계

audio → ❌

vibration / IMU → ✔


👉 그래서 overlap 제거됨


---

3️⃣ 실시간 RTOS 구조

ISR → SensorTask → ProcessTask

queue 기반 pipeline


👉 일반 DSP 코드보다 훨씬 구조적


---

🧠 최종 정리

이론 vs 현재 구현

이론	현재 구현

Frame × Coeff	✔
Envelope 추출	✔
MFCC 13	✔
Delta	✔
Δ²	✔
39 vector	✔
Sequence	✔ (추가)
Overlap	❌
Noise robustness	✔ (강화됨)



---

🚀 한 줄 핵심 요약

👉 지금 코드는 “표준 MFCC + 산업용 진동 분석용 확장 버전”이다


---

원하시면 다음 단계로:

👉 “Overlap + sliding window + real speech-grade MFCC 구조로 업그레이드 (009)”

👉 또는
👉 “TinyML 입력 최적화 (CNN/LSTM 입력 텐서 형태)”

까지 바로 이어서 설계해드릴게요.