# T20 MFCC Todo Master v091

## 최상위 목표
- 055 기능 수준 복구
- 안정화 구조 유지
- 실센서 / Recorder / Parser / Viewer 축별 마감

## 현재 상태
### 완료
- actual SPI path 준비
- transaction begin/end skeleton
- DRDY ISR attach skeleton
- burst read skeleton

### 진행 중
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode
- Recorder session finalize
- Preview parser 강화

## 우선순위
### P1. BMI270 실센서 본체
- [ ] 실제 SPIClass begin
- [ ] 실제 register read
- [ ] 실제 DRDY ISR attach
- [ ] 실제 raw burst decode
- [ ] 실제 live frame build 마감
