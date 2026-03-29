# T20 MFCC 194 Notes

## 이번 단계 목적
- actual SPI burst/isr runtime와 finalize commit/finalize 흐름을 더 크게 묶어 전진

## 핵심 반영
- BMI270 burst_runtime_state / isr_queue_state 추가
- Recorder commit_route_state / finalize_sync_state 추가
- Web/UI에서 burst-isr-runtime / commit-finalize 확인 가능하게 추가

## 다음 핵심 단계
1. 실제 SPIClass begin 연결
2. 실제 register/burst read
3. 실제 DRDY ISR attach
4. recorder finalize 실제 commit/finalize 저장 고도화
