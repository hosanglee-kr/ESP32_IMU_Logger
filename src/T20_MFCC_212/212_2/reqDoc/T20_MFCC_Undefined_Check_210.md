# T20 MFCC Undefined Check v210

## 이번 문서의 목적
새로운 상태/함수 추가보다,
기존 구조 위에 실제 코드를 꽂을 때 생길 수 있는 누락을 미리 점검한다.

## 점검 항목

### 1. 선언/정의 불일치
- `Inter_*.h` 선언 존재 여부
- `Core_*.cpp` 또는 적절한 cpp 파일 정의 존재 여부
- Web route가 builder를 실제로 참조하는지 여부

### 2. alias accessor 사용 여부
- direct member access로 회귀하지 않았는지 점검
- 기존 구조체 멤버와 staged 이름의 연결이 유지되는지 점검

### 3. 헤더 안전성
- struct 밖 전역 정의 없는지
- inline/static 사용 위치 문제 없는지
- multiple definition 가능성 없는지

### 4. 실제 연결 시 추가 점검
- SPIClass 객체 접근 가능 여부
- interrupt attach에 필요한 pin/handler 준비 여부
- file flush/close/finalize 호출 대상 유효 여부
- 실패 시 상태 fallback 반영 여부

## 앞으로 실제 연결 단계에서 우선 점검할 항목
1. BMI270 begin 함수 인자/객체
2. burst read 버퍼/길이
3. ISR pin / attachInterrupt handler
4. Recorder file handle / open 상태
5. LittleFS / SD write path 유효성
