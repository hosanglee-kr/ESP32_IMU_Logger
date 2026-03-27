# T20 MFCC 021 Notes

## 이번 단계 반영
- 가능한 큰 묶음 기준으로 다음 항목 진행
  - WebSocket 실시간 push 실제 연결
  - SSE 실시간 push 실제 연결
  - live bundle JSON 송신
  - recorder rotate 동반 회전 강화
  - 설정 JSON 적용 보강 유지

## 현재 단계 성격
- 안정성 우선 유지
- polling API + push API 병행
- rotate는 raw/meta/event/feature 동반 회전까지 확장

## 향후 단계 TODO
- websocket 인증 / 권한 제어
- SSE reconnect / last-event-id
- multi-config FFT / mel cache
- zero-copy / DMA / cache aligned 최적화
- push payload decimation / aggregation
- rotate 파일 목록/정리 API
