# T20 MFCC 029 Notes

## 이번 단계 반영
- ArduinoJson 기반 config import/export 적용
- recorder file / summary / meta 다운로드 API 추가
- viewer data를 vector + log_mel + mfcc까지 확장
- manifest 기반 다운로드 예시를 웹 UI에서 확인 가능하도록 확장

## 이번 단계 핵심 API
- /api/t20/config/export
- /api/t20/config/import
- /api/t20/recorder/index
- /api/t20/recorder/manifest
- /api/t20/recorder/file?path=
- /api/t20/recorder/summary?path=
- /api/t20/recorder/meta?path=
- /api/t20/viewer/state
- /api/t20/viewer/data

## 향후 단계 TODO
- sequence viewer / waveform / spectrum / event timeline API
- profile별 FFT twiddle / mel bank 완전 snapshot 분리
- zero-copy / DMA / cache aligned recorder path
- range download / pagination / large file handling
- async web 차트 UI 고도화
