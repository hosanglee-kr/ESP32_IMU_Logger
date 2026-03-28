# T20 MFCC 060 Notes

## 이번 단계 성격
060은 059의 Recorder 실저장 경로 위에서,
SD_MMC profile 분기 구조와 zero-copy batching 준비 단계를 올린 버전입니다.

## 이번 단계에서 한 일
- SD_MMC profile preset 배열 추가
- profile 이름 기반 적용 helper 추가
- recorder batch buffer / batch flush 경로 추가
- recorder task에서 단건 append 대신 batch push/flush 구조 반영
- backend status에 SD_MMC profile / batch count 출력 추가

## 이번 단계에서 아직 단순화한 것
- 실제 핀 mux / board별 SD_MMC begin 상세 분기
- zero-copy DMA 실제 write 경로
- rotate/prune 정책 고도화
- batch timeout / watermark 정밀 제어
- runtime config JSON과 SD_MMC profile 완전 연동

## 다음 단계 권장
1. 060 실제 컴파일 및 실행 확인
2. runtime config JSON ↔ SD_MMC profile 연동
3. zero-copy / DMA 실제화
4. viewer / selection sync / type-meta 고도화
