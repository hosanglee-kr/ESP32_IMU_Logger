# T20 MFCC 055 대비 081 누락/축소 점검 및 효율적 구현계획

## 1. 결론

081은 055 이후에 새로 붙은 구조도 있지만, **055에서 이미 있던 기능 중 상당수가 081에서 축소되거나 빠진 상태**입니다.  
특히 **Web/UI, Recorder 세션 관리, CSV/Preview 고급 기능, DSP 전처리 파이프라인, 버튼 기반 측정 제어**가 많이 줄었습니다.

즉, 지금은 단순히 다음 기능을 더 붙이기보다:

1. **055 기능을 기준선으로 삼아**
2. **081의 구조 안정화 위에**
3. **빠진 기능을 축별로 복구**

하는 방식이 가장 효율적입니다.

---

## 2. 정량 비교

### 파일 크기 기준
- `T20_Mfcc_Core`: 055 = 125,739 bytes / 081 = 72,509 bytes
- `T20_Mfcc_Inter`: 055 = 37,069 bytes / 081 = 25,809 bytes
- `T20_Mfcc_Recorder`: 055 = 34,623 bytes / 081 = 18,422 bytes
- `T20_Mfcc_Dsp`: 055 = 20,084 bytes / 081 = 8,851 bytes
- `littlefs js`: 055 = 39,727 bytes / 081 = 1,703 bytes

### HTML 버튼/요소 수
- 055 HTML id 개수: 69
- 081 HTML id 개수: 13

081은 구조 정리 과정에서 기능이 많이 단순화된 것으로 보는 게 맞습니다.

---

## 3. 055 대비 081에서 누락/축소된 핵심 기능

## A. DSP / 전처리 / 라이브 파이프라인
대표 누락 함수:
- T20_applyBiquadFilter
- T20_applyPreprocessPipeline
- T20_applyStage
- T20_applyWindow
- T20_buildMelFilterbank
- T20_buildPipelineSnapshot
- T20_buildRecorderCsvTableColumnFilteredJsonText
- T20_buildRecorderCsvTableFilteredJsonText
- T20_configBMI270_1600Hz_DRDY
- T20_configureFilter
- T20_copyFrameFromRing
- T20_dspCacheFindMelBank
- T20_dspCacheStoreMelBank
- T20_initBMI270_SPI
- T20_makeBiquadCoeffsFromStage
- T20_matchCsvColumnFiltersAdvanced
- T20_matchCsvColumnFiltersCore
- T20_pushSlidingSample
- T20_recorderEnqueueRawFrame
- T20_recorderWriteRawFrame

핵심 의미:
- 전처리 stage 적용
- biquad/filter/window/mel filterbank
- sliding sample / frame ring / ready frame enqueue
- BMI270 초기화/1600Hz DRDY 설정
- pipeline snapshot

판단:
- 081의 live source는 **skeleton 수준**
- 055 쪽은 **실제 MFCC 파이프라인 기능이 더 풍부**

우선순위:
- 매우 높음

---

## B. Recorder / Storage
대표 누락 함수:
- T20_createRecorderObjects
- T20_decodeRecorderBinaryHeaderStrict
- T20_drainRecorderQueue
- T20_enqueueRecorderVector
- T20_flushRecorderBatchMessages
- T20_flushRecorderVectorBatch
- T20_getRecorderBinaryVectorRecordBytes
- T20_recorderBegin
- T20_recorderCloseSession
- T20_recorderEnd
- T20_recorderEnqueueEvent
- T20_recorderEnqueueFeature
- T20_recorderOpenSession
- T20_recorderReopenSessionFiles
- T20_recorderTryRecover
- T20_recorderWriteBinaryHeader
- T20_recorderWriteConfig
- T20_recorderWriteEvent
- T20_recorderWriteFeature
- T20_recorderWriteMetadataHeartbeat
- T20_releaseRecorderObjects
- T20_rewriteRecorderBinaryHeader
- T20_tryFinalizeRecorderBinarySession
- T20_tryOpenRecorderBinarySession
- T20_writeBinaryRawRecordToOpenFile

핵심 의미:
- recorder begin/end/session open/close
- raw/event/feature/config/metadata write
- binary header rewrite/finalize/recover
- queue/object lifecycle
- rotate list push
- vector/raw frame enqueue

판단:
- 081은 storage 방향은 정리됐지만
- 055의 **세션형 recorder 기능**이 많이 빠졌음

우선순위:
- 매우 높음

---

## C. CSV / JSON / Preview Parser
대표 누락 함수:
- T20_applyConfigJsonAdvanced
- T20_compareCsvCellAdvanced
- T20_findJsonKeyPos
- T20_guessCsvCellType
- T20_jsonFindBool
- T20_jsonFindFloat
- T20_jsonFindInt
- T20_jsonFindSection
- T20_jsonFindString

핵심 의미:
- advanced config apply
- csv 셀 타입 추론
- json section/key 탐색
- 비교/정렬/필터링 고급 로직

판단:
- 081 preview parser는 기초적인 schema/delim/header 수준
- 055는 **CSV/JSON 쪽 고급 기능이 더 많음**

우선순위:
- 높음

---

## D. Viewer / Selection
대표 누락 함수:
- T20_buildViewerSequenceOverviewJsonText
- T20_pushViewerEvent
- T20_pushViewerWaveformHistory

핵심 의미:
- viewer event push
- waveform history push
- sequence overview 빌더

판단:
- 081은 unified bundle을 만들었지만
- 055의 **viewer history/event 계열**은 오히려 축소됨

우선순위:
- 중간~높음

---

## E. Measurement / 버튼 제어
대표 누락 함수:
- T20_measurementStart
- T20_measurementStop
- T20_pollButtonEvent

판단:
- 055에는 측정 시작/정지/버튼 이벤트 폴링 개념이 있었는데
- 081에서는 거의 사라짐

우선순위:
- 중간

---

## F. Web / UI / Push
대표 누락 함수:
- T20_rotateListPush
- T20_webAttach
- T20_webAttachStaticFiles
- T20_webDetach
- T20_webPeriodicPush
- T20_webPushLiveNow

판단:
- 055는 Web attach/static/live push 흐름이 더 풍부
- 081은 endpoint는 일부 늘었지만, **실시간 push/정적 UI 풍부함은 감소**

우선순위:
- 중간

---

## 4. UI 관점에서 특히 많이 줄어든 부분

055 HTML에는 아래 같은 기능이 직접 노출되어 있었습니다.
- Start / Stop
- Recorder Index
- Config Export / Manifest
- Viewer Waveform / Spectrum / Events / Sequence / Overview / Multi-frame
- Recorder Preview / Parsed Preview / Range
- Chart View / Zoom / Pan
- Binary Header / Binary Records / Payload Schema
- CSV Table / Filtered / Column Filtered / Advanced / Type Meta / Schema
- Render Selection Sync

반면 081은 UI가 대략 아래 수준으로 축소되었습니다.
- Status
- Config
- Viewer Data
- Manifest
- Sync
- Type Meta / Selection Sync
- Unified Viewer Bundle
- Recorder Storage
- Live Source / Live Debug

즉, **081은 백엔드 구조를 정리하는 대신, 055의 풍부한 UI/검사용 도구를 많이 잃은 상태**입니다.

---

## 5. 가장 효율적인 구현계획

지금 가장 비효율적인 방식:
- 081 위에 새 기능을 계속 덧붙이기
- 빌드가 깨질 때마다 단건 패치
- 055 기능 복구 없이 새 skeleton만 계속 확장

가장 효율적인 방식:
- **055를 기능 기준선**
- **081을 구조 기준선**
- **기능 축별 복구형 통합**

---

## 6. 권장 복구 순서

## 1단계: Recorder 세션 기능 복구
이유:
- 손실 폭이 가장 크고, 실사용 영향도 큼
- storage/fallback/rotate와 연결됨

복구 우선 대상:
- `T20_recorderBegin`
- `T20_recorderEnd`
- `T20_recorderOpenSession`
- `T20_recorderCloseSession`
- `T20_recorderWriteBinaryHeader`
- `T20_recorderWriteRawFrame`
- `T20_recorderWriteFeature`
- `T20_recorderWriteEvent`
- `T20_recorderWriteMetadataHeartbeat`
- `T20_tryFinalizeRecorderBinarySession`
- `T20_tryOpenRecorderBinarySession`
- `T20_recorderTryRecover`

---

## 2단계: DSP / BMI270 실제 경로 복구
이유:
- 지금 live source는 skeleton이라 실질 기능 부족
- 055 쪽의 frame/sliding/pipeline 기능을 되살리는 게 효과 큼

복구 우선 대상:
- `T20_initBMI270_SPI`
- `T20_configBMI270_1600Hz_DRDY`
- `T20_pushSlidingSample`
- `T20_copyFrameFromRing`
- `T20_tryEnqueueReadyFrames`
- `T20_applyPreprocessPipeline`
- `T20_applyStage`
- `T20_applyWindow`
- `T20_buildMelFilterbank`

---

## 3단계: Preview Parser / CSV 고급 기능 복구
이유:
- 081에서 parser는 기초 기능만 있음
- 055의 CSV/filter/schema 기능을 복구하면 진단력이 크게 올라감

복구 우선 대상:
- `T20_guessCsvCellType`
- `T20_compareCsvCellAdvanced`
- `T20_matchCsvColumnFiltersAdvanced`
- `T20_splitCsvColumnFilters`
- `T20_splitCsvColumnFiltersAdv`
- `T20_buildRecorderCsvTableFilteredJsonText`
- `T20_buildRecorderCsvTableColumnFilteredJsonText`

---

## 4단계: Viewer / Event / History 복구
이유:
- unified bundle은 좋지만,
- 055의 상세 viewer 기능이 사라져 디버깅이 약해짐

복구 우선 대상:
- `T20_pushViewerEvent`
- `T20_pushViewerWaveformHistory`
- `T20_buildViewerSequenceOverviewJsonText`

---

## 5단계: UI 복구
이유:
- 마지막에 붙여야 백엔드와 안 엇갈림
- 055의 풍부한 버튼/패널을 선택적으로 재도입

복구 우선 대상:
- waveform/spectrum/events/sequence/overview/multiframe
- recorder preview/range/header/binary records
- csv advanced / type meta / schema
- start/stop / live push

---

## 7. 구현 묶음 제안

### 082-A
**Recorder 세션 기능 복구 묶음**
- recorder session lifecycle
- binary/raw/feature/event write
- recover/finalize

### 082-B
**BMI270 + 전처리 파이프라인 복구 묶음**
- BMI270 init/DRDY
- sliding/ring/frame build
- preprocess pipeline

### 082-C
**Preview Parser / CSV 고급 기능 복구 묶음**
- csv schema/type/filter/advanced compare

### 082-D
**Viewer/Event/History + UI 복구 묶음**
- viewer event/history
- detailed endpoints
- 선택적 UI 재도입

---

## 8. 최종 권장

가장 효율적인 다음 한 걸음은:

**081 → 082에서 “Recorder 세션 기능 복구 묶음”부터 시작**

이유:
- 055 대비 손실이 가장 큼
- 나머지 기능과 연결점이 많음
- 복구 효과가 가장 큼
