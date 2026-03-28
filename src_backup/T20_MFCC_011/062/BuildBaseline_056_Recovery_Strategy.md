# BuildBaseline 056 Recovery Strategy

## 상태
056은 컴파일 복구 우선 기준 세트입니다.

## 이번 062 방향
- 056~061 컴파일 안정성을 유지
- runtime config 양방향 동기화를 강화
- zero-copy / DMA write 준비 구조를 반영

## TODO
- BMI270 실센서 경로 연결
- esp-dsp 최적화
- zero-copy / DMA 실제 write 경로
- SD_MMC profile 실제 하드웨어 분기 고도화
- Viewer / TypeMeta / Selection sync 고도화
