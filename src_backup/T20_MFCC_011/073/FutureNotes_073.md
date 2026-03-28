# T20 MFCC 073 Notes

## 이번 단계 성격
073은 072의 selection subset overlay / preview text load 위에서,
type preview의 csv/schema 추론 1차와 overlay 시각화 메타 강화를 추가한 단계입니다.

## 이번 단계에서 수정한 오류
- 072에서 `viewer_recent_frame_ids` 선언이 구조체에 완전히 반영되지 않아 발생한 빌드 오류 수정
- Inter 구조체 멤버와 생성자 초기화 구간을 다시 정합화

## 이번 단계에서 한 일
- preview text에 대해 csv/tsv/pipe/plain text 1차 추론 추가
- detected delimiter / schema kind 상태 추가
- overlay_subset_count 추가
- type meta JSON / chart bundle에 schema 추론 결과 반영

## 다음 단계 권장
1. 073 실제 컴파일 확인
2. preview text 첫 줄 헤더 기반 컬럼 추론 강화
3. BMI270 실센서 경로 강화
4. selection overlay 시각화 고도화
