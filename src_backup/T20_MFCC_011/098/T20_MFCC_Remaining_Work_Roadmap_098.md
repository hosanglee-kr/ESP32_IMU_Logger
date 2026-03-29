# T20 MFCC Remaining Work Roadmap v098

## 현재 전략
- Todo Master와 각 소스 주석을 함께 관리
- 축별 마감형 진행
- 이번 단계는 BMI270 actual path와 Recorder finalize 준비를 조금 크게 묶어 진행

## 트랙 A. BMI270
현재:
- actual register read bridge / actual burst bridge / axis decode 임시 경로 준비

다음:
- 실제 SPI / 실제 register+bust read / 실제 ISR / 실제 raw decode 고도화

## 트랙 B. Recorder
현재:
- session 1차 복구 + finalize state 준비
다음:
- finalize/recover / 분리 저장 / summary 저장

## 트랙 C. Parser/UI
현재:
- 기본 구조 존재
다음:
- 정교화
