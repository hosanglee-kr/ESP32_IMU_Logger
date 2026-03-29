# T20 MFCC Todo Master v197

## 잔여 구현계획 재점검 결과

### 1. BMI270
현재 준비 완료:
- begin request
- spi begin runtime
- spi attach prep
- actual read
- register read runtime
- burst read prep
- burst runtime
- isr runtime
- isr queue
- real begin
- real burst
- real isr
- hw link
- frame build

남은 핵심:
- 실제 SPIClass.begin 연결
- 실제 burst read 연결
- 실제 DRDY ISR attachInterrupt 연결
- raw -> frame -> dsp ingress 실제 연결

### 2. Recorder
현재 준비 완료:
- store bundle
- file write
- bundle map
- path route
- write commit
- write finalize
- commit route
- finalize sync
- real flush
- real close
- real finalize
- meta sync
- report sync

남은 핵심:
- 실제 flush / close / finalize
- meta / report / audit / manifest / summary 동기화 실연결
- LittleFS / SD 경로별 실제 저장

## 이번 단계 핵심
- hw link / frame build / meta sync / report sync 까지 더 크게 묶음
- 추가/수정시 변수/상수 미정의 점검 반영 유지
