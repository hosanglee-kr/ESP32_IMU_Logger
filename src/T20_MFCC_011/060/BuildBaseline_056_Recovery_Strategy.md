# BuildBaseline 056 Recovery Strategy

## 상태
056은 컴파일 복구 우선 기준 세트입니다.

## 이번 060 방향
- 056~059 컴파일 안정성을 유지
- Recorder 실저장 경로 위에 SD_MMC profile 분기 구조를 추가
- zero-copy batching 준비 단계까지 반영

## TODO
- BMI270 실센서 경로 연결
- esp-dsp 최적화
- zero-copy / DMA 실제화
- SD_MMC profile 실제 하드웨어 분기
- Viewer / TypeMeta / Selection sync 고도화
