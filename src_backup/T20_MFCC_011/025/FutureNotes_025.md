# T20 MFCC 025 Notes

## 이번 단계 반영
- profile slot 저장/불러오기 추가
- 경량 JSON 파서 보강
- begin 시 runtime config 우선, profile fallback 적용
- multi-config mel cache 확장 스캐폴드 추가
- Web profile save/load API 추가

## 구현 포인트
- profile 슬롯 4개
- profile 파일 경로 자동 생성
- section 기반 경량 JSON 파싱
- mel cache key에 profile index 포함
- current_profile_index 상태 유지

## 향후 단계 TODO
- 전체 JSON 스키마 정식 파서 전환
- zero-copy / DMA / cache aligned 최적화
- FFT twiddle cache 다중화
- recorder binary batching + CRC
- 프로파일 이름 편집 / 목록 API
