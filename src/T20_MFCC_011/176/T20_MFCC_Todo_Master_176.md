# T20 MFCC Todo Master v176

## 최상위 목표
- 055 기능 수준 복구
- 안정화 구조 유지
- 실센서 / Recorder / Parser / Viewer 축별 마감

## 이번 단계 핵심 수정
- 컴파일 에러의 핵심 원인이었던 누락 식별자 보강
- 누락된 BMI270 / Recorder 상태 상수 보강
- ST_Impl 누락 멤버 보강
- finalize pipeline 매크로명을 `_STATE_` 기준으로 정리
- 다음 단계용 begin/driver + finalize bundle 진입점 추가

## 현재 상태
### 완료
- actual SPI path 준비
- actual read transaction / reg-burst / start-hook-request / exec-apply-session / pipeline / readback / verify / hardware-ISR bridge / board-runtime-pinmap / boot-irq / spiclass-hwexec / live-capture-sample-pipe / driver-session-control 상태 준비
- recorder finalize save/persist/result/exec/commit/write/pipeline/manifest/summary/index/artifact/meta/archive/package/cleanup/export/recover/delivery/final-report/publish/audit 상태 준비
- compile identifier fix 진행

## 진행 중
- 실제 SPIClass begin
- 실제 register/burst read
- 실제 DRDY ISR attach
- 실제 raw decode 고도화
- Recorder finalize 실제 persist/commit/write/manifest/summary/index/artifact/meta/archive/package/cleanup/export/recover/delivery/report/publish/audit 저장 고도화
- Preview parser 강화
