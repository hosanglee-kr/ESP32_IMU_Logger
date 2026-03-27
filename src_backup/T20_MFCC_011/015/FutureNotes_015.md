# T20 MFCC 015 Notes

## 이번 단계 반영
- 014 기준 실컴파일 에러 대응 패치 라운드 2 성격의 구조 보완
- recorder batching drain 구조 추가
- 세션 동안 파일 핸들 유지 방식으로 flush 주기 제어 추가
- metadata heartbeat 주기 기록 추가
- queued / dropped / written recorder 통계 추가

## 큰 다음 단계까지 포함한 방향
- recorder batching/flush 최적화
- metadata 주기 저장
- 상태 출력 확장
- 다음 단계에서 AsyncWeb / 다운로드 / 이벤트 마커 API 연결 준비

## 향후 단계 TODO
- reopen / retry / 오류 복구
- 파일 rotate / 크기 제한
- live stream / web dashboard
- snapshot별 FFT / mel_bank cache
- zero-copy / DMA / cache aligned 최적화
