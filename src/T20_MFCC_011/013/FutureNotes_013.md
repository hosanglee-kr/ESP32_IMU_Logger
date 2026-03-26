# T20 MFCC Compile Patch + Stage C Recorder Notes

## 이번 단계 반영
- 012 기반 실컴파일 지향 보완
- SD_MMC recorder 대묶음 C 기본 구조
- session open/close + config/event/raw/feature writer 골격
- 주석 정책 반영
  - 향후 예정 사항만 English TODO
  - 그 외 주석은 한글 유지

## 다음 단계 예정

### 1. Recorder 고도화
- recorder 전용 queue/task
- binary header / chunk / flush 주기
- metadata 주기 저장
- error recovery / reopen
- fs health monitor

### 2. Web/Control
- AsyncWeb 설정 API
- measurement start/stop API
- live status / file list / download
- waveform / MFCC 시각화

### 3. DSP 확장
- spectral subtraction 공개 옵션
- preprocess stage type 추가
- multi-config FFT / mel bank cache
- overlap/hop runtime tuning UI

### 4. 성능
- zero-copy frame view
- DMA 친화 recorder path
- cache-aware copy reduction
- task timing profiler
