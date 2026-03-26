# T20 MFCC StageA+StageB Future Notes

## 이번 단계 반영
- 전처리 stage 배열화 구조
- sliding window / hop_size 구조
- sample ring buffer 기반 frame assemble
- session state machine 기본형
- button short-press 기반 measurement start/stop

## 향후 단계 구현 예정

### 1. Recorder / Storage
- SD_MMC mount / health check
- session folder 생성
- raw writer
  - CSV / Binary 선택
- config writer
  - JSON
- metadata writer
  - JSONL / CSV 선택
- event writer
  - JSONL / CSV 선택

### 2. Session 확장
- session id
- start / stop timestamp
- marker event
- summary stats
- file manifest

### 3. Web / AsyncWeb
- config read / apply / rollback
- measurement start / stop
- live status
- live waveform
- live log-mel / mfcc / vector
- file list / download

### 4. DSP 고도화
- spectral subtraction dedicated config
- stage type 확장
  - normalize
  - RMS normalize
  - clip
  - detrend
  - moving average
- snapshot별 mel bank/FFT cache
- multi-config runtime cache

### 5. Performance / Zero-copy
- aligned buffers 유지
- sample ring direct frame view 최적화
- DMA friendly storage path
- cache-aware copy reduction
- task timing/profiling
