# T20 MFCC Undefined Check v197

## 재점검 항목
- 새 상태 상수 정의 여부
- ST_Impl 멤버 추가 및 생성자 초기화 여부
- 선언/정의 누락 가능성이 큰 setter / prepare / json builder 점검
- Web/UI에서 새 endpoint 참조 이름 점검

## 이번 단계 보강 항목
- bmi270_hw_link_state
- bmi270_frame_build_state
- recorder_meta_sync_state
- recorder_report_sync_state

## 비고
- 이번 단계도 추가/수정시 변수/상수 미정의 가능성을 우선 점검한 뒤 반영
