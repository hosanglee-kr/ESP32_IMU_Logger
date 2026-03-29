# T20 MFCC Repeated Error Pattern Check v210

## 반복 오류 패턴 점검 항목

1. include 이전에 `CL_T20_Mfcc::ST_Impl` 사용 금지  
2. `ST_Impl`에 없는 staged 멤버 직접 접근 금지  
3. alias accessor 우선 사용  
4. 헤더에 struct 밖 전역 정의 삽입 금지  
5. multiple definition 링크 에러 가능성 점검  
6. Web endpoint 추가 시 builder 존재 여부 점검  
7. 새 prepare/builder 추가 시 실제 호출 체인 연결 여부 점검  
8. 상태 이름만 늘리고 실제 연결은 비어 있는 상황인지 점검  

## 특히 주의할 재발 패턴

### 패턴 A. 헤더에 stray global definition 생성
- struct 멤버 줄이 struct 밖으로 삽입되면 링크 단계에서 multiple definition 발생

### 패턴 B. staged 이름은 추가했지만 ST_Impl 실멤버는 없음
- direct member access 사용 시 즉시 컴파일 에러 발생

### 패턴 C. builder는 추가했지만 Web route와 연결 안 됨
- UI 버튼만 있고 실제 응답이 비어 있을 수 있음

### 패턴 D. prepare 체인만 늘고 실제 HW/FS 연결 없음
- 구조만 커지고 진척이 없는 상태가 되므로 앞으로는 실제 코드 치환 우선

## v210부터의 추가 원칙
- 새 상태명 추가는 최소화
- 실제 begin/burst/isr/flush/close/finalize 코드 치환을 우선
- 문서도 상태 나열보다 **실제 연결 여부** 중심으로 갱신
