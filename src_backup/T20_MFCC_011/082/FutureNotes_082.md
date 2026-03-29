# T20 MFCC 082 Notes

## 이번 단계 성격
082는 잔여 진행 계획을 다시 정리하면서,
055 대비 손실이 컸던 Recorder 세션 기능을 1차 복구한 버전입니다.

## 이번 단계에서 한 일
- 진행 계획을 md 파일로 재정리
- `T20_recorderBegin()` 추가
- `T20_recorderEnd()` 추가
- `T20_recorderOpenSession()` 추가
- `T20_recorderCloseSession()` 추가
- `T20_recorderWriteEvent()` 추가
- `T20_recorderWriteMetadataHeartbeat()` 추가
- `T20_buildRecorderSessionJsonText()` 추가
- Web에 `/recorder_session`, `/recorder_begin`, `/recorder_end` 추가
- UI에 `Recorder Session` 버튼 추가

## 향후 단계 구현 예정 사항
- BMI270 실제 begin/init
- DRDY 기반 샘플 수집
- 실제 raw sample -> frame build
- hop_size 실경로 반영
- raw/event/feature/config/metadata 분리 저장
- binary finalize/recover 강화
- preview parser 컬럼 타입 추론 강화
