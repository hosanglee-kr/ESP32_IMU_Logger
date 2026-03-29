# T20 MFCC Remaining Work Roadmap v086

## 목표
055 기능 수준을 잃지 않으면서 081 이후의 안정화 구조를 유지한다.

## 현재 기준 전략
- 기능을 조금씩 여러 축에 동시에 추가하지 않는다.
- 축별 마감형으로 진행한다.
- 각 소스의 Todo 주석과 이 문서를 동기화한다.

---

## 트랙 A. BMI270 실센서
핵심:
- 실제 init
- 실제 raw read
- DRDY ISR
- hop_size 반영 frame build

출력물:
- `T20_Mfcc_Core_086.cpp`
- `T20_Mfcc_Dsp_086.cpp`

---

## 트랙 B. Recorder 세션 마감
핵심:
- raw/event/feature/config/metadata 분리 저장
- finalize/recover
- session summary
- rotate/prune 실제화

출력물:
- `T20_Mfcc_Recorder_086.cpp`
- `T20_Mfcc_Web_086.cpp`

---

## 트랙 C. Preview Parser 강화
핵심:
- header/delim/schema/column type 정교화
- preview row 품질 개선
- 고급 filter/compare 복구 검토

출력물:
- `T20_Mfcc_Core_086.cpp`
- `T20_Mfcc_Web_086.cpp`

---

## 트랙 D. UI / Viewer 정리
핵심:
- unified viewer bundle 화면 정리
- 상태 패널 강화
- 055의 유용한 진단 UI 선별 복구

출력물:
- `littlefs_t20_index.html.txt`
- `littlefs_t20_t20.js.txt`

---

## 다음 단계
### 086-A
BMI270 실센서 본체 연결

### 086-B
Recorder 세션 기능 마감

### 086-C
Preview Parser 강화

### 086-D
UI / Viewer 정리
