# BuildBaseline 056 Recovery Strategy

## 상태
056은 컴파일 복구 우선 기준 세트입니다.

## 이번 069 방향
- 068 selection sync 상태 계산을 유지
- viewer bundle / multi-frame 응답에 selection sync를 실제 반영
- type meta 정보도 viewer bundle에 함께 노출

## TODO
- BMI270 실센서 경로 연결
- esp-dsp 최적화
- 실제 하드웨어 DMA API 연계
- SD_MMC host slot / pin matrix 고도화
- Selection overlay multi-frame 확장
