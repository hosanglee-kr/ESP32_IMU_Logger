# T20 MFCC Todo Master v106

## 최상위 목표
- 055 기능 수준 복구
- 안정화 구조 유지
- 실센서 / Recorder / Parser / Viewer 축별 마감

## 현재 상태
### 완료
- actual SPI path 준비
- actual read transaction / reg-burst / start-hook-request 상태 준비
- recorder finalize save/persist/result/exec 상태 준비

### 진행 중
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 고도화
- Recorder finalize 실제 persist 저장 고도화
- Preview parser 강화

## 우선순위
### P1. BMI270 실센서 본체
- [ ] 실제 SPIClass begin
- [ ] 실제 register read
- [ ] 실제 DRDY ISR attach
- [ ] 실제 raw burst decode 및 축 매핑 고도화
- [ ] 실제 live frame build 마감

### P2. Recorder finalize
- [ ] binary footer/header rewrite
- [ ] summary/manifest 실제 저장 고도화
- [ ] persist/recover/rewrite 연동
