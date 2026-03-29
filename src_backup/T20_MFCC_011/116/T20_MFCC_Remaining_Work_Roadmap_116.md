# T20 MFCC Remaining Work Roadmap v116

## 현재 전략
- Todo Master와 각 소스 주석을 함께 관리
- 축별 마감형 진행
- 이번 단계는 BMI270 actual transaction pipeline과 Recorder finalize pipeline 흐름을 조금 더 크게 묶어 진행

## 트랙 A. BMI270
현재:
- actual read transaction / reg-burst / start-hook-request / exec-apply-session / pipeline state까지 준비

다음:
- 실제 SPI / 실제 register+bust read / 실제 ISR / 실제 raw decode 고도화

## 트랙 B. Recorder
현재:
- finalize save + persist + result + exec + commit + write + pipeline 상태 준비
다음:
- finalize/recover / 실제 저장 / 분리 저장

## 트랙 C. Parser/UI
현재:
- 기본 구조 존재
다음:
- 정교화
