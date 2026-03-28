# T20 MFCC 069 Notes

## 이번 단계 성격
069는 068의 selection sync 상태 계산 위에서,
viewer chart bundle / multi-frame 응답에 selection sync 결과를 실제 반영한 단계입니다.

## 이번 단계에서 한 일
- selection sync projection 버퍼 추가
- 현재 frame이 selection range 안에 들어오면 waveform 일부를 selection_points로 투영
- viewer_multi_frame JSON에 selection_points 포함
- viewer_chart_bundle JSON에
  - waveform
  - selection_points
  - selection range 정보
  - type meta 정보
  포함
- selection/type apply 후 selection projection 즉시 재계산

## 이번 단계에서 아직 단순화한 것
- 과거 여러 frame에 대한 selection overlay 누적은 아직 아님
- selection sync가 viewer_recent_waveforms 전체를 자르지는 않음
- type meta를 csv/type preview와 깊게 연동하는 것은 후속

## 다음 단계 권장
1. 069 실제 컴파일 및 실행 확인
2. selection overlay를 multi-frame 전체에 확장
3. type meta를 csv/type preview와 연동
4. BMI270 실센서 경로 강화
