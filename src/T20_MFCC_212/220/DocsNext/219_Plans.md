- 프론트엔드] Chart.js 실시간 성능 부하
   - 현재 app_220_001.js는 requestAnimationFrame을 통해 60Hz로 렌더링을 시도합니다.
   - 위험: 3축 모드 + 1024 FFT를 켜면, 매 프레임마다 Chart.js가 3000개 이상의 포인트를 다시 그립니다. 모바일 브라우저나 저사양 PC에서는 웹 UI가 급격히 느려질 수 있습니다.
   - 권장: FFT Size가 512 이상일 경우, pendingWave 데이터를 전송하기 전에 2:1 혹은 4:1로 **Decimation(추출)**하여 차트 포인트 숫자를 줄이는 로직을 JS에 추가하는 것이 좋습니다.
- [백엔드] Smart Trigger - 밴드 에너지 초기화 문제
   - 점검: 이 루프는 all_ready 상태가 아닐 때(MFCC 히스토리가 덜 찼을 때)도 매번 실행되어 ws_payload를 채웁니다.
   - 개선: p->dsp.getBandEnergy()는 FFT 직후에 에너지를 계산하므로 문제가 없으나, 메모리 효율을 위해 all_ready 조건 밖에서 수행하는 것이 맞습니다. 다만, max_band_energy를 p_feature->band_energy에 대입하는 시점은 all_ready 안쪽이어야 합니다.
   - 
   

- 트리거 전후 데이터 확보 (Pre-Trigger Buffering)
   - 현황: 트리거가 발생하는 순간부터 기록을 시작하므로, 충격 직전의 징후 데이터를 놓칠 수 있습니다.
   - 개선: 순환버퍼 등을 활용하여 trigger 발생전 최대한의 과거 데이터 저장되게 


- 트리거 전후 데이터 확보 (Pre-Trigger Buffering)
   - 현황: 트리거가 발생하는 순간부터 기록을 시작하므로, 충격 직전의 징후 데이터를 놓칠 수 있습니다.
   - 개선: raw_buffer 슬롯 4개 중 1~2개를 순환 버퍼로 상시 유지하여, 트리거 발생 전 약 100~200ms 분량의 데이터를 포함해 SD에 저장하도록 수정 제안합니다.
   - 개선: StorageService에 약 1~2초 분량의 특징량 순환 버퍼를 두고, 트리거 발생 시 **"발생 2초 전 데이터 + 현재 데이터"**를 묶어서 저장하도록 개선 (사고 분석 시 매우 유용).





- 트리거, 이벤트 및 상태관리 개선 방안 검토
    - 여러 상태 변수,flag 변수들 통폐합/분리 검토 및 상태 머신(FSM) 명시적 구현
    - 시작모드 구분
          - auto :  자동 시작
          - manual : 사용자 시작, 종료
          - 스케줄(향후 구현) : 특정요일 들, 특정시간대 들 
    - 노이즈 학습(manual, 적응형)
    - 관련 설정항목 통폐합 및 분리 검토
    - trigger 이벤트 유형, 상태관리 검토
    - trigger 변수 및 임계값 분리/통폐합 검토

### 1. ⚙️ 명시적 상태 머신 (FSM) 설계

기존의 단순 boolean 플래그들을 통폐합하여, 시스템 전체의 흐름을 관장하는 **메인 FSM**과 **서브 FSM**으로 분리합니다.  

#### **A. 메인 시스템 상태 (System State)**   
measurement_active를 대체하며, 시스템의 현재 행동을 명확히 정의합니다.
 * SYS_STATE_IDLE: 센서는 작동 중지 또는 초저전력 대기 상태. (딥슬립 진입 대기)
 * SYS_STATE_MONITOR: 센서 수집 및 DSP 연산은 진행하되, **저장(SD)은 하지 않고 트리거만 감시**하는 상태. (웹 UI 실시간 모니터링 유지)
 * SYS_STATE_RECORD: 수동 조작 또는 트리거 조건이 충족되어 **데이터를 스토리지에 기록** 중인 상태.
 * SYS_STATE_FAULT: 센서 먹통(Watchdog), SD카드 에러 등으로 인한 예외 상태.
 * 
#### **B. 노이즈 학습 상태 (Noise State)**  
 * NOISE_STATE_IDLE: 노이즈 제거 OFF 또는 초기화 상태.
 * NOISE_STATE_LEARNING: 환경 소음을 수집하여 프로필을 생성 중인 상태 (설정된 Frame 도달 시 자동 전환).
 * NOISE_STATE_ACTIVE: 학습된 노이즈 프로필을 바탕으로 스펙트럼 감산을 적용 중인 상태.
 * 
 
### 2. 🚀 시작 모드(Operation Mode) 구분 및 구조체 개선
단순 auto_start boolean을 없애고, 확장성을 고려한 열거형(Enum)으로 통폐합합니다.
```cpp
typedef enum {
    EN_T20_MODE_MANUAL = 0,    // 사용자 시작(버튼/Web) 시에만 RECORD 상태 진입
    EN_T20_MODE_AUTO_TRIGGER,  // 부팅 직후 MONITOR 상태 진입, 트리거 감지 시 자동 RECORD 진입
    EN_T20_MODE_SCHEDULE       // (향후) 특정 요일/시간대 조건 충족 시 MONITOR/RECORD 진입
} EM_T20_OpMode_t;

```
 * **적용 방안:** cfg.system.auto_start를 cfg.system.op_mode로 교체합니다.
 
 

 
### 4. 🔔 이벤트 유형(Event Type) 정형화
기존에는 문자열 하드코딩("smart_trigger_alert", "btn_start")으로 이벤트를 처리했습니다. 이를 정형화된 Event Enum으로 관리하여 MQTT와 Storage 로그의 일관성을 높입니다.
```cpp
typedef enum {
    EVT_SRC_MANUAL_UI = 1,     // 웹/앱에서 시작
    EVT_SRC_MANUAL_BTN,        // 하드웨어 버튼 누름
    EVT_SRC_TRIG_RMS,          // DSP 전체 RMS 초과
    EVT_SRC_TRIG_BAND_0,       // 주파수 밴드 0 초과
    EVT_SRC_TRIG_BAND_1,
    EVT_SRC_SYS_SCHEDULE,      // (향후) 스케줄러에 의한 시작
    EVT_SRC_SYS_WATCHDOG       // 에러/워치독 이벤트
} EM_T20_EventSource_t;

```
### 5. 🔄 제안하는 상태 전환(Transition) 흐름도
 * **[MANUAL 모드]**
   * 부팅 -> SYS_STATE_IDLE
   * 사용자 START 버튼 -> SYS_STATE_RECORD (저장 시작)
   * 사용자 STOP 버튼 -> SYS_STATE_IDLE (저장 종료)
 * **[AUTO_TRIGGER 모드]**
   * 부팅 -> SYS_STATE_MONITOR (버퍼 순환, 감시 중)
   * RMS/Band 임계값 초과 감지 -> EVT_SRC_TRIG_... 발생 -> SYS_STATE_RECORD 진입 (이벤트와 함께 스토리지 오픈)
   * 신호가 임계값 아래로 떨어짐 -> hold_time_ms 타이머 가동 -> 타이머 만료 시 SYS_STATE_MONITOR로 복귀
   * 모니터 상태에서 sleep_timeout_sec 초과 -> 센서 HW Wake 세팅 후 딥슬립.
### 💡 리팩토링 진행 방향 제안
이 구조를 도입하면 T230_Mfcc_Cor_220.cpp의 수많은 if(measurement_active)와 하드코딩된 5000ms 타이머가 하나의 깔끔한 **switch(sys_state) 문으로 정리**됩니다. 향후 스케줄 기능이 들어와도 case EN_T20_MODE_SCHEDULE: 하나만 추가하면 끝납니다.
이 방향에 동의하신다면, **① T210_Def_221.h의 구조체/Enum 통폐합 선언부 리팩토링**을 먼저 진행할까요, 아니면 **② 메인 코어의 State Machine 제어 로직 구조**부터 먼저 짜드릴까요?

## ***
## --- 
## ***

- 가속도/자이로 구분 설정/임계값
- 
- 가속도, 자이로 단위 통일 필요(측정,표시,임계값)

- 3축모드 설정시 가속도 3축인지 자이로 3축 구분
- T234_Storage_Service 내부에서 기록 실패가 N회 이상 발생하면 자동으로 closeSession() 후 다시 begin()을 시도하는 보호 로직
- 안전한 재부팅: 웹에서 설정을 저장하고 ESP.restart()가 호출될 때, StorageService의 세션이 열려있다면 closeSession()이 호출되지 않고 꺼질 수 있습니다. CommService의 reboot API 핸들러에서 p->storage.closeSession("reboot")을 먼저 호출하도록 보강
- ST_T20_RecorderBinaryHeader_t의 version이 JSON에서는 219로 되어 있고 코드에서는 220으로 혼용되는 부분이 보입니다. 바이너리 파일 분석 툴(Python 등)과의 호환성을 위해 220으로 통일
- 스마트 트리거 밴드별 논리 연산 (AND/OR/XOR)
   - 현황: 현재는 3개의 밴드 중 하나라도 넘으면 트리거되는 OR 구조입니다.
   - 개선: "A 밴드는 높고 B 밴드는 낮을 때"와 같은 조합 논리를 추가하면, 단순 충격과 실제 기계 결함 진동을 더 정밀하게 구분할 수 있습니다.
- 트리거 히스테리시스 (Hysteresis) 강화
   - 문제: 진동이 임계값 근처에서 왔다 갔다 할 경우 파일이 너무 자주 열리고 닫히는 현상이 생길 수 있습니다.
   - 개선: trigger_hold_frames 외에 **입력 임계값(High)**과 **해제 임계값(Low)**을 분리하여 '채터링' 현상을 방지.
   - 
- 적응형 임계값 (Adaptive Threshold)
   - 상황: 주변 배경 진동이 바뀌면 오작동할 수 있습니다.
   - 개선: NoiseMode 학습 시의 에너지를 바탕으로 임계값을 $Background\_Energy \times Margin$ 형태로 자동 설정하는 모드 추가.


## ***
## --- 
## ***


### 2. 데이터 수집 및 라벨링 자동화 (Active Learning Support)
양질의 AI 모델을 위해서는 데이터의 '질'이 중요합니다.
 * **웹 기반 데이터 라벨링 도구**: 'Explorer' 탭에서 저장된 바이너리 파일을 선택하면 해당 구간의 MFCC 워터폴을 다시 보여주고, 사용자가 "정상/비정상" 태그를 달아 JSON 인덱스에 저장하는 기능을 추가합니다.



### 메모리, littlefs, sd 사용량/잔여용량 모니터링



### 5. 대시보드 시각화 확장 (UX Enhancement)
웹 UI의 분석 기능을 전문가 수준으로 높입니다.
 * **오프라인 분석기 (Playback Mode)**: SD 카드에 저장된 바이너리 파일을 웹으로 불러와 실제 수집 당시의 차트와 워터폴로 재현(Replay)하는 기능을 구현합니다.
 * **주파수 대역별 RMS 차트**: 특정 설비 진동 분석에 필수적인 주파수 영역별 에너지 합산(RMS) 그래프를 추가하여 직관적인 모니터링을 돕습니다.
 * **다국어 지원 및 테마 최적화**: 사용자의 설정에 따라 한국어/영어 전환 및 가시성이 높은 고대비 테마 기능을 제공합니다.
 * **Web-based 3D 진동 시각화**: Three.js 등을 사용하여 웹 대시보드에 기계의 3D 모델을 띄우고, 측정 중인 축의 진동 방향과 세기를 실시간 화살표나 히트맵으로 표시합니다.
 





## ***
## --- **완료** ---
## ***



**T20 MFCC v219** 고도화 계획은 시스템을 단순한 데이터 수집기를 넘어, **지능형 클라우드 엣지 디바이스**로 진화시키는 매우 중요한 단계입니다. 제안하신 5가지 핵심 기능에 대한 기술적 검토와 구체적인 구현 로드맵을 정리해 드립니다.
### 1. 기술 검토 및 시스템 영향 분석
#### **① FFT SIZE 동적 설정 (256 ~ 4096)**
 * **검토**: 현재 FFT_SIZE는 상수로 고정되어 있으며, 관련 버퍼들이 정적으로 할당되어 있습니다.
 * **해결**: CL_T20_DspPipeline 내부의 작업 버퍼들을 heap_caps_malloc을 이용한 동적 할당 방식으로 전환해야 합니다. 4096 포인트 사용 시 약 16KB의 버퍼가 필요하며, S3의 내부 RAM(512KB)에서 충분히 수용 가능합니다.
 * **주의**: FFT 크기가 변하면 esp-dsp 라이브러리의 룩업 테이블 초기화와 윈도우 함수(_window) 재계산이 수반되어야 합니다.
#### **② NTP 기반 정밀 타임스탬프 삽입**
 * **검토**: 현재 바이너리 데이터는 frame_id만 가지고 있으며 절대 시간을 알 수 없습니다.
 * **해결**: ST_T20_FeatureVector_t 구조체에 uint64_t timestamp_ms 필드를 추가합니다.
 * **동기화**: T20_sensorTask에서 데이터 가득 참(Interrupt) 발생 시, gettimeofday()를 통해 마이크로초 단위의 정밀 시간을 획득하여 프레임에 기록합니다.
#### **③ Cloud & Connectivity (MQTT / OTA)**
 * **MQTT**: PubSubClient 라이브러리를 통합하여 CL_T20_CommService에서 비동기 발행(Publish)을 수행합니다. 이상 탐지 발생 시 즉시 JSON 페이로드를 전송합니다.
 * **OTA**: Update.h 라이브러리를 사용하여 웹 핸들러에 바이너리 업로드 엔드포인트를 추가합니다.
#### **④ 이벤트 기반 레코딩 트리거 (Smart Trigger)**
 * **검토**: 단순 RMS 임계값 외에 주파수 대역별 에너지 감시가 필요합니다.
 * **해결**: DSP 파이프라인에서 특정 Mel-Bin 주파수 대역의 합산 에너지를 계산하고, 설정된 임계값(Threshold)과 비교하여 storage.openSession()을 자동으로 호출하는 로직을 T20_processTask에 구현합니다.
#### **⑤ 다중 축(Multi-axis) 퓨전 (117D 확장)**
 * **검토**: 현재 센서 엔진은 한 번에 한 축의 FIFO만 읽습니다.
 * **해결**: BMI270 FIFO 설정에서 3축(X, Y, Z)을 동시에 활성화하고, readFifoBatch를 통해 인터리빙(Interleaving)된 데이터를 분리하여 3개의 DSP 인스턴스 또는 루프를 통해 처리합니다. 결과 벡터는 39 * 3 = 117차원으로 결합됩니다.
### 2. v219 구현 계획 (Implementation Roadmap)
#### **[Phase 1] 데이터 구조 및 메모리 혁신**
 1. **구조체 업데이트 (T214)**: ST_T20_Config_t에 fft_size 필드 추가, ST_T20_FeatureVector_t에 timestamp_ms 추가.
 2. **동적 버퍼 할당 (T231)**: CL_T20_DspPipeline 내부의 _work_frame, _power 등을 포인터로 변경하고 begin()에서 fft_size에 맞춰 메모리를 할당하는 로직 구현.
 3. **바이너리 헤더 갱신**: 기록 파일의 header_size를 유지하면서 타임스탬프 유무를 나타내는 플래그 추가.
#### **[Phase 2] 지능형 제어 및 클라우드 통합**
 1. **NTP 서비스 강화**: CommService 시작 시 NTP 서버 동기화 완료를 확인한 후 시스템 시각을 설정합니다.
 2. **MQTT 엔진 탑재**: 브로커 주소, 토픽 설정을 JSON에서 로드하고 백그라운드 태스크에서 상태 정보를 발행합니다.
 3. **트리거 엔진 구현**: preprocess.filter 단계 이후 특정 주파수 밴드(예: 베어링 결함 주파수 대역)의 에너지를 감시하는 로직을 추가합니다.
#### **[Phase 3] 웹 인터페이스 및 OTA 확장**
 1. **UI 업데이트 (index_218, app_218)**: FFT Size(256~4096) 드롭다운 메뉴 추가 및 MQTT 설정 폼 추가.
 2. **바이너리 파서 업데이트**: 117차원 데이터 및 타임스탬프가 포함된 새로운 WebSocket 바이너리 포맷을 프론트엔드에서 파싱할 수 있도록 수정합니다.
 3. **OTA 핸들러 등록**: /api/t20/ota_update 엔드포인트를 열어 웹에서 직접 .bin 펌웨어를 업로드 가능하게 합니다.
### 3. v219 예상 시스템 사양 (Target Spec)
 * **샘플링 주파수**: 1600Hz 고정 (BMI270 최적화)
 * **분석 해상도**: 최대 0.39Hz (4096 FFT 시)
 * **특징량**: 39D (Single) 또는 117D (Triple-axis Fusion)
 * **메모리 점유**: 내부 RAM 약 150~200KB (동적 할당 포함)
 * **통신 스택**: HTTP, WebSockets, MQTT, OTA 지원
이 계획에 따라 가장 먼저 **[Phase 1]의 메모리 동적 할당 및 타임스탬프 구조체 수정**부터 진행할 준비가 되었습니다. 어떤 단계부터 코드를 구체화해 나갈까요?



***

## 1. FFT SIZE 웹에서 설정
  * 256, 512, 1024, 2048, 4096
## 2. **NTP 기반 정밀 타임스탬프**: 파일 이름뿐만 아니라 바이너리 데이터 내부 프레임마다 정확한 UTC 타임스탬프를 삽입하여 타 장비 데이터와의 동기화 분석을 가능하게 합니다.

## 3. 통신 및 원격 관리 고도화 (Cloud & Connectivity)
현재는 로컬 웹 서버 중심이지만, 장비가 늘어날 경우를 대비해야 합니다.
 * **MQTT 프로토콜 지원**: 대시보드 접속 없이도 원격 서버(AWS IoT, Mosquitto 등)로 이상 징후 알림 및 시스템 상태를 발행(Publish)합니다.
 * **OTA(Over-The-Air) 업데이트**: 웹 인터페이스를 통해 새로운 펌웨어(AI 모델 포함)를 업로드하고 기기를 원격으로 업데이트하는 기능을 추가합니다.

## 4. **이벤트 기반 레코딩 트리거**: 단순 버튼이나 설정값이 아닌, 특정 주파수 대역의 에너지가 급증하거나 AI가 "모르는 패턴"이라고 판단할 때 자동으로 SD 카드에 기록을 시작하는 기능을 구현합니다.
## 5.**다중 축(Multi-axis) 퓨전**: 현재는 한 번에 하나의 축(예: Accel Z)만 분석하지만, 3축 데이터를 동시에 MFCC로 추출하여 특징량을 확장(39D → 117D)함으로써 공간적 진동 특성을 반영합니다.



### 3. 🎯 트리거 변수 및 임계값(Threshold) 분리/통폐합
현재 cfg.trigger 안에는 하드웨어 전원 관리(딥슬립/Wake)와 소프트웨어 연산(DSP 트리거)이 혼재되어 있습니다. 이들의 목적과 임계값 단위를 명확히 분리해야 합니다.
**개선된 Trigger 구조체 설계안:**
```cpp
typedef struct {
    // 1. 하드웨어 전원/모션 제어 (BMI270 칩셋 레벨)
    struct {
        bool     use_deep_sleep;
        uint32_t sleep_timeout_sec;
        float    wake_threshold_g;     // 하드웨어 Any-Motion 임계값 (G 단위)
        uint16_t duration_x20ms;
    } hw_power;

    // 2. 소프트웨어 이벤트 감시 (DSP 레벨)
    struct {
        uint32_t hold_time_ms;         // (중요) 기존 소스에 5000ms로 하드코딩된 값을 변수화
        
        bool     use_rms;
        float    rms_threshold_power;        // 전체 진동 파워 임계값

        ST_T20_TriggerBand_t bands[T20::C10_DSP::TRIGGER_BANDS_MAX]; // 다중 밴드 감시
    } sw_event;
} ST_T20_ConfigTrigger_t;

```
 * **분리 이유:** 하드웨어를 깨우는 wake_threshold_g는 거칠고 큰 충격(예: 1.0G)에 반응해야 하지만,  
 * DSP에서 감시하는 rms_threshold_power는 미세한 변화(예: 0.1G)를 잡아내야 할 수 있으므로 두 임계값을 분리하는 것이 맞습니다.
 
- storage.closeSession("trigger_hold_timeout"); 5초 고정값을 설정값으로 변경
- bmi wakup 임계값(단위확인필요)과 rms 임계값 구분 필요 검토
