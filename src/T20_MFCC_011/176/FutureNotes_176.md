# T20 MFCC 176 Notes

## 이번 단계 목적
- 누락된 상수/멤버로 인한 컴파일 에러 수정
- 다음 단계용 묶음 진입점 추가

## 핵심 반영
- READBACK / FINALIZE_PIPELINE / MANIFEST / ARCHIVE 계열 상수 보강
- ST_Impl 누락 멤버 보강
- begin-driver / finalize-bundle 흐름 추가

## 다음 핵심 단계
1. 실제 SPIClass begin 연결
2. 실제 register/burst read
3. 실제 DRDY ISR attach
4. recorder finalize 실제 저장 고도화
