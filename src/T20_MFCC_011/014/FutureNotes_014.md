# T20 MFCC 014 Notes

## 이번 단계 반영
- 013 기준 실제 컴파일 오류 대응 패치 라운드
- recorder 전용 queue/task 추가
- binary raw header 설계 및 구현
- event/raw/feature 비동기 기록 구조 반영

## 향후 단계 TODO
- recorder batching / flush 주기 / reopen retry
- metadata 주기 저장
- AsyncWeb 설정 변경 / 측정 시작·종료 / 다운로드
- live waveform / live MFCC
- snapshot별 FFT / mel bank cache
- zero-copy / DMA / cache aligned 최적화
