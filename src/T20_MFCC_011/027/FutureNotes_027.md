# T20 MFCC 027 Notes

## 이번 단계 반영
- recorder summary/meta/index 생성
- recorder index export API 추가
- viewer state API 및 웹 표시 추가
- schema export 유지
- section helper 기반 config parser 유지
- twiddle cache scaffold 유지

## 이번 단계 핵심
- /api/t20/recorder/index
- /api/t20/viewer/state
- recorder close 시 summary/meta/index 저장
- 상위 웹 UI에서 recorder 목록 표시
- 향후 큰 단계로 정식 구조화 parser / 고급 viewer / zero-copy 최적화 준비

## 향후 단계 TODO
- ArduinoJson 기반 정식 구조 파서 및 스키마 검증
- profile별 FFT twiddle / mel bank 완전 분리 캐시
- recorder binary record format 버전업 및 block footer
- download manifest / partial range download
- async web viewer 파형/스펙트럼/이벤트 타임라인
- zero-copy / DMA / cache aligned 저장 경로
