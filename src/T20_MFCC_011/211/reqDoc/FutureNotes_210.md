# T20 MFCC 210 Notes

## 이번 단계 목적
- 잔여 구현 계획을 다시 정리
- 앞으로는 상태 확장보다 실제 연결 치환 중심으로 진행 방향 전환

## 핵심 결론
- BMI270: begin / burst / isr 실제 연결이 최우선
- Recorder: flush / close / finalize 실제 연결이 최우선
- raw -> frame -> dsp ingress / meta-report-audit-manifest sync 는 그 다음
- md와 주석도 모두 이 우선순위 기준으로 갱신
