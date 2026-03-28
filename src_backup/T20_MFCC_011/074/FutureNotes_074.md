# T20 MFCC 074 Notes

## 이번 단계 성격
074는 073 recent_frame_ids 빌드 오류를 확실히 수정하고,
preview text 첫 줄 헤더 기반 컬럼 추론의 시작점을 추가한 단계입니다.

## 이번 단계에서 수정한 오류
- `viewer_recent_frame_ids` 멤버가 구조체 선언보다 늦게 반영되거나 누락되어 발생한 빌드 오류 수정
- Inter 구조체 멤버 / 생성자 초기화 / Core 참조를 다시 정합화

## 이번 단계에서 한 일
- preview text 첫 줄을 `header_guess`로 추출
- schema 추론 시 header 존재를 반영
- headered_table 유형 추가
- type meta JSON / 상태 출력에 header_guess 반영

## 다음 단계 권장
1. 074 실제 컴파일 확인
2. 헤더 문자열 기반 컬럼 타입 추론 강화
3. BMI270 실센서 경로 강화
4. selection overlay 시각화 고도화
