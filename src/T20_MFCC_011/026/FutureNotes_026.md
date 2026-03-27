# T20 MFCC 026 Notes

## 이번 단계 반영
- profile slot 저장/불러오기 유지
- section 기반 경량 JSON 파서 -> section helper 강화
- schema export API 추가
- FFT twiddle cache scaffold 추가
- recorder batching/flush/CRC/binary header 추가

## 이번 단계 핵심
- /api/t20/config/schema
- recorder flush interval 및 batch buffer
- binary header payload CRC 갱신
- twiddle cache slot 기반 준비

## 향후 단계 TODO
- 정식 JSON schema validator 또는 ArduinoJson 기반 구조화 파서 전환
- multi-config FFT/mel cache 완전 분리
- zero-copy / DMA / cache aligned I/O
- recorder summary / index / download metadata
- profile name 편집, profile list 조회 API
