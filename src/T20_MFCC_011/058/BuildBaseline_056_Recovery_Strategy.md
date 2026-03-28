# BuildBaseline 056 Recovery Strategy

## 상태
056은 컴파일 복구 우선 기준 세트입니다.

## 이번 058 방향
- 056/057 컴파일 안정성을 유지
- DSP 경로를 유지하면서 런타임 샘플 처리 경로까지 연결
- Recorder / Web / JSON 인터페이스는 정합성을 그대로 유지

## TODO
- BMI270 실센서 경로 연결
- esp-dsp 최적화
- zero-copy / DMA 실제화
- SD_MMC profile 실제화
- Viewer / TypeMeta / Selection sync 고도화
