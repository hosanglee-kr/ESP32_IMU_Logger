# T20 MFCC 058 Notes

## 이번 단계 성격
058은 057의 DSP 강화 위에서,
실행 시 실제로 프레임이 생성되고 처리되는 런타임 샘플 경로를 연결한 단계입니다.

## 이번 단계에서 한 일
- synthetic frame 생성 경로 추가
- sensor task / process task를 lightweight runtime loop로 연결
- frame -> MFCC -> delta/delta2 -> vector -> viewer 상태 반영 경로 연결
- 최근 waveform / events / sequence 상태 갱신 연결
- 057 패키지에 섞여 있던 불필요한 056 core 파일 제거

## 여전히 TODO
- BMI270 실센서 경로 연결
- esp-dsp 최적화
- SD_MMC board profile 실제 적용
- zero-copy / DMA / cache aligned write 실제화
- Recorder 실저장 경로 본격 연결
- Viewer / TypeMeta / Selection sync 고도화
