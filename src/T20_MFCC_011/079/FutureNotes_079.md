# T20 MFCC 079 Notes

## 이번 단계 성격
079는 078-C에 해당하는 Recorder / Storage 전용 라운드입니다.

## 이번 단계에서 한 일
- recorder active path 개념 추가
- SD_MMC mount 실패 시 LittleFS fallback 경로 추가
- rotate keep max 상태 추가
- recorder rotate index trim helper 추가
- recorder storage 상태 JSON 추가
- Web에 recorder_storage / recorder_rotate_apply endpoint 추가
- LittleFS UI에 Recorder Storage 버튼 추가

## 효과
- 저장 경로 상태를 한 번에 보기 쉬워짐
- SD_MMC 실패 시 fallback 흐름이 더 명확해짐
- 다음 BMI270 실센서 라운드로 넘어가기 좋은 상태가 됨
