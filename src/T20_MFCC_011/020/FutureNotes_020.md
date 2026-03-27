# T20 MFCC 020 Notes

## 이번 단계 반영
- 가능한 큰 묶음 기준으로 다음 항목 진행
  - websocket/SSE 실시간 push 골격
  - 설정 JSON 적용 보강
  - periodic push hook
  - static UI 갱신
  - live push endpoint 추가

## 현재 단계 성격
- 안정성 우선 유지
- 실제 websocket broadcast는 다음 단계에서 마무리
- polling API는 계속 유지

## 향후 단계 TODO
- websocket 실제 broadcast / client 관리
- SSE 스트림 지속 연결 처리
- recorder rotate 연계 확장(meta/event/feature)
- zero-copy / DMA / cache aligned 최적화
- live decimation / chart aggregation
