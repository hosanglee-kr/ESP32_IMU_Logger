# T20 MFCC Todo Master v086

## 1. 운영 원칙
- 이 문서는 전체 Todo의 **단일 기준 문서**입니다.
- 각 소스의 `향후 단계 구현 예정` 주석은 이 문서와 같은 방향을 유지합니다.
- 완료된 항목은 삭제하지 않고 상태만 갱신합니다.

---

## 2. 현재 단계 상태 요약

### 완료
- 빌드 안정화 기본 구조
- Viewer / TypeMeta / Preview Parser 기본 고도화
- Recorder / Storage 기본 구조
- `T20_jsonWriteDoc` 복구
- Live source skeleton
- Recorder session 기능 1차
- BMI270 live finalize 준비 구조
- 재초기화 / heartbeat / queue 상태 구조

### 진행 중
- BMI270 실센서 본체 연결
- Recorder session 마감
- Preview parser 정교화
- UI / Viewer 정리

### 미착수
- 실제 DRDY ISR
- 실제 gyro/acc raw read
- raw/event/feature/config/metadata 분리 저장
- preview column type 정교화

---

## 3. 최우선 작업 순서

### P1. BMI270 실센서 본체 연결
상태: 진행 중

세부 작업:
- [ ] SPI begin 실제 연결
- [ ] BMI270 chip id 확인
- [ ] ODR/DRDY 실제 설정
- [ ] 실제 raw sample read
- [ ] live frame build와 hop_size 실경로 연결

관련 소스:
- `T20_Mfcc_Core_086.cpp`
- `T20_Mfcc_Dsp_086.cpp`
- `T20_Mfcc_Inter_086.h`

---

### P2. Recorder 세션 기능 마감
상태: 진행 중

세부 작업:
- [ ] raw/event/feature/config/metadata 분리 저장
- [ ] binary finalize/recover 강화
- [ ] session summary/manifest 정리
- [ ] rotate/prune 실제 파일 처리

관련 소스:
- `T20_Mfcc_Recorder_086.cpp`
- `T20_Mfcc_Web_086.cpp`
- `T20_Mfcc_Inter_086.h`

---

### P3. Preview Parser 강화
상태: 진행 중

세부 작업:
- [ ] header 기반 컬럼 타입 추론 강화
- [ ] csv/tsv/pipe/plain text 외 추가 힌트 정리
- [ ] preview sample row 개선
- [ ] advanced filter/compare 구조 복구 검토

관련 소스:
- `T20_Mfcc_Core_086.cpp`
- `T20_Mfcc_Web_086.cpp`

---

### P4. UI / Viewer 정리
상태: 진행 중

세부 작업:
- [ ] unified viewer bundle 화면 정리
- [ ] recorder/live/preview 상태 가시화 강화
- [ ] 055 대비 필요한 viewer/event/history 복구 검토

관련 소스:
- `littlefs_t20_index.html.txt`
- `littlefs_t20_t20.js.txt`
- `T20_Mfcc_Web_086.cpp`

---

## 4. 055 대비 복구 체크

### Recorder
- [ ] session lifecycle
- [ ] binary header finalize
- [ ] recover
- [ ] event/metadata write

### DSP / Live
- [ ] BMI270 actual init
- [ ] sliding/ring/frame build 정교화
- [ ] preprocess pipeline 확장

### CSV / Preview
- [ ] schema/type/filter 고급 기능 복구
- [ ] column hint 정교화

### Viewer
- [ ] event/history 복구 검토
- [ ] multi-frame 상세화

---

## 5. 다음 권장 구현 묶음
1. **086-A** BMI270 실센서 본체 연결
2. **086-B** Recorder 세션 기능 마감
3. **086-C** Preview Parser 강화
4. **086-D** UI / Viewer 정리
