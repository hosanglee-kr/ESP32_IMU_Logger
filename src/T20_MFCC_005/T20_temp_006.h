- 중복 제거·책임 분리·상수 일관성 정리

- T20_resetRuntimeResources() 역할 T20_stopTasks,T20_releaseSyncObjects, T20_clearRuntimeState 3개로 구분
- stop()과 reset 정책 명확화 검토
- begin()에서 상태 초기화 코드가 reset 함수와 중복
- T20_validateConfig()는 더 많은 항목 검증 
- T20_sensorTask()에서 drop count 의미 큐가 꽉 차서 옛 프레임을 버린 것도 드롭으로 포함
- printConfig() / printLatest()는 아래 3가지로 분리
   - printConfig() → 샘플링/필터/출력모드
   - printStatus() → dropped_frames, initialized, running, history count
   - printLatest() → 최신 특징값
- 하드코딩 상수화
  - T20_processTask() 내부 지역 배열 상수 13 하드코딩 G_T20_MFCC_COEFFS사용
   - memcpy(... sizeof(float) * 13)도 상수화 
   - T20_pushMfccHistory, delta 함수도 13 하드코딩 제거 추천
   
- ST_Impl 생성자와 reset 함수의 초기화 중복
  -> 생성자에서는 최소한만: 포인터 null, 기본 설정값, T20_resetRuntimeResources(this) 호출
