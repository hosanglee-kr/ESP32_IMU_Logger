# T20 MFCC 018 Notes

## 이번 단계 반영
- 가능한 큰 묶음 기준으로 다음 항목 진행
  - AsyncWeb 설정 조회/설정 적용 POST
  - live status / live latest API
  - LittleFS 정적 html/css/js 예제 확장
  - recorder reopen/retry/rotate 1차 골격 반영

## 주의
- 설정 변경은 안정성 우선 정책에 따라 측정 중에는 차단
- JSON 파싱은 현재 단계에서 부분 파싱 방식
- recorder rotate는 raw 파일 중심의 1차 구현

## 향후 단계 TODO
- live waveform / live sequence
- WebSocket / SSE
- 고급 JSON 파서 기반 전체 설정 반영
- recorder rotate 연계 확장(meta/event/feature)
- zero-copy / DMA / cache aligned 최적화
