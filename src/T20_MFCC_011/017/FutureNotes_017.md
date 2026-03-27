# T20 MFCC 017 Notes

## 이번 단계 반영
- A안 안정성 우선 방향 적용
- AsyncWeb + LittleFS 정적 UI 골격 추가
- status / config / latest / measurement start / stop / files / download API 유지
- 정적 파일 html/css/js 예시 포함

## LittleFS 배포 예시
- littlefs_t20_index.html.txt -> /t20/index.html
- littlefs_t20_t20.css.txt -> /t20/t20.css
- littlefs_t20_t20.js.txt -> /t20/t20.js

## 향후 단계 TODO
- 설정 변경 POST body 실제 반영
- live waveform / live feature stream
- recorder reopen / retry / rotate
- multi-config FFT / mel cache 완전 분리
- zero-copy / DMA / cache aligned 최적화
