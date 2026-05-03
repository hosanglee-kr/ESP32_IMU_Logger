# SMEA-100 (Super Micro Edge AI) 상세 설계 사양서 (v1.0)
본 문서는 ESP32-S3 기반 산업용 예지보전 엣지 AI 디바이스인 **SMEA-100**의 백엔드(C++ 코어) 및 프론트엔드(Web UI) 시스템 전반에 대한 상세 설계 및 구현 규격서입니다. 본 문서를 기준으로 즉각적인 개발 및 유지보수가 가능하도록 모든 아키텍처 원칙, 방어 로직, API 규격을 망라하여 기술합니다.
## 1. 시스템 코어 아키텍처 및 구현 원칙
### 1.1 하드웨어 및 메모리 규격
 * **MCU:** ESP32-S3 (Dual Core, PSRAM 장착 필수)
 * **SIMD 가속 최적화:** ESP-DSP 라이브러리의 128-bit SIMD 연산 효율을 극대화하기 위해, 오디오 파형 및 텐서를 다루는 모든 버퍼와 필터 계수 배열은 반드시 alignas(16)으로 16-Byte 메모리 정렬을 강제합니다.
 * **메모리 오염 및 OOM 방어:** * 동적 할당(malloc, new)은 초기화(init()) 시점에 heap_caps_aligned_alloc(16, ..., MALLOC_CAP_SPIRAM)을 사용하여 한 번만 수행하며, 메인 루프 내에서의 런타임 동적 할당은 전면 금지합니다.
   * 모든 스택(Stack) 버퍼(예: Median Filter Window)는 런타임 설정값이 아닌 정적 상수(MAX_..._CONST) 크기로 선언하여 스택 오버플로우를 원천 차단합니다.
### 1.2 네이밍 컨벤션 (Naming Convention)
 * **클래스 멤버 변수:** 언더스코어 접두사 (_systemState, _lock)
 * **메서드 매개변수 (Parameter):** p_ 접두사 (p_slotIdx, p_outBuffer)
 * **로컬 변수 (Local Variable):** v_ 접두사 (v_cfg, v_sum)
 * **정적 상수 (Static Const):** 대문자 스네이크 케이스 + _CONST 접미사 (FFT_SIZE_CONST)
 * **동적 기본값 (Dynamic Default):** 대문자 스네이크 케이스 + _DEF 접미사 (HOP_MS_DEF)
## 2. 모듈별 상세 설계 규격
### 2.1 설정 관리 모듈 (T410_Config, T415_ConfigMgr)
 * **T410_Config (정적 상수):** 하드웨어 핀맵, 메모리 상한선, 단위 변환 상수 등 런타임에 절대 변하지 않는 _CONST 상수들의 집합입니다.
 * **T415_ConfigMgr (동적 설정 매니저):** * **역할:** 웹 API를 통해 수신된 JSON 설정을 파싱하고, 플래시 메모리(LittleFS)의 /sys/config.json에 저장/로드하는 Singleton 클래스입니다.
   * **명시적 구조체:** ST_Config_Dsp, ST_Config_Feature 등 6개의 개별 구조체를 조합한 DynamicConfig를 사용합니다.
   * **방어 로직 1 (부분 업데이트):** updateFromJson() 메서드를 통해 수신된 JSON 문자열을 기존 메모리 구조체에 병합(Merge)한 뒤 저장합니다.
   * **방어 로직 2 (정전 벽돌화 방지):** .tmp 확장자로 먼저 저장한 후, 성공 시 기존 파일을 삭제하고 rename하는 **원자적 쓰기(Atomic Write)**를 수행합니다.
### 2.2 신호 수집 및 전처리 엔진 (T480_MicEng, T430_DspEng)
 * **T480_MicEngine (I2S DMA 수집):** * ICS43434 2채널 마이크로부터 42kHz, 32bit PCM 데이터를 I2S DMA로 수집합니다.
   * FSM 상태와 동기화되어 pause(), resume() 시 클럭을 제어하며, 재가동 시 clearBuffer()로 과거 쓰레기 데이터를 강제 소각합니다.
   * 수집된 32비트 정수를 1.0f / 2147483648.0f 스케일링을 통해 Float으로 정규화 및 L/R 채널 De-interleave를 수행합니다.
 * **T430_DspEngine (파이프라인):** * **순서:** Median Filter -> DC Removal -> FIR HPF -> FIR LPF -> IIR Notch(60Hz/120Hz) -> Noise Gate -> Pre-emphasis -> Beamforming.
   * **핑퐁 버퍼 방어:** In-place 연산으로 인한 메모리 오염을 막기 위해 _workBufA와 _workBufB를 교차(Ping-pong) 참조하며 필터링을 수행합니다.
### 2.3 특징량 추출 및 텐서 조립 (T440_FeatExtra, T445_SeqBd)
 * **T440_FeatureExtractor:** * **주파수 도메인:** 1024 FFT 후 Power Spectrum 도출 (ANC 스펙트럼 감산 포함).
   * **MFCC 39D:** Mel Filterbank (2595/700 스케일) -> DCT-II -> N=2 기반 Delta 및 Delta-Delta 연산 (5프레임 히스토리 링버퍼 유지).
   * **Cepstrum:** 4개의 동적 주파수 타겟(Hz)에 대한 최대 Quefrency 진폭 및 RMS 비율 도출 (공간 분석 버퍼 스크래치 재사용으로 OOM 방어).
   * **기타 Feature:** Band RMS, N-Top Peaks(5개), Spectral Centroid, STA/LTA Ratio(충격음 비율), Kurtosis, Phase IPD & Coherence.
 * **T445_SequenceBuilder:** * TinyML 모델 추론을 위해 MAX_SEQUENCE_FRAMES_CONST x MFCC_TOTAL_DIM_CONST 크기의 2D 링버퍼를 관리합니다.
   * 모델 입력용 1D 플랫 배열을 추출할 때 CPU 병목이 없도록 O(1) 포인터 연산 및 memcpy 직렬화를 수행합니다.
### 2.4 시스템 오케스트레이터 (T450_FsmMgr)
 * **멀티 코어 태스크:**
   * **Core 0 (Capture Task):** I2S I/O 및 Hop Size 기반 슬라이딩 오버랩 연산 전담.
   * **Core 1 (Processing Task):** DSP 필터링, 특징 추출, ML 조립, 스토리지 로깅, FSM 판정 전담.
 * **이중 큐 핑퐁 아키텍처:** 데이터 드랍을 막기 위해 크기 100(FEATURE_POOL_SIZE_CONST)의 _qFreeSlotIdx와 _qReadySlotIdx를 사용하여 Zero-Copy로 인덱스만 교환합니다. (에러 발생 시 인덱스 증발 방지를 위한 롤백 로직 탑재)
 * **하이브리드 판정 (Decision):** * 하드웨어 단선 (Test NG) -> Rule 기반 임계치 판정 (Energy, StdDev, STA/LTA) -> (추후 TFLite ML 연동).
### 2.5 스토리지 및 로깅 엔진 (T460_Storage)
 * **듀얼 로깅:** 특징량 .bin 파일(SD_MMC 직결) 및 원본 파형 .pcm 파일(내부 SRAM 바운스 버퍼 경유 기록)을 동시 생성합니다.
 * **Pre-trigger 링버퍼:** 런타임 설정된 시간(예: 3초)만큼 PSRAM에 과거 데이터를 보존하다가, 이벤트 발생(Session Open) 시 즉각 SD카드로 플러시합니다.
 * **안전 로테이션:** 지정된 크기(MB) 또는 시간(Min) 초과 시 세션을 닫고 새 파일을 엽니다. 로테이션 큐 초과 시 가장 오래된 파일 페어(.bin, .pcm)를 동반 삭제하고 인덱스 JSON을 갱신합니다.
### 2.6 네트워크 및 통신 엔진 (T470_Commu)
 * **WiFi 관리:** * 동적 모드 제어 (AP, STA, AP+STA, Auto-Fallback).
   * 최대 3개의 Multi-AP 순회 접속 및 정적 IP 설정 지원. 연결 실패 시 SoftAP 모드로 자가 구동(Auto-Fallback).
   * 메인 루프에서 논블로킹 타이머로 소켓 클린업 및 재연결 수행.
 * **비동기 Web API (ESPAsyncWebServer):**
   * GET /api/status: 시스템 상태, 힙/PSRAM 잔여량 조회.
   * GET /api/runtime_config, POST /api/runtime_config: 설정 다운로드 및 Chunked 업로드 (부분 병합/OOM 방어 적용).
   * GET /api/download?file=...: 기록 파일 다운로드 (단, RECORDING 상태 시 SPI 경합을 막기 위해 HTTP 423 Locked 응답).
   * POST /api/recorder_begin, /api/recorder_end, /api/noise_learn, /api/reboot, /api/factory_reset: FSM 비동기 커맨드 하달.
   * POST /api/ota: 펌웨어 무선 업데이트 (_isOtaRunning 파티션 락 적용).
 * **WebSocket & MQTT:**
   * ws://IP/ws: 추출된 39D MFCC (추후 TelemetryPacket으로 확장) 바이너리 브로드캐스트 (가용 버퍼 검사 로직 적용).
   * MQTT: 지정된 브로커로 판정 결과(JSON) Publish 및 논블로킹 자동 재접속.
## 3. 프론트엔드 (Web UI) 설계 규격
### 3.1 코어 아키텍처
 * **파일 분리:** index.html, style.css, app.js 3개 파일로 구성하여 가독성을 높입니다.
 * **다국어 (i18n):** data-i18n 속성과 JSON 사전을 활용하여 한국어/영어 실시간 전환을 지원하며 localStorage에 상태를 저장합니다.
 * **모바일 반응형 방어:** CSS Grid의 minmax 폭을 100% 기준으로 적용하고 overflow-x: hidden을 설정하여 안드로이드 크롬 등에서 화면 렌더링이 찢어지는 현상을 원천 차단합니다.
### 3.2 주요 컴포넌트 및 방어 로직
 * **대시보드 (Dashboard):** * setInterval(1초)을 통한 헬스체크 및 sys_state에 따른 뱃지 애니메이션(Pulse) 처리.
   * 수동 녹음, 노이즈 학습 제어 인터페이스.
 * **동적 설정 폼 (Config Forms):**
   * 백엔드 JSON의 Depth(ex. dsp.hop_ms, decision.sta_lta_threshold)와 HTML name 속성을 1:1 매핑.
   * setNested() 헬퍼 함수를 통한 깊은 복사(Deep Object) JSON 직렬화 수행 후 POST /api/runtime_config 전송.
 * **차트 렌더링 (추후 연동 준비):**
   * 브라우저 뻗음 현상을 막기 위해 DOM 기반 라이브러리를 배제하고 Canvas/WebGL 기반 고속 차트(uPlot 등) 사용 예정.
   * requestAnimationFrame 루프를 통한 60Hz 배치 렌더링 (WebSocket onmessage 디커플링).
 * **통신 및 기기 제어 방어:**
   * 재부팅, 공장초기화, 설정 덮어쓰기 버튼은 반드시 confirm() 팝업을 띄워 휴먼 에러 방지.
   * 명령 전송 후 브라우저는 기기 부팅 시간(약 5~6초) 대기 후 안전하게 location.reload() 수행.
   * OTA 업데이트 시 XMLHttpRequest.upload.onprogress를 통한 실어진행률 렌더링 지원.

