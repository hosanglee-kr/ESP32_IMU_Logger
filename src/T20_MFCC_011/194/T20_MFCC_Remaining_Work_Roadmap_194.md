# T20 MFCC Remaining Work Roadmap v194

## 이번 단계 방향
- SPI begin / actual read / burst prep / burst runtime / attach / ISR queue 흐름을 한 묶음으로 전진
- write path를 commit route / finalize sync로 확장

## 트랙 A. BMI270
현재:
- begin request / spi begin runtime / actual read / register read runtime / burst prep / burst runtime / attach prep / ISR runtime / ISR queue / driver / session control / live path 준비

다음:
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 및 frame build

## 트랙 B. Recorder
현재:
- publish / audit / finalize bundle / store bundle / file bundle / path route / write finalize / commit finalize 준비
다음:
- 실제 파일 저장
- recover / rewrite / cleanup 안정화
