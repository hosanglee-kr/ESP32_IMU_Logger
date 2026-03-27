# T20 MFCC 022 Notes

## 이번 단계 반영
- rotate 파일 목록 API 추가
- rotate 파일 삭제 API 추가
- recorder rotate index 기록 추가
- live push decimation / force push 정책 추가
- polling + push 병행 유지

## 구현 포인트
- status hash 기반 상태 변화 감지
- live update / force push 기준 분리
- 최근 rotate 항목 메모리 유지
- rotate index jsonl 기록

## 향후 단계 TODO
- websocket 인증 / 권한 제어
- 전체 JSON 스키마 정식 파서 전환
- multi-config FFT / mel cache
- zero-copy / DMA / cache aligned 최적화
- rotate 오래된 파일 정리 정책
