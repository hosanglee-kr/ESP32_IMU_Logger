좋습니다.
현재 기준은 016 버전(Compile Patch Round 3 + AsyncWeb 제어/다운로드 골격 반영) 이고, 여기서 남은 작업을 대묶음별 상세 로드맵 + 파일별 수정 대상 + 구현 순서표로 정리하면 아래처럼 가는 게 가장 안정적입니다.


---

T20 MFCC 잔여 구현계획 로드맵

기준 버전: T20_Mfcc_016

전체 방향 요약

지금 상태는 이미 다음 4개 축의 골격이 잡혀 있습니다.

1. 센서 수집 / 특징 추출 파이프라인


2. 세션 상태기계 + 버튼 측정 제어


3. Recorder queue/task + binary header + batching/flush


4. AsyncWeb 상태조회/제어/다운로드 골격



이제 남은 것은 크게 보면 아래 6개 대묶음입니다.

대묶음 H: 실제 컴파일 안정화 라운드

대묶음 I: AsyncWeb 설정 변경 실동작화

대묶음 J: Recorder 안정화 및 파일 운영 고도화

대묶음 K: 실시간 시각화 / 스트리밍

대묶음 L: DSP 멀티설정 대응 및 snapshot 완전 분리

대묶음 M: 성능 최적화(zero-copy, DMA, cache aligned, batching 고도화)



---

대묶음 H

실제 컴파일 안정화 + 인터페이스 정합성 라운드

이 단계는 가장 먼저 해야 합니다.
이유는 이후 기능을 얹어도 헤더/함수 시그니처/구조체 필드 정합성이 흔들리면 계속 되돌아오게 되기 때문입니다.

목표

016 전체 파일 실컴파일 통과

선언/정의 불일치 제거

Inter/Core/Dsp/Recorder/Web 계층 간 함수 시그니처 통일

타입명/버전명/파일명/주석 정리


핵심 작업

T20_Mfcc_Inter_016.h 선언과 각 cpp 구현 함수 시그니처 1:1 대조

ST_Impl 필드명 최종 확정

process_biquad_state, biquad_coeffs, cfg snapshot 사용 경로 정리

AsyncWebServer 조건부 컴파일 분기 실제 빌드 확인

SD_MMC, File, FS include 충돌 여부 점검

button, session state, recorder, web 관련 enum/struct 빠진 부분 보강


파일별 수정 대상

T20_Mfcc_Def_016.h

상수 중 실제 사용 안 하는 항목 정리

naming 일관성 점검

default/max 의미가 겹치는 항목 주석 보강


T20_Mfcc_016.h

public API 최종 확정

attachWebServer() / detachWebServer() / measurementStart() / measurementStop() / pollButton() 시그니처 재점검

공개 API와 내부 구현 연결 주석 정리


T20_Mfcc_Inter_016.h

최우선 수정 대상

ST_Impl 필드 누락/중복/이름 불일치 점검

Core/Dsp/Recorder/Web 함수 선언 전부 최신 시그니처로 통일

File 핸들, web runtime, recorder runtime, filter runtime 필드 확정


T20_Mfcc_Core_016.cpp

Inter 헤더 기준으로 함수 구현 시그니처 맞춤

setConfig() / begin() / start() / stop() / measurementStart() / measurementStop() 흐름 검증


T20_Mfcc_Dsp_016.cpp

const float* / float* 타입 일치

T20_computeMFCC() 인자 구조 확정

filter coeff/state snapshot 기반 경로 확인


T20_Mfcc_Recorder_016.cpp

File 객체 사용 가능 여부, flush() / close() / reopen 흐름 점검

queue item 구조와 write 함수 정합성 확인


T20_Mfcc_Web_016.cpp

ESPAsyncWebServer 설치 여부에 따른 컴파일 분기 확인

lambda에서 캡처/타입 문제 점검



---

대묶음 I

AsyncWeb 설정 변경 실동작화

현재 016은 조회/시작/정지/다운로드 골격은 들어가 있지만, 실제로 중요한 건 설정 변경 POST 적용입니다.

목표

웹에서 설정 변경 가능

설정 JSON GET/POST round-trip 가능

설정 변경 후 setConfig() 반영

필요 시 measurement stop → reconfigure → restart 정책 반영


구현 범위

/config GET: 현재 설정 JSON 반환

/config POST: JSON body 수신 → 검증 → 적용

/measurement/start

/measurement/stop

/status

/latest

/files, /download/*


정책 결정 필요

설정 변경 시 아래 두 가지 중 하나를 택해야 합니다.

정책 A

비측정 상태에서만 설정 변경 허용

안정성 가장 좋음

구현 쉬움


정책 B

측정 중 변경도 허용

내부에서 안전하게 snapshot 갱신

필터 상태/history/sequence 리셋 정책 필요


현재 단계에서는 정책 A 먼저 구현이 좋습니다.

파일별 수정 대상

T20_Mfcc_Web_016.cpp

POST body 수신 처리

JSON 파싱

필드별 validation

적용 결과 JSON 응답


T20_Mfcc_Core_016.cpp

setConfig() 적용 시 reset 범위 확정

측정 중 config 변경 제한 여부 반영


T20_Mfcc_Def_016.h

웹 API 응답 코드/에러 메시지 상수 추가 가능


구현 순서

1. /config GET


2. /config POST


3. validation 실패 응답 포맷 통일


4. 측정 중 POST 차단 또는 safe-restart 정책 추가


5. /status에 config revision / last_apply_result 추가




---

대묶음 J

Recorder 안정화 및 파일 운영 고도화

현재 recorder는 골격과 batching/flush가 있지만, 장시간 운용 기준으로는 아직 부족합니다.

목표

장시간 기록 안정화

파일 reopen/retry

flush 정책 고도화

metadata/event/feature/raw 파일 운영 정리

session 종료 정합성 보장


상세 작업

write 실패 시 retry

세션 종료 시 마지막 flush/close 보장

일정 크기 이상 파일 rotate

세션 시작/종료 이벤트 자동 기록

metadata heartbeat 내용 강화

feature CSV 헤더 추가

raw binary record header/version 정책 고정

JSON config dump 더 상세화


파일별 수정 대상

T20_Mfcc_Recorder_016.cpp

핵심 수정 대상

retry 정책

rotate 정책

마지막 flush 처리

header 재기록 금지/허용 정책

CSV 헤더/JSONL line 포맷 정리


T20_Mfcc_Core_016.cpp

measurementStart/Stop 시 recorder 세션 처리 정교화

버튼 이벤트와 recorder 이벤트 연계


T20_Mfcc_Def_016.h

recorder rotate size

retry count

flush batch threshold

metadata flush interval

feature CSV header on/off 옵션 추가 가능


구현 순서

1. session start/end 이벤트 자동 기록


2. CSV header 작성


3. write 실패 retry


4. rotate


5. flush 정책 튜닝


6. metadata heartbeat 강화




---

대묶음 K

실시간 시각화 / 스트리밍

이 단계는 웹 기능을 “보는 수준”에서 “실시간 관측 수준”으로 올리는 단계입니다.

목표

live waveform

live feature vector

live status

나중에 TinyML inference 결과까지 연결 가능하게 구조 확보


권장 방식

처음부터 WebSocket보다, 먼저 아래 순서가 안전합니다.

1. Polling REST


2. SSE(Server-Sent Events)


3. 필요하면 WebSocket



권장 API

/live/samples

/live/feature

/live/status

/live/sequence

/events/stream (SSE)


파일별 수정 대상

T20_Mfcc_Web_016.cpp

live endpoint 추가

response payload 경량화

JSON 길이 최적화


T20_Mfcc_Inter_016.h

live export용 임시 buffer/state 필요 시 추가


T20_Mfcc_Core_016.cpp

sample ring export 함수 추가

latest sequence export 최적화


구현 순서

1. /live/status


2. /live/feature


3. /live/samples


4. /live/sequence


5. SSE 또는 WebSocket




---

대묶음 L

DSP 멀티설정 대응 + snapshot 완전 분리

이건 구조적으로 가장 중요합니다.
사용자가 이미 여러 번 지적한 핵심이기도 합니다.

목표

processing 중 config 변경이 들어와도 현재 frame은 cfg snapshot 기준으로 끝까지 처리

filter coeff/state도 snapshot 기준 분리

FFT / mel_bank 도 snapshot/derived cache 기준 분리

multi-config 대응 기반 마련


지금 남은 핵심 이슈

현재는 상당 부분 snapshot 방향으로 갔지만, 아직 아래가 남아 있습니다.

FFT window

mel_bank

noise_spectrum

history

pre-emphasis prev sample

filter state

sequence feature_dim


이런 항목들이 global runtime shared state와 frame-local snapshot state 사이에서 완전히 정리되지 않았습니다.

권장 구조

1. Config snapshot

현재 설정 복사본


2. Derived DSP snapshot

해당 설정에서 파생된

filter coeffs

hamming window

mel_bank

feature_dim



3. Runtime state

noise learning state

pre-emphasis prev sample

filter runtime state

mfcc history

sequence state


여기서도 다시 두 갈래가 있습니다.

단순형

설정 바뀌면 runtime state reset

고급형

설정별 state bank 유지

지금은 단순형 먼저 완성이 좋습니다.

파일별 수정 대상

T20_Mfcc_Inter_016.h

ST_T20_DspDerived_t 같은 내부 구조 추가 추천

ST_T20_RuntimeState_t 분리 추천


T20_Mfcc_Dsp_016.cpp

buildWindowFromConfig()

buildMelFilterbankFromConfig()

makeDerivedDspFromConfig()

computeMFCC(snapshot, derived, runtime) 형태로 정리


T20_Mfcc_Core_016.cpp

setConfig()에서 derived rebuild + runtime reset 정책 반영


구현 순서

1. derived 구조체 분리


2. filter coeff/state 분리


3. window 분리


4. mel_bank 분리


5. history/runtime reset 정책 확정


6. multi-config cache 여부 검토




---

대묶음 M

성능 최적화(zero-copy, DMA, cache aligned, batching 고도화)

이 단계는 기능이 어느 정도 고정된 뒤 들어가는 게 맞습니다.

목표

memcpy 감소

frame pipeline 지연 감소

queue overhead 감소

recorder throughput 향상

웹 응답 부하 최소화


주요 항목

zero-copy pipeline

sensor buffer → process input 직접 참조

가능한 구간에서 memcpy 제거


DMA + cache aligned

raw frame / fft buffer / recorder raw queue payload 정렬 최적화

SPI / SD_MMC / sensor sampling 구간의 캐시 영향 검토


sliding window / hop_size 최적화

overlap 있는 경우 full copy 대신 ring view 기반 처리 검토


recorder batching 고도화

queue drain N건

raw binary multi-record append

flush interval + batch threshold 결합


JSON 응답 최적화

latest full JSON vs compact JSON 모드 분리

웹 대역폭 절약


파일별 수정 대상

T20_Mfcc_Core_016.cpp

raw frame queue 전달 구조 개선

sliding window memory movement 감소


T20_Mfcc_Dsp_016.cpp

temp/work buffer 복사 최소화

FFT input path 단순화


T20_Mfcc_Recorder_016.cpp

batch write

binary append 최적화

flush 조건 튜닝


T20_Mfcc_Web_016.cpp

compact JSON

sampling decimation endpoint



---

파일별 수정 우선순위 표

1순위

T20_Mfcc_Inter_016.h

이유:

모든 cpp의 기준점

필드/함수 선언 정합성 문제의 근원


T20_Mfcc_Core_016.cpp

이유:

begin/start/stop/session/measurement/web/recorder를 모두 묶는 중심축


T20_Mfcc_Dsp_016.cpp

이유:

snapshot, filter, MFCC, mel_bank, performance 문제의 핵심


2순위

T20_Mfcc_Recorder_016.cpp

이유:

장시간 운용 안정성 핵심


T20_Mfcc_Web_016.cpp

이유:

사용자 제어/UI 체감 기능 핵심


3순위

T20_Mfcc_Def_016.h

이유:

상수/기본정책/주석 기준점


T20_Mfcc_016.h

이유:

public API 고정 시점에 정리


T20_Main_016.h

이유:

예제/통합 진입점이므로 마지막에 정리해도 됨



---

구현 순서표

0단계

기준선 정리

016 전체 컴파일 기준선 확보

헤더/시그니처 정합화


대상 파일:

Inter

Core

Dsp



---

1단계

대묶음 H

실제 컴파일 오류 라운드

필드명/함수명/시그니처 정리



---

2단계

대묶음 I

AsyncWeb 설정 변경 POST 실동작화

/config GET/POST, /status, /latest, /measurement/start, /measurement/stop


대상 파일:

Web

Core



---

3단계

대묶음 J

recorder retry/reopen/flush/rotate

metadata heartbeat 강화

config dump 강화


대상 파일:

Recorder

Core

Def



---

4단계

대묶음 L

DSP snapshot 완전 분리

filter/window/mel_bank/feature_dim derived 구조화

config 변경 시 runtime reset 정책 확정


대상 파일:

Inter

Dsp

Core



---

5단계

대묶음 K

live samples / feature / status / sequence

SSE 또는 WebSocket 초안


대상 파일:

Web

Core



---

6단계

대묶음 M

zero-copy

DMA/cache aligned

batch write 고도화

hop_size overlap 최적화


대상 파일:

Core

Dsp

Recorder



---

가장 추천하는 다음 실작업 순서

지금 바로 이어서 구현한다면 아래 순서가 가장 좋습니다.

A안: 안정성 우선

1. 016 컴파일 오류 라운드 4


2. AsyncWeb POST 설정 변경


3. Recorder retry/rotate


4. DSP snapshot 완전 분리


5. live API


6. zero-copy 최적화



B안: 사용자 체감 우선

1. 016 컴파일 오류 라운드 4


2. AsyncWeb POST + 다운로드 정리


3. live latest/status API


4. Recorder 안정화


5. DSP snapshot 완전 분리


6. 성능 최적화



현재 흐름상은 A안이 더 맞습니다.


---

한 줄 결론

지금 남은 핵심은
“컴파일 정합화 → 웹 설정 실동작 → recorder 안정화 → DSP snapshot 완전 분리 → 실시간 시각화 → 성능 최적화”
이 순서로 가는 것입니다.

원하시면 다음 답변에서 바로
“대묶음 H 착수용 체크리스트 + 파일별 수정 함수 목록”
형태로 더 잘게 쪼개서 드리겠습니다.
