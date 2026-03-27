# T20 MFCC 023 Notes

## 이번 단계 반영
- rotate 오래된 파일 정리 정책 추가
- rotate 정리 API 추가
- recorder 오류 복구 재시도 기본 구조 추가
- runtime config 조회/저장 API 추가
- 상태/오류 진단 항목 확대

## 구현 포인트
- 최근 rotate 항목 메모리 목록 유지
- keep 개수 기준 prune
- recorder reopen retry
- 마지막 recorder 오류 문자열 유지
- LittleFS runtime config 저장 엔드포인트

## 향후 단계 TODO
- 전체 JSON 스키마 정식 파서 전환
- multi-config FFT / mel cache
- zero-copy / DMA / cache aligned 최적화
- recorder binary batching + CRC
- rotate/세션 정리 정책 고도화
