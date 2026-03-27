# T20 MFCC 024 Notes

## 이번 단계 반영
- config export/import API 추가
- 경량 JSON key/value 파서 추가
- runtime config LittleFS load/save 보강
- mel bank cache scaffold 추가
- 다음 단계용 multi-config cache 확장 기반 확보

## 구현 포인트
- begin() 시 runtime config auto-load 시도
- setConfig() 성공 시 runtime config save
- /api/t20/config/export
- /api/t20/config/import
- 정식 JSON 라이브러리 없이 경량 파서 사용
- mel bank 재생성 최소화용 캐시 골격

## 향후 단계 TODO
- 전체 JSON 스키마 정식 파서 전환
- zero-copy / DMA / cache aligned 최적화
- FFT twiddle cache 다중화
- recorder binary batching + CRC
- 복수 설정 프로파일 전환 UI
