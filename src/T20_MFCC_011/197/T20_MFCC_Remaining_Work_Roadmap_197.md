# T20 MFCC Remaining Work Roadmap v197

## 트랙 A. BMI270
현재:
- 실제 연결 직전 단계 대부분 준비 완료
다음:
1. SPIClass.begin 실제 연결
2. burst read 실제 연결
3. DRDY ISR 실제 연결
4. raw -> frame build 실제 연결
5. DSP ingress 연결

## 트랙 B. Recorder
현재:
- 실제 finalize 직전 단계 대부분 준비 완료
다음:
1. flush 실제 호출
2. close 실제 호출
3. finalize 실제 호출
4. meta/report/audit 동기화 실제 연결
5. LittleFS/SD write 실제 연결

## 우선순위
- 다음 단계는 placeholder를 실제 코드로 치환하는 단계
