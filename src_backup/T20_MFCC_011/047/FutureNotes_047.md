# T20 MFCC 047 Notes

## 이번 단계 반영
- SD_MMC backend 실제 open/mount/unmount 골격 추가
- recorder batch 누적 버퍼에 정렬/aligned skeleton 강화
- CSV 타입 메타 추론과 고급 조회 연결 기반 유지
- 멀티 캔버스 render sync 상태 표시 강화

## 이번 단계 의미
- recorder 저장 backend가 LittleFS 전용 골격에서 SD_MMC 실제 연결 방향으로 한 단계 전진
- batch 버퍼는 이후 DMA/cache aligned 최적화를 적용할 수 있는 형태로 정리됨
- 차트는 shared sync state에서 실제 render sync 방향의 표현이 더 선명해짐

## 향후 단계 TODO
- SD_MMC 보드별 pin/mode 안정화
- zero-copy / DMA / cache aligned 실제 적용
- 멀티 canvas 동시 렌더/selection 동기 정교화
- 타입 메타 캐시를 정렬/필터/다운샘플 preview와 연동
