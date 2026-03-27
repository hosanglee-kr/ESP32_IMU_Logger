# T20 MFCC 052 Notes

## 이번 단계 반영
- 헤더/소스 세트 간 정의 순서와 구조체 불일치 보정
- 구버전/신버전 혼합 참조를 위한 호환 필드 추가
- 미선언 함수 프로토타입 공용 선언 정리
- 깨진 JSON 키 문자열(`""key""`)을 정상 문자열로 보정

## 이번 단계 의미
- 이번 버전은 기능 추가보다 빌드가 깨지는 핵심 원인인
  '리비전 불일치'를 줄이는 정합성 패치에 초점을 두었다.
- 특히 Def / Inter / Core / Recorder / Web 사이에서
  상수, 구조체 필드, helper 선언을 같은 기준으로 맞추는 작업을 수행했다.

## 향후 단계 TODO
- SD_MMC 보드별 pin/mode/profile 실제 적용
- zero-copy / DMA / cache aligned write 경로 실제화
- selection sync와 waveform/spectrum 구간 연동
- 타입 메타 캐시와 정렬/필터/다운샘플 preview 실제 연계
