# T20 MFCC 116 Notes

## 이번 단계 목적
- BMI270 actual transaction pipeline과 Recorder finalize pipeline 흐름을 크게 묶어 진행
- Todo / Roadmap 문서를 다시 유지

## 이번 단계 핵심
- BMI270 transaction pipeline state 추가
- Recorder finalize pipeline state 추가
- Web/UI에서 pipeline 상태 확인 가능하게 추가

## 다음 핵심 단계
1. 실제 SPIClass begin
2. 실제 register/burst read
3. 실제 DRDY ISR attach
4. recorder finalize 실제 persist/commit/write 저장 고도화
