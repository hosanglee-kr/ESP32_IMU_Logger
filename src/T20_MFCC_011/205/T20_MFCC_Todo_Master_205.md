# T20 MFCC Todo Master v205

## 이번 단계 핵심
- 다음 단계 조금 더 크게 묶어서 apply pipeline bundle까지 확장
- 반복 오류 패턴 점검 유지
- alias accessor 우선 정책 유지

## 다음 우선순위
1. 실제 SPIClass.begin 코드 연결
2. 실제 burst read 코드 연결
3. 실제 DRDY ISR attachInterrupt 연결
4. 실제 flush / close / finalize 연결
5. raw -> frame -> dsp ingress / meta-report-audit-manifest sync 실연결
