# T20 MFCC Todo Master v194

## 최상위 목표
- 055 기능 수준 복구
- 안정화 구조 유지
- 실센서 / Recorder / Parser / Viewer 축별 마감

## 현재 상태
### 완료
- compile identifier fix 반영
- actual SPI path 준비
- actual begin / actual read / burst read prep / burst runtime / SPI attach prep / ISR runtime / ISR queue 상태 준비
- recorder finalize publish/audit + store bundle + file write/bundle map + path route/write finalize + commit/finalize sync 상태 준비

## 진행 중
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 고도화
- Recorder finalize 실제 commit/finalize 저장 고도화
- Preview parser 강화

## 이번 단계 핵심
- begin/read 경로를 burst runtime / isr queue까지 확장
- finalize write path를 commit/finalize sync 단계까지 확장
- 다음 단계의 실제 하드웨어/파일 저장 치환 준비
