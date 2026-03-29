# T20 MFCC 083 Notes

## 이번 단계 성격
083은 진행 계획을 다시 정리하면서,
BMI270 실센서 라운드 본체 진입 구조를 한 단계 더 구체화한 버전입니다.

## 이번 단계에서 한 일
- `T20_initBMI270_SPI()` 추가
- `T20_configBMI270_1600Hz_DRDY()` 추가
- `T20_readBMI270Sample()` 추가
- live source axis mode 반영
- live debug JSON에 BMI270 상태 추가

## 향후 단계 구현 예정 사항
- 실제 BMI270 begin/init 세부화
- 실제 raw sample read
- DRDY ISR/queue 수집
- hop_size 반영된 frame build 마감
- raw/event/feature/config/metadata 분리 저장
- binary finalize/recover 강화
- preview parser 컬럼 타입 추론 강화
