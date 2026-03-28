# T20 MFCC 066 Notes

## 이번 단계 성격
066은 065의 SD_MMC profile 정밀화 위에서,
viewer / selection sync / type-meta 고도화를 시작한 단계입니다.

## 이번 단계에서 한 일
- selection sync 상태값 추가
- type meta 상태값 추가
- runtime config JSON에 selection sync / type meta 반영
- selection sync JSON 빌더 추가
- type meta JSON 빌더 추가
- Web API에 selection_sync / type_meta 조회/적용 endpoint 추가
- 간단한 LittleFS 정적 UI 버튼 추가

## 이번 단계에서 아직 단순화한 것
- selection sync 실제 렌더 시리즈 동기화는 후속
- type meta 추론/자동 분류는 후속
- viewer multi-series / overlay 시각화는 후속
- 실센서 BMI270 경로 고도화는 후속

## 다음 단계 권장
1. 066 실제 컴파일 및 실행 확인
2. selection sync 렌더 범위 동기화 실제 처리
3. type meta 자동 생성/분류 고도화
4. BMI270 실센서 경로 강화
