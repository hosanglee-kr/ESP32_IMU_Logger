# T20 MFCC 016 Notes

## 이번 단계 반영
- 015 기준 실제 컴파일 오류 로그 대응 라운드 3 성격의 구조 보완
- AsyncWeb 제어/다운로드 대묶음 추가
- status/config/latest/start/stop/files/download endpoint 골격 추가
- recorder batching/flush 구조 유지
- 세션 파일 핸들 유지 방식 유지

## 포함 파일
- T20_Mfcc_Def_016.h
- T20_Mfcc_016.h
- T20_Mfcc_Inter_016.h
- T20_Mfcc_Core_016.cpp
- T20_Mfcc_Dsp_016.cpp
- T20_Mfcc_Recorder_016.cpp
- T20_Mfcc_Web_016.h
- T20_Mfcc_Web_016.cpp
- T20_Main_016.h

## 향후 단계 TODO
- AsyncWeb 설정 변경 POST body 실제 반영
- recorder reopen/retry/rotate
- websocket/SSE 실시간 그래프
- snapshot별 FFT / mel_bank cache 분리
- zero-copy / DMA / cache aligned 고도화
