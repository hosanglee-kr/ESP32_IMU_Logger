# T20 MFCC 043 Notes

## 이번 단계 반영
- recorder 전용 queue/task 골격 추가
- process task -> recorder task 비동기 전달 흐름 추가
- batch pending / flush requested / flush count 상태 추가
- recorder flush 요청 웹 경로 추가
- 향후 대규모 batch flush 최적화를 위한 구조적 발판 마련

## 이번 단계 의미
- recorder는 이제 process task 직접 파일 쓰기에서 한 단계 분리됨
- open / queue enqueue / recorder task / finalize 흐름의 큰 골격이 연결됨
- 안정성 우선 구조로 stop/finalize 자동화 방향에 더 가까워짐

## 향후 단계 TODO
- recorder task 내부 실제 batch 묶음 write 최적화
- stop/finalize 시 queue drain 보장
- SD_MMC writer 공통 추상화
- CSV 서버측 컬럼별 필터/정렬 방향/숫자형 정렬 강화
- 멀티 캔버스 동기 zoom/pan 정교화
- zero-copy / DMA / cache aligned 최적화
