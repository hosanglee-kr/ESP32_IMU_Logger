# T20 MFCC Todo Master v098

## 최상위 목표
- 055 기능 수준 복구
- 안정화 구조 유지
- 실센서 / Recorder / Parser / Viewer 축별 마감

## 현재 상태
### 완료
- actual SPI path 준비
- SPI begin / transaction / attach bridge
- actual register read bridge
- actual burst read bridge
- burst -> axis/sample 임시 decode
- recorder finalize 준비 상태 추가

### 진행 중
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 고도화
- Recorder session finalize 마감
- Preview parser 강화

## 우선순위
### P1. BMI270 실센서 본체
- [ ] 실제 SPIClass begin
- [ ] 실제 register read
- [ ] 실제 DRDY ISR attach
- [ ] 실제 raw burst decode 및 축 매핑 고도화
- [ ] 실제 live frame build 마감

### P2. Recorder session finalize
- [ ] binary footer/header rewrite
- [ ] summary/manifest 실제 저장
- [ ] recover/finalize/rewrite 연동
