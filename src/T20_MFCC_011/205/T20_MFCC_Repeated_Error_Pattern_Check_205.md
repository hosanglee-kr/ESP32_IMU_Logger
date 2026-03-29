# T20 MFCC Repeated Error Pattern Check v205

## 매 단계 점검 항목
1. include 이전 ST_Impl 사용 금지
2. ST_Impl에 없는 staged 멤버 직접 접근 금지
3. alias accessor 우선 사용
4. setter / prepare / json builder 선언·정의 누락 점검
5. Web endpoint 추가 시 builder 존재 여부 점검

## 이번 단계 조치
- apply pipeline bundle 확장도 alias 기반 유지
- 신규 builder도 alias accessor만 사용
