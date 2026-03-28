# T20 MFCC 072 Notes

## 이번 단계 성격
072는 071의 평균 overlay / type preview 메타 위에서,
selection range 기반 recent frame subset overlay와 type preview text 로드 경로를 추가한 단계입니다.

## 이번 단계에서 한 일
- recent waveform별 frame_id 추적 추가
- selection range에 포함되는 recent frame subset만 overlay 평균에 반영
- type preview text buffer 추가
- LittleFS preview text 로드 helper 추가
- Web API에 type_preview_load endpoint 추가
- chart bundle에 recent_frame_ids 노출

## 이번 단계에서 아직 단순화한 것
- csv/type preview 실제 구조화 파서는 아직 기본 텍스트 로드 수준
- overlay는 waveform 평균 방식 유지
- preview text에서 schema/type 자동 추론 강화는 후속

## 다음 단계 권장
1. 072 실제 컴파일 및 실행 확인
2. type preview 실제 csv/schema 추론 강화
3. BMI270 실센서 경로 강화
4. selection overlay 시각화 고도화
