# T20 MFCC 잔여 진행 계획 v082

## 현재까지 완료된 큰 묶음
- 076-A: 빌드 안정화
- 077-B: Viewer / TypeMeta / Preview Parser 고도화
- 078-C: Recorder / Storage 기본 정리
- 079~081: `T20_jsonWriteDoc` 복구 + live source skeleton
- 082-A: Recorder 세션 기능 복구 1차

## 남은 핵심 작업

### 1. BMI270 실센서 본체 연결
대상:
- `T20_Mfcc_Core_082.cpp`
- `T20_Mfcc_Dsp_082.cpp`

작업:
- BMI270 begin/init
- raw sample read
- DRDY 기반 수집
- live frame build
- hop_size 실경로 반영

### 2. Recorder 세션 기능 마감
대상:
- `T20_Mfcc_Recorder_082.cpp`
- `T20_Mfcc_Web_082.cpp`

작업:
- raw/event/feature/config/metadata 분리 저장
- binary finalize/recover 강화
- session summary/manifest 정리

### 3. Preview Parser 강화
대상:
- `T20_Mfcc_Core_082.cpp`
- `T20_Mfcc_Web_082.cpp`

작업:
- header 기반 컬럼 타입 추론 강화
- csv/tsv/pipe/plain text 외 추가 힌트 정리
- preview sample row 개선

### 4. UI/Viewer 정리
대상:
- `littlefs_t20_index.html.txt`
- `littlefs_t20_t20.js.txt`

작업:
- unified viewer bundle 화면 정리
- recorder/live/preview 상태 가시화 강화

## 가장 효율적인 다음 순서
1. BMI270 실센서 본체 연결
2. Recorder 세션 기능 마감
3. Preview Parser 강화
4. UI/Viewer 정리
