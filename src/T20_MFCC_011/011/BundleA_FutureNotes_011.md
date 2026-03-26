# Bundle-A Future Notes

이 묶음은 아래 항목들을 반영한 **설계 확정 + 전체 골격 코드** 단계입니다.

## 이번 단계에서 반영된 핵심
- 전처리 stage 배열화
- sliding window / hop_size 구조
- sample ring buffer 기반 frame assemble 구조
- config snapshot / pipeline snapshot 구조
- vector / sequence 출력 골격

## 다음 단계에서 이어질 예정
- 버튼 기반 측정 시작/종료/이벤트 마커
- session state machine
- SD_MMC 저장 계층
  - raw binary / CSV
  - config JSON
  - metadata / events JSONL / CSV
- AsyncWeb 설정/제어/시각화
- multi-config FFT / mel-bank cache
- zero-copy / DMA / cache-aware 최적화

## 현재 코드 성격
- full skeleton code
- 구조와 책임 분리가 중심
- 실제 하드웨어/라이브러리 세부 튜닝은 다음 단계에서 보완 가능
