# T20 MFCC 056 Notes

## 이번 단계 성격
이번 056은 **기능 확장판 위에 패치를 누적하는 방식**에서 벗어나,
**컴파일 복구를 최우선으로 다시 맞춘 기준 세트**입니다.

즉,
- Def / Public / Inter / Core / DSP / Recorder / Web
를 **한 번에 다시 맞춘 단일 리비전 베이스라인**입니다.

## 이번 단계에서 한 일
- 중복 매크로/중복 typedef/중복 struct 제거
- 구조체 필드와 cpp 사용 코드를 같은 기준으로 재정렬
- 누락된 helper 선언/정의 최소 세트 복구
- Web 라우팅을 단순한 정적 구조로 재작성
- Recorder helper 중복 제거
- Viewer / CSV / TypeMeta / Sync API를 컴파일 가능한 skeleton으로 정리

## 이번 단계에서 의도적으로 단순화한 것
- 실제 BMI270 고속 수집
- esp-dsp 기반 FFT 최적화
- SD_MMC board profile 실제 적용
- zero-copy / DMA / cache aligned write 경로
- selection sync와 waveform/spectrum 구간 연동
- CSV 고급 파싱 / schema / type-meta 캐시 고도화

위 항목들은 **TODO로 유지**했습니다.

## 다음 단계 권장 순서
1. 이 056 기준으로 실제 컴파일
2. 남는 오류 로그를 기준으로 환경/라이브러리 차이 정리
3. 그 다음 DSP 실제화
4. 그 다음 Recorder/SD_MMC 실제화
5. 마지막으로 Web/Viewer 고도화
