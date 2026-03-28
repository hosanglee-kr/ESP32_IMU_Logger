# T20 MFCC 잔여 구현 정리 및 더 효율적인 진행계획 v075

## 1. 지금 가장 중요한 것
지금은 기능 추가보다 **정합성 안정화**가 먼저입니다.
반복 오류의 대부분은 아래 때문입니다.
- ST_Impl 멤버 누락
- Inter/Core/Web/Recorder 선언 불일치
- 새 필드 추가 후 생성자/JSON/상태 출력 미동기화

---

## 2. 가장 효율적인 진행 방식
앞으로는 기능을 조금씩 여러 축에 동시에 더하지 말고,
**축별 마감형**으로 진행하는 것이 가장 효율적입니다.

### 075-A. 빌드 안정화 전용
대상:
- `T20_Mfcc_Inter_075.h`
- `T20_Mfcc_Core_075.cpp`
- `T20_Mfcc_Web_075.cpp`
- `T20_Mfcc_Recorder_075.cpp`

목표:
- ST_Impl 멤버 표준화
- 프로토타입/정의/호출 일치화
- 반복 빌드 오류 차단

### 075-B. Viewer / TypeMeta / Preview Parser 묶음
대상:
- `T20_Mfcc_Core_075.cpp`
- `T20_Mfcc_Web_075.cpp`
- `littlefs_t20_index.html.txt`
- `littlefs_t20_t20.js.txt`

목표:
- selection overlay / chart bundle / multi-frame 정리
- preview header/delim/schema/column 후보 추론 강화
- type meta / preview metadata 응답 정리

### 075-C. Recorder / Storage 마감
대상:
- `T20_Mfcc_Recorder_075.cpp`
- `T20_Mfcc_Def_075.h`
- `T20_Mfcc_Inter_075.h`

목표:
- batch/index/rotate/fallback 정리
- SD_MMC / LittleFS failover 강화
- binary schema 명확화

### 075-D. BMI270 실센서
대상:
- `T20_Mfcc_Core_075.cpp`
- `T20_Mfcc_Dsp_075.cpp`

목표:
- synthetic → live 전환
- DRDY / frame build / hop_size 실경로 반영

---

## 3. 권장 실제 순서
1. 075-A 빌드 안정화 전용 full sync
2. 075-B Viewer / TypeMeta / Preview Parser full sync
3. 075-C Recorder / Storage 정리
4. 075-D BMI270 실센서 연결

이 순서가 전체 비용이 가장 낮고, 다시 깨질 확률도 가장 낮습니다.
