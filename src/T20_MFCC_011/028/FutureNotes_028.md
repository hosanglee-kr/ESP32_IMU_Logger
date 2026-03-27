# T20 MFCC 028 Notes

## 이번 단계 반영
- parser-lite 방식 config import 적용
- recorder manifest export/API 추가
- viewer data export/API 추가
- recorder index/summary/meta 유지
- 웹 UI에서 manifest/viewer data 확인 골격 추가

## 이번 단계 핵심
- /api/t20/config/import (parser-lite)
- /api/t20/recorder/manifest
- /api/t20/viewer/data
- latest vector 기반 viewer data export
- recorder close 후 manifest 저장

## 향후 단계 TODO
- ArduinoJson 기반 정식 구조 파서 및 schema validator
- download API / range download / 파일 직접 다운로드 링크
- viewer용 파형/스펙트럼/이벤트/sequence 시각화 데이터
- profile별 FFT twiddle / mel cache 완전 분리
- zero-copy / DMA / cache aligned recorder 최적화
