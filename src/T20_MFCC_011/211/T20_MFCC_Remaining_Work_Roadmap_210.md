# T20 MFCC Remaining Work Roadmap v210

## A. 즉시 구현 대상

### A-1. BMI270 실제 begin/burst/isr 연결
- SPIClass.begin 실제 호출
- burst read 실제 호출
- DRDY interrupt attach 실제 연결
- 성공/실패 상태를 alias 상태에 반영

### A-2. Recorder 실제 finalize 연결
- flush 실제 호출
- close 실제 호출
- finalize 실제 호출
- 저장 결과와 finalize 관련 상태 반영

---

## B. 이어서 구현할 대상

### B-1. raw -> frame -> dsp ingress
- raw pipe 적재
- frame build 호출
- dsp ingress 진입

### B-2. meta/report/audit/manifest/summary 동기화
- 파일 저장 경로 연결
- 실패 시 rollback/recover 방향 정의

---

## C. 마감 단계

### C-1. 디버그 번들 정리
- 중복 번들 축소
- 실제 연결이 끝난 후 ready/apply/pipeline/integration 중 중복되는 계층 정리

### C-2. 예외 처리 강화
- fallback
- recover
- save/commit failure 대응

---

## D. 앞으로의 권장 진행 단위

### 1단계
- BMI270 begin
- Recorder flush/close

### 2단계
- BMI270 burst/isr
- Recorder finalize/save

### 3단계
- raw -> frame -> dsp ingress
- meta/report/audit/manifest 저장

### 4단계
- Web 검증 / 로그 검증 / 예외 처리
