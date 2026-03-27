# T20 MFCC 019 Notes

## 이번 단계 반영
- 가능한 큰 묶음 기준으로 다음 항목 진행
  - live waveform API
  - live sequence API
  - 설정 JSON POST 보강
  - latest wave frame 조회 API
  - sequence shape 조회 API
  - 정적 UI 확장

## 현재 단계 성격
- 안정성 우선 유지
- 설정 변경은 측정 중 차단
- Web 조회는 polling 기반 유지

## 향후 단계 TODO
- websocket / SSE 기반 실시간 push
- 고급 JSON 파서 기반 전체 설정 반영
- recorder rotate 연계 확장(meta/event/feature)
- zero-copy / DMA / cache aligned 최적화
