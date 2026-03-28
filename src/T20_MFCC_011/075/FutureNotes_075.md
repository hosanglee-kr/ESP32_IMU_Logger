# T20 MFCC 075 Notes

## 이번 단계 성격
075는 074의 `type_preview_header_guess`, `viewer_recent_frame_ids` 관련 정합성 문제를 다시 맞추고,
효율적인 진행 순서를 문서화한 단계입니다.

## 이번 단계에서 수정한 오류
- `viewer_recent_frame_ids` 구조체 멤버 누락/초기화 불일치 재수정
- `type_preview_header_guess` 구조체 멤버 누락/초기화 불일치 재수정
- selection/type preview 관련 프로토타입 재정렬

## 다음 권장
- 075-A: 빌드 안정화 전용 full sync
- 075-B: Viewer / TypeMeta / Preview Parser 전용 full sync
