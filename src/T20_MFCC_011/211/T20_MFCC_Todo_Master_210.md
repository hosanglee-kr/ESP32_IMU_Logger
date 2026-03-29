# T20 MFCC Todo Master v210

## 0. 현재 상태 재점검 요약

지금까지는 **실제 연결 직전의 상태 흐름 / 번들 / 디버그 라우트 / 계획 문서**를 매우 넓게 깔아 두는 단계가 중심이었다.
즉, 구조는 많이 준비되었지만 **실제 HW/FS/DSP 연결 코드는 아직 placeholder 성격이 강하다.**

### 이미 충분히 준비된 것
- BMI270 관련 단계별 상태 흐름
  - begin / burst / isr / raw pipe / frame build / dsp ingress
  - ready / exec / apply / pipeline / integration / final integration / mega bundle 등
- Recorder 관련 단계별 상태 흐름
  - flush / close / finalize
  - meta / report / audit / manifest / finalize sync
  - ready / exec / apply / pipeline / integration / final integration / mega bundle 등
- Web/UI 점검 엔드포인트 및 버튼
- alias accessor 기반의 호환 계층
- 반복 오류 패턴 점검 문서

### 아직 실질적으로 남은 핵심
- BMI270 실제 SPIClass.begin
- BMI270 실제 burst read
- BMI270 실제 DRDY ISR attachInterrupt
- raw -> frame -> dsp ingress 실연결
- Recorder flush / close / finalize 실연결
- meta / report / audit / manifest / summary 동기화 실연결
- LittleFS / SD write 실제 연결

---

## 1. 남은 구현의 큰 방향

이제부터는 **새로운 상태 이름을 더 늘리는 것보다**
**기존에 준비된 alias/번들 구조 위에 실제 코드를 꽂는 단계**가 효율적이다.

즉, 앞으로의 우선순위는 아래처럼 바뀐다.

### 1차 우선순위
1. BMI270 실제 begin 연결
2. BMI270 실제 burst read 연결
3. BMI270 실제 DRDY ISR 연결
4. Recorder 실제 flush / close / finalize 연결

### 2차 우선순위
5. raw -> frame build -> dsp ingress 실제 연결
6. meta / report / audit / manifest / summary 실제 동기화
7. LittleFS / SD 경로별 실제 저장

### 3차 우선순위
8. 실데이터 기준 Web/UI 점검
9. 예외/오류/rollback/recover 정리
10. 단계별 번들 JSON 축소/정리

---

## 2. 단계별 진행 계획

### Stage A. BMI270 실제 연결 최소 완성
목표:
- SPIClass.begin 실제 호출
- burst read 실제 호출
- DRDY ISR attachInterrupt 실제 연결

완료 기준:
- placeholder가 아닌 실제 함수 호출 존재
- 성공/실패에 따라 alias 상태 반영
- Web에서 real/final/integration bundle 값 변화 확인

### Stage B. Recorder 실제 finalize 최소 완성
목표:
- flush 실제 호출
- close 실제 호출
- finalize 실제 호출
- commit/finalize/save 계열 상태 반영

완료 기준:
- 실제 파일 핸들 기반 처리 코드 반영
- 성공/실패 상태가 Web JSON에 반영됨

### Stage C. Raw -> Frame -> DSP ingress 연결
목표:
- burst 결과를 raw pipe에 적재
- frame build와 연결
- dsp ingress 호출부 연결

완료 기준:
- 샘플/프레임 길이 갱신
- 최소 1개 이상 실제 frame build 경로 정리

### Stage D. Recorder 동기화 계층 연결
목표:
- meta / report / audit / manifest / summary 저장
- LittleFS / SD 경로별 처리 연결

완료 기준:
- json/text/file 저장 함수 골격이 아닌 실제 호출 경로 존재
- 경로 선택과 결과 상태가 일관됨

### Stage E. 예외 처리 / 정리
목표:
- rollback / recover / fallback
- 중복 상태/번들 정리
- 디버그 JSON 통합 정리

---

## 3. 효율적 진행 원칙

### 원칙 1. 더 이상 staged 이름을 무한히 늘리지 않음
이미 충분히 많은 상태 번들이 있으므로,
이제는 새 이름 추가보다 **실제 코드 치환**에 집중한다.

### 원칙 2. direct member access보다 alias accessor 우선
구조체 멤버 불일치 재발 방지를 위해 계속 유지한다.

### 원칙 3. 헤더에는 선언만
struct 밖 전역 정의 금지.
multiple definition 재발 방지.

### 원칙 4. 한 단계에서 "HW 1개 + Recorder 1개" 같이 진행
BMI270만 진전시키지 말고,
항상 Recorder도 같은 폭으로 전진시키는 것이 전체 구조 검증에 효율적이다.

### 원칙 5. 문서/주석은 앞으로 "실제 연결 기준"으로 갱신
상태 이름 나열보다
- 무엇이 placeholder인지
- 무엇이 실제 연결됐는지
- 다음에 무엇을 꽂아야 하는지
를 명확히 적는다.

---

## 4. 이번 v210 문서 갱신 포인트

- 기존 계획 md 전체를 **실제 연결 중심**으로 재정리
- 반복 오류 패턴 문서에 **header stray global / multiple definition / alias 우선** 유지
- source 상단 주석을 **잔여 구현 계획 + 실제 연결 우선순위** 기준으로 재작성
