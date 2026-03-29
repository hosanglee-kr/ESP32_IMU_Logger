# T20 MFCC 091 Notes

## 이번 단계 목적
- BMI270 actual SPI transaction / ISR attach 직전 구조를 더 실제화
- Todo / Roadmap 문서를 다시 유지

## 이번 단계 핵심
- begin/end transaction skeleton 추가
- DRDY ISR attach skeleton 추가
- register/burst read가 transaction 경유 구조로 정리

## 다음 핵심 단계
1. 실제 SPIClass begin
2. 실제 register/burst read
3. 실제 DRDY ISR attach
4. Recorder session finalize
