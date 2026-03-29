# T20 MFCC Remaining Work Roadmap v176

## 이번 단계 방향
- 컴파일 오류를 먼저 안정화
- 누락된 상태 상수/멤버를 보강
- 그 위에 다음 단계용 begin/driver + finalize bundle 묶음을 추가

## 트랙 A. BMI270
현재:
- 정의 누락 보강 완료
- begin request / driver / session control / live 흐름 준비

다음:
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 고도화

## 트랙 B. Recorder
현재:
- 정의 누락 보강 완료
- finalize bundle / publish / audit / delivery 준비

다음:
- 실제 저장 고도화
- recover/rewrite 안정화
