# T20 MFCC 잔여 진행 계획 v083

## 현재까지 완료된 큰 묶음
- 076-A: 빌드 안정화
- 077-B: Viewer / TypeMeta / Preview Parser 고도화
- 078-C: Recorder / Storage 정리
- 079~081: `T20_jsonWriteDoc` 복구 + live source skeleton
- 082-A: Recorder 세션 기능 복구 1차
- 083: BMI270 live path 본체 진입 구조 1차

## 남은 핵심 작업

### 1. BMI270 실센서 본체 마감
대상:
- `T20_Mfcc_Core_083.cpp`
- `T20_Mfcc_Dsp_083.cpp`

작업:
- 실제 BMI270 begin/init
- 실제 raw sample read
- DRDY ISR/queue 수집
- hop_size 반영된 frame build 마감

### 2. Recorder 세션 기능 마감
대상:
- `T20_Mfcc_Recorder_083.cpp`
- `T20_Mfcc_Web_083.cpp`

작업:
- raw/event/feature/config/metadata 분리 저장
- binary finalize/recover 강화
- session summary/manifest 정리

### 3. Preview Parser 강화
대상:
- `T20_Mfcc_Core_083.cpp`
- `T20_Mfcc_Web_083.cpp`

작업:
- header 기반 컬럼 타입 추론 강화
- 컬럼 후보 힌트 정교화
- preview sample row 개선

### 4. UI/Viewer 정리
대상:
- `littlefs_t20_index.html.txt`
- `littlefs_t20_t20.js.txt`

작업:
- unified viewer bundle 화면 정리
- recorder/live/preview 상태 가시화 강화

## 가장 효율적인 다음 순서
1. BMI270 실센서 본체 마감
2. Recorder 세션 기능 마감
3. Preview Parser 강화
4. UI/Viewer 정리
