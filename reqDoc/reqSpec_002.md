# IMU Logger (ESP32-S3 + BMI270) H/W · S/W 기능 요구사항 명세서

## 1. 문서 목적

이 문서는 **ESP32-S3 + BMI270 + SD Card + DS3231** 기반의 고성능 IMU Logger를 상용 수준으로 구현하기 위한 하드웨어/소프트웨어 요구사항을 정리한 명세서입니다.
주 사용 목적은 **액션캠, 스마트폰 상단/측면, 자전거, 손, 드론 등**에 부착하여 고주파 IMU 데이터를 안정적으로 기록하고, 이후 **Gyroflow**에서 영상 안정화 데이터로 활용하는 것입니다.

참조 대상은 **Senseflow A1** 계열 장치이며, 여기서 제공하는 UX/운용 개념을 참고하되, 다음 방향으로 확장합니다.

* 더 높은 로깅 안정성
* 더 직관적인 LED 상태 피드백
* 더 강한 전원/파일 무결성 설계
* 더 체계적인 상태 머신 기반 운용
* 더 유연한 설정/웹 관리/USB 데이터 추출

---

## 2. 제품 목표

### 2.1 핵심 목표

* **BMI270 IMU 데이터를 1000Hz급으로 안정 기록**
* **SD 카드 쓰기 지연 최대 100ms 수준에도 데이터 유실 없이 버퍼링**
* **전원 차단/강제 종료 상황에서도 손실 범위를 최소화**
* **Gyroflow 연동을 고려한 타임스탬프/축 정의/메타데이터 체계 확보**
* **현장 사용자가 LED/버튼만으로도 상태를 즉시 이해할 수 있는 UX 제공**
* **USB 연결 시 저장장치처럼 접근 가능하거나, 최소한 빠른 데이터 추출 가능**
* **배터리 구동 장치로서 슬립/웨이크/동작 감지 기반 저전력 최적화**

### 2.2 사용 시나리오

* 액션캠 상단 부착 후 자전거 라이딩 IMU 로깅
* 스마트폰 리그 측면 부착 후 손떨림/이동 영상 보정용 로그 수집
* 드론/RC 플랫폼 진동 분석 및 자세 로그 수집
* 핸드헬드 테스트 기기로 짧은 반복 실험 데이터 기록

---

## 3. 상위 시스템 요구사항

### 3.1 플랫폼

* MCU: **ESP32-S3**
* 개발 환경: **PlatformIO + Arduino Core for ESP32**
* 외부 RAM: **PSRAM 사용 가능 보드 전제**
* RTOS: **FreeRTOS 활용**
* 파일시스템/SD: **SPI 기반 SD Card + SdFat 사용**
* IMU: **Bosch BMI270 (SPI)**
* RTC: **DS3231**
* UI: **Button 다수 + WS2812 RGB LED 다수**
* 연결성: USB, Wi-Fi(설정/시간동기화), 필요 시 BLE 확장 여지

### 3.2 기본 성능 목표

* 샘플링: 200 / 400 / 800 / 1000Hz 설정 가능
* 목표 운용: **1000Hz 연속 로깅 안정화**
* 로그 손실률: 정상 사용 조건에서 **0% 지향**
* SD 쓰기 정지(spike): **최대 100ms 이상 버퍼 흡수 가능**
* 파일 손상 시 영향 범위: **현재 파일 청크/세그먼트 단위로 최소화**
* 부팅 후 기록 시작까지: **사용자 체감 3~5초 이내**

---

## 4. Gyroflow 연동 관점 요구사항

## 4.1 데이터 품질 요구

* 자이로 데이터는 **낮은 지터**와 **일관된 샘플 간격**이 중요
* 타임스탬프는 가능한 한 **단조 증가(monotonic)** 해야 함
* 샘플 드롭/중복/역전 방지 필요
* IMU 축 방향 정의와 장치 장착 방향 보정 정보가 함께 관리되어야 함

### 4.2 메타데이터 요구

로그 파일 또는 헤더에 최소한 아래 정보 포함 권장:

* 펌웨어 버전
* 장치 ID / 시리얼
* 센서 모델(BMI270)
* 샘플링 주파수
* 가속도 범위 / 자이로 범위
* 디지털 필터 설정
* 장착 방향(orientation preset / custom matrix)
* RTC 기준 시각 / boot epoch / local tick 기준
* 캘리브레이션 오프셋 정보
* 파일 시작/종료 시각

### 4.3 영상 안정화 실사용 관점

Gyroflow는 자이로 기반 안정화에 의존하므로 다음이 중요합니다.

* 빠른 움직임에서는 충분히 짧은 셔터 속도 필요
* 카메라 내부 안정화(OIS/EIS/IBIS)는 가능하면 비활성화 권장
* 장치와 카메라 사이 시간 동기 기준이 명확해야 함
* 장착 방향이 바뀌어도 소프트웨어에서 쉽게 축 매핑 가능해야 함

---

## 5. 하드웨어 요구사항

## 5.1 블록 구성

* ESP32-S3
* BMI270 (SPI)
* SD Card (SPI)
* DS3231 (I2C 권장)
* WS2812 RGB LED x N
* 다중 버튼
* 배터리 입력 / 충전회로 / 3.3V 전원 레일
* USB Type-C (전원 + 데이터)
* 선택: 부저, 진동 모터, 전원 측정 ADC, 카메라 동기 GPIO

### 5.2 센서(IMU) 요구사항

* BMI270는 SPI 연결 사용
* INT1 핀을 ESP32-S3 GPIO 인터럽트 입력에 연결
* 가급적 IMU는 보드 진동/비틀림 영향을 줄이는 위치에 배치
* 기판 휨(strain), 온도 변화, 전원 노이즈가 오프셋에 미치는 영향 최소화
* IMU 주변 GND 리턴 경로를 짧고 안정적으로 설계
* 필요 시 기구적으로 폼/댐핑 구조 검토하되, 실제 motion fidelity 훼손은 피해야 함

### 5.3 저장장치 요구사항

* microSD socket 사용
* SD는 **SPI + SdFat dedicated mode** 우선 검토
* 전원 차단 시 카드 손상 최소화를 위해 충분한 디커플링 필요
* 카드 detect 스위치가 있다면 상태 감지에 활용
* 실제 상용 조건에서 카드 호환성 검증 필요

  * 일반 TLC 카드
  * 고내구성(high endurance) 카드
  * U1/U3/V30 등급 카드별 비교

### 5.4 RTC 요구사항

* DS3231 사용
* RTC 배터리 백업 포함
* Wi-Fi 접속 가능 시 NTP로 RTC 보정
* 로그 파일명/세그먼트 시각/사용자 표시 시간은 RTC 기준 사용
* RTC 불량 또는 시간 미설정 상태를 구분할 수 있어야 함

### 5.5 버튼 요구사항

최소 권장 버튼 구성:

* Power / Mode 버튼
* Record 버튼
* Up 버튼
* Down 버튼
* Function 버튼(선택)

버튼 UX 요구:

* short press / long press / multi-click 구분
* 디바운싱 확실히 구현
* 장갑 착용/실외 사용 고려한 물리감 확보
* 오동작 방지를 위한 길게 누름 전원 ON/OFF 채택 권장

### 5.6 LED 요구사항

WS2812 RGB LED 다수개를 사용하여 다음 정보를 직관적으로 표시:

* 전원 상태
* 부팅 중
* 캘리브레이션 중
* 기록 대기
* 기록 중
* 파일 flush 중
* Wi-Fi AP 모드
* USB MSC 모드
* 오류 상태(SD 없음, RTC 오류, IMU 오류, 파일 손상 위험, 저전압)
* 배터리 수준(막대형 또는 색상형)

권장 UX 예시:

* 녹색 점멸: 대기
* 적색 고정: 기록 중
* 청색 천천히 점멸: Wi-Fi 설정 모드
* 황색 빠른 점멸: 캘리브레이션 중
* 적색 빠른 점멸: 오류
* 무지개 sweep 1회: 부팅 완료

### 5.7 USB 요구사항

* ESP32-S3 네이티브 USB 사용
* USB CDC(디버그/CLI), MSC(대용량 저장장치), DFU/OTA 연계 구조 검토
* 실사용에서는 **USB 연결 시 안전하게 로깅 정지 후 MSC 전환** 정책 권장
* PC 연결 시 파일 추출이 쉬워야 함

### 5.8 전원 설계 요구사항

* 배터리 기반 사용 전제
* 입력원: Li-ion/LiPo 1셀 + 충전 IC + 3.3V regulator 구조 권장
* 피크 전류 고려: ESP32 Wi-Fi 송신, SD write, WS2812 동시 구동
* 주요 설계 포인트:

  * 저노이즈 3.3V 레일
  * 충분한 bulk cap 배치
  * SD 근처/ESP32 근처/IMU 근처 개별 디커플링
  * 배터리 저전압 감지
  * brownout 방지
  * USB 전원과 배터리 전원 전환 정책
* SD 쓰기/USB/Wi-Fi 동작 시 전압 강하 검증 필요

### 5.9 기구/실장 고려사항

* 액션캠/스마트폰 리그 장착을 위한 얇은 폼팩터
* 진동 전달은 확보하되 과도한 기판 공진은 억제
* 열 분산 고려
* microSD 교체 용이성
* LED 가시성
* 버튼 오조작 방지
* 렌즈/카메라 광축과의 orientation 관계를 사용자가 쉽게 맞출 수 있어야 함

---

## 6. 권장 핀맵 설계 원칙

> 실제 보드 핀 충돌은 사용 보드(ESP32-S3-WROOM/WROVER/Zero 계열), PSRAM 점유 핀, USB, 스트래핑 핀에 따라 달라지므로 최종 PCB 전에는 반드시 회로도/보드 variant 기준 재검증이 필요합니다.

### 6.1 핀 선정 원칙

* USB D+/D-는 ESP32-S3 네이티브 USB 핀 사용
* SPI는 가능하면 다음처럼 분리 검토

  * IMU용 SPI 버스
  * SD용 SPI 버스 또는 동일 SPI 내 CS 분리
* 고속/지속 쓰기 안정성 관점에서는 **SD 전용 SPI bus**가 유리
* IMU INT는 인터럽트 가능한 GPIO 사용
* WS2812는 RMT 지원 사용이 쉬운 GPIO 배정
* 부팅 스트래핑에 민감한 핀은 버튼/외부풀업/LED에 신중 사용
* Deep/Light Sleep wakeup 가능 핀 고려

### 6.2 예시 핀맵(개념안)

| 기능            | 권장 연결  | 비고                |
| ------------- | ------ | ----------------- |
| USB D-        | GPIO19 | ESP32-S3 네이티브 USB |
| USB D+        | GPIO20 | ESP32-S3 네이티브 USB |
| BMI270 SCK    | GPIO12 | SPI IMU           |
| BMI270 MOSI   | GPIO11 | SPI IMU           |
| BMI270 MISO   | GPIO13 | SPI IMU           |
| BMI270 CS     | GPIO10 | SPI IMU CS        |
| BMI270 INT1   | GPIO14 | 데이터 준비 인터럽트       |
| SD SCK        | GPIO36 | SPI SD            |
| SD MOSI       | GPIO35 | SPI SD            |
| SD MISO       | GPIO37 | SPI SD            |
| SD CS         | GPIO34 | SPI SD CS         |
| DS3231 SDA    | GPIO8  | I2C               |
| DS3231 SCL    | GPIO9  | I2C               |
| Button Power  | GPIO4  | wakeup 고려         |
| Button Record | GPIO5  |                   |
| Button Up     | GPIO6  |                   |
| Button Down   | GPIO7  |                   |
| WS2812 DIN    | GPIO21 | RMT 사용 권장         |
| Battery ADC   | GPIO1  | 분압 회로             |
| Buzzer/Vibe   | GPIO2  | 선택                |

### 6.3 핀맵 검증 체크리스트

* PSRAM 사용 핀과 충돌 없는지
* 부팅 스트래핑 영향 없는지
* USB 사용과 충돌 없는지
* SPI 신호선 길이/배치 적절한지
* SD와 IMU 동시 구동 시 signal integrity 문제 없는지
* wakeup 지원 핀 조건 충족하는지

---

## 7. 소프트웨어 아키텍처 요구사항

### 7.1 아키텍처 원칙

* 공통 함수화
* 객체지향/모듈화
* 상태 머신 기반 운용
* ISR 최소화, 큐/버퍼 기반 비동기 처리
* 로깅 경로는 **동적 메모리 할당 최소화**
* 장애 복구/오류 격리/진단 로그 확보
* 상용 수준 유지보수를 위한 계층 분리

### 7.2 권장 소프트웨어 계층

1. **BSP / HAL 계층**

   * GPIO, SPI, I2C, USB, RTC, LED, Button
2. **Device Driver 계층**

   * BMI270Driver
   * SdCardDriver
   * DS3231Driver
   * BatteryMonitor
3. **Service 계층**

   * ImuSamplingService
   * LoggerService
   * FileManagerService
   * CalibrationService
   * TimeSyncService
   * LedUiService
   * PowerService
   * WebConfigService
4. **Application 계층**

   * StateMachine
   * ModeController
   * CommandDispatcher
5. **Utility/Common 계층**

   * RingBuffer
   * DoubleBuffer
   * EventBus / Queue wrapper
   * CRC / checksum
   * Config manager
   * Binary codec / CSV exporter

### 7.3 권장 클래스/모듈 예시

* `AppController`
* `SystemStateMachine`
* `ImuManager`
* `ImuSamplerTask`
* `LogWriterTask`
* `FileSegmentManager`
* `RtcTimeManager`
* `PowerManager`
* `LedPatternManager`
* `ButtonManager`
* `WebServerManager`
* `UsbModeManager`
* `ConfigManager`
* `HealthMonitor`

---

## 8. 시스템 상태 머신 설계

### 8.1 상위 모드

* BOOT
* SELF_TEST
* IDLE
* CALIBRATION
* READY
* RECORDING
* FLUSHING
* WIFI_CONFIG
* USB_MSC
* ERROR
* SLEEP
* LOW_BATTERY_PROTECT

### 8.2 대표 상태 전이

* 전원 ON → BOOT → SELF_TEST
* SELF_TEST 성공 → READY
* READY + Record 버튼 → CALIBRATION 또는 즉시 RECORDING
* RECORDING 중 정지 → FLUSHING → READY
* USB 연결 감지 → 안전 정지 → USB_MSC
* 무동작 timeout → SLEEP
* motion interrupt → READY 또는 RECORDING 복귀
* SD/IMU 치명 오류 → ERROR

### 8.3 상태 머신 요구사항

* 각 상태는 entry / do / exit action 분리
* 비동기 이벤트 기반 상태 전이
* 상태 전이 로깅 필수
* LED 패턴과 상태를 1:1에 가깝게 연결
* 오류 상태는 recoverable / fatal 구분

---

## 9. FreeRTOS Task / Queue 설계

### 9.1 권장 Task 구성

1. **ImuAcquisitionTask**

   * BMI270 FIFO/DRDY 기반 샘플 획득
   * 최고 우선순위권
2. **LogAssembleTask**

   * 샘플 패킷화, 타임스탬프 보정, 버퍼 축적
3. **SdWriteTask**

   * 대형 버퍼를 SD에 순차 기록
4. **UiTask**

   * 버튼 스캔, LED 패턴, 사용자 피드백
5. **SystemTask**

   * 상태 머신, 오류 감시, 저전력 전이
6. **WebTask**

   * Wi-Fi/AP/웹 설정 처리
7. **UsbTask**

   * USB 연결 상태 및 MSC 전환 관리
8. **MaintenanceTask**

   * RTC sync, health metrics, housekeeping

### 9.2 Queue / Buffer 구조

* ISR → 샘플 ready 이벤트만 queue 전송
* 샘플 데이터는 queue보다 **lock-free ring buffer / double buffer** 우선
* 상태 이벤트, 버튼 이벤트, 오류 이벤트는 queue 사용
* SD writer는 큰 block 단위 쓰기를 유지

### 9.3 우선순위 원칙

* IMU 취득 > 로그 조립 > SD 쓰기 > 시스템 제어 > UI > 웹
* 웹서버/Wi-Fi가 로깅 실시간성에 영향 주지 않도록 코어/우선순위 분리 고려

---

## 10. 데이터 로깅 설계

### 10.1 핵심 원칙

* **실행 중 CSV 직접 기록 금지**
* **이진(binary) 형식으로 우선 저장**
* **큰 버퍼 + 순차 쓰기 + pre-allocation + segment rotation** 조합 사용
* 센서 취득과 SD 쓰기를 완전히 분리

### 10.2 샘플 구조 예시

```text
struct ImuSample {
  uint32_t tick_us;
  int16_t  gyro_x;
  int16_t  gyro_y;
  int16_t  gyro_z;
  int16_t  acc_x;
  int16_t  acc_y;
  int16_t  acc_z;
  int16_t  temp;
  uint16_t status;
}
```

### 10.3 타임스탬프 전략

권장 계층:

1. 고해상도 monotonic tick (`esp_timer_get_time()` 계열)
2. RTC 기준 wall-clock start time
3. 파일 헤더에 boot epoch 기록

즉, 각 샘플은 monotonic microsecond 기반으로 기록하고, 파일 헤더에 실시간 시각 anchor를 넣는 구조가 바람직합니다.

### 10.4 Double Buffering 요구사항

* 최소 2개 이상 대형 버퍼
* 예: active buffer / write buffer
* active buffer가 차면 write buffer로 넘기고, 즉시 다음 active buffer로 전환
* SD 쓰기 지연이 길어질 경우를 위해 3중 버퍼 또는 ring buffer 확장 가능

### 10.5 버퍼 용량 산정 예시

1000Hz, 샘플당 24~32B 가정 시:

* 초당 약 24~32KB
* 100ms stall 흡수에 약 2.4~3.2KB 이상 필요
* 실제 상용 설계에서는 여유를 크게 잡아 **수십~수백 KB급 버퍼** 권장
* PSRAM 사용 가능하더라도 **실시간 producer-consumer 경로 핵심 버퍼는 내부 RAM 우선**, 장기 스풀/보조 버퍼는 PSRAM 검토

### 10.6 파일 기록 정책

* `.bin` 형태의 연속 파일 기록
* 파일 시작 시 헤더 작성
* 본문은 고정 길이 record 또는 chunk 단위 저장
* 세그먼트 종료 시 footer/index/checkpoint 선택 적용
* 전원 차단 대비를 위해 일정 주기마다 sync/metadata checkpoint 수행

### 10.7 자동 파일 분할

* 5분 단위 자동 세그먼트 분할 권장
* 파일명 예시:

  * `IMU_20260307_101500_0001.bin`
  * `IMU_20260307_102000_0002.bin`
* 장점:

  * 전원 차단 시 피해 범위 축소
  * 파일 탐색 편의
  * PC 후처리 용이

### 10.8 사전 할당(pre-allocation)

* 파일 open 직후 예상 세그먼트 크기만큼 pre-allocate
* 가능하면 contiguous allocation 유도
* 세그먼트 종료 시 실제 길이로 truncate 또는 footer finalize
* 카드 단편화가 성능에 큰 영향을 주므로 장치 내 포맷 기능 제공 권장

### 10.9 무결성 요구사항

* 파일 헤더 magic/version/checksum 포함
* chunk 단위 CRC 선택 가능
* 정상 종료 플래그 / 비정상 종료 복구 플래그 관리
* 부팅 시 미종료 파일 scan & repair 수행

---

## 11. BMI270 운용 요구사항

### 11.1 샘플링/필터

* Gyroflow 목적이면 200~1000Hz 설정 지원
* 실사용 기본값은 1000Hz 또는 800Hz 우선 검토
* BMI270의 ODR/BW 설정은 **지터와 노이즈, aliasing, CPU 부하**를 함께 고려해 결정
* 저속 모드와 고속 모드를 프리셋으로 제공

### 11.2 FIFO/인터럽트 활용

* BMI270 FIFO 또는 DRDY 기반 구조 채택
* 폴링보다 인터럽트 기반 권장
* ISR에서는 최소 처리만 수행하고 task에서 burst read
* FIFO 오버런/워터마크 관리 필요

### 11.3 캘리브레이션

* 부팅 후 3초 정지 감지 시 자이로 바이어스 자동 추정
* 실패 시 재시도/건너뛰기 지원
* 수동 캘리브레이션 메뉴 제공
* 오프셋 값 저장/적용 구조 필요
* 온도에 따른 drift를 고려해 온도 기록 유지 권장

### 11.4 장착 방향 설정

* 사용자 프리셋 제공:

  * top-mounted
  * side-mounted-left
  * side-mounted-right
  * inverted
  * custom
* 각 orientation은 축 swap/sign/inversion 매트릭스로 관리
* 기록 원본(raw) + 논리 orientation 메타 저장 방식 권장

---

## 12. 전력 최적화 요구사항

### 12.1 저전력 모드 정책

* 장시간 미사용 시 light sleep 또는 deep sleep
* 버튼, USB 연결, BMI270 motion interrupt로 wakeup
* RTC alarm 기반 periodic wake 검토 가능

### 12.2 상태별 전력 정책

* READY: LED 저휘도, Wi-Fi off 기본
* RECORDING: CPU 고성능, Wi-Fi 제한, USB 기능 비활성
* WIFI_CONFIG: 샘플링 중단 또는 저속 제한
* SLEEP: WS2812 off, SD power gate 검토, IMU 저전력 motion detect

### 12.3 motion-based wakeup

* BMI270 내장 motion interrupt 기능을 사용해 움직임 감지 시 wake
* 자전거/드론/핸드헬드 사용에서는 false wake와 miss detection 균형 필요
* 웨이크 후 빠른 READY 복귀가 중요

### 12.4 배터리 보호

* 저전압 경고 → 로그 종료 유도 → 안전 flush → 종료
* 전압 강하 순간에도 헤더/파일시스템 손상이 없도록 hold-up time 확보 검토

---

## 13. USB / 데이터 추출 요구사항

### 13.1 USB MSC 모드

* PC 연결 시 장치를 USB 메모리처럼 인식시키는 방식 지원 검토
* 로깅 중에는 즉시 MSC 진입 금지
* 반드시 **record stop → flush → unmount → MSC expose** 순서 적용
* 호스트 연결 해제 후 파일시스템 remount 절차 필요

### 13.2 대체 접근

USB MSC가 운용상 복잡하면 다음 fallback 제공:

* USB CDC 명령으로 파일 export
* 웹 UI 다운로드
* SD 카드 직접 탈착

### 13.3 변환 유틸리티 전략

* 장치 내부 실시간 CSV 변환은 비권장
* PC 도구에서 `.bin → csv` 또는 `.bin → gyroflow compatible format` 변환
* 펌웨어와 함께 converter spec/version 관리 필요

---

## 14. Wi-Fi / 웹 설정 요구사항

### 14.1 운영 원칙

* Wi-Fi는 **기본 OFF**
* 설정이 필요할 때만 AP mode 또는 known STA 연결
* 로깅 실시간성에 영향이 없도록 recording 중 Wi-Fi 제한 또는 금지

### 14.2 웹 UI 기능

* 샘플링 속도 설정 (200/400/800/1000Hz)
* gyro range / accel range 설정
* LPF/BW 프리셋 설정
* orientation 설정
* 장치 이름 / 시간대 / 자동 슬립 시간 / LED 밝기 설정
* RTC/NTP 시간 동기화
* SD 사용량 / 파일 목록 / 펌웨어 버전 표시
* 로그 다운로드 / 삭제 / 포맷
* 캘리브레이션 실행

### 14.3 구현 전략

* AsyncWebServer 계열 사용 가능하나, 사용 라이브러리 유지보수성과 코어 호환성 검증 필요
* 정적 파일은 LittleFS/embedded assets에 저장
* REST API + 단순 SPA 구조 권장
* 설정은 JSON/NVS/LittleFS 기반 관리

---

## 15. UI/UX 요구사항

### 15.1 Senseflow A1 참조 확장 포인트

Senseflow A1의 핵심 UX 중 참고할 요소:

* 단순한 버튼 기반 탐색
* 현재 시간 / 저장상태 / 배터리 확인 용이성
* 자이로 기록 중심 모드 단순화
* SD 포맷/캘리브레이션/기본 설정 접근성

본 장치에서는 OLED 없이도 LED 중심 UX로 설계 가능하지만, 상용성 향상을 위해 소형 디스플레이 옵션도 검토할 수 있습니다.

### 15.2 LED 중심 UX 설계

LED는 상태를 “한눈에” 이해시키는 것이 목표입니다.

권장 표시 체계:

* 1번 LED: 시스템 상태
* 2번 LED: 기록 상태
* 3번 LED: 저장/오류 상태
* 4~N LED: 배터리 또는 모드 상태

### 15.3 버튼 동작 예시

* Power long: 전원 ON/OFF
* Record short: 녹화 시작/정지
* Mode short: 모드 순환
* Up/Down short: 값 조정
* Record long: Wi-Fi 설정 진입
* Mode long: 수동 캘리브레이션

---

## 16. 파일 포맷 요구사항

### 16.1 Binary log 포맷 권장 구조

```text
[FileHeader]
[MetaBlock]
[DataChunk 1]
[DataChunk 2]
...
[Footer / FinalIndex / CRC]
```

### 16.2 헤더 항목 예시

* magic
* format_version
* firmware_version
* device_id
* session_id
* rtc_start_unix
* monotonic_start_us
* sample_rate_hz
* gyro_range_dps
* accel_range_g
* orientation_mode
* calibration blob
* reserved

### 16.3 chunk 구조 예시

* chunk header
* first_sample_tick
* sample_count
* payload bytes
* chunk crc

### 16.4 복구 전략

* footer가 없더라도 chunk 단위로 최대한 salvage
* 마지막 유효 chunk까지만 복구 가능하도록 설계

---

## 17. 오류 처리 및 상용 수준 신뢰성 요구사항

### 17.1 오류 분류

* Recoverable

  * 일시적 SD busy
  * Wi-Fi 연결 실패
  * RTC 미동기화
* Semi-fatal

  * SD 없음
  * 캘리브레이션 실패
  * 낮은 저장공간
* Fatal

  * IMU 통신 실패 지속
  * 파일시스템 mount 실패 지속
  * 버퍼 오버런 지속
  * brownout 반복

### 17.2 보호 로직

* SD 쓰기 latency 감시
* 버퍼 수위 high watermark 감시
* FIFO overflow 감시
* watchdog 적용
* 오류 카운터 및 self-heal 재시도
* 치명 오류 시 안전 정지 후 LED 에러 표시

### 17.3 생산/서비스 관점 요구사항

* factory self-test mode
* SD benchmark mode
* IMU raw test mode
* RTC battery health check
* firmware version / board revision 표시
* 에러 로그 파일 별도 저장

---

## 18. 구현 최적화 전략

### 18.1 CPU / 메모리 최적화

* 실시간 로깅 경로에서 `String` 사용 최소화
* 동적 할당 최소화
* ISR 짧게 유지
* 큰 memcpy 횟수 최소화
* 패킷 구조체 정렬/packing 주의
* PSRAM은 대용량 보조 버퍼/웹자원 저장에 활용하되, 최고 실시간성 경로는 내부 RAM 우선

### 18.2 SD 성능 최적화

* contiguous pre-allocation
* large block sequential write
* dedicated SPI mode 검토
* flush/sync 주기 튜닝
* 카드별 latency benchmark 내장
* 조각난 카드보다 장치 포맷 후 사용 권장

### 18.3 동시성 최적화

* 기록 중 Wi-Fi 제한
* USB MSC와 recording 동시 금지
* LED 업데이트 빈도 제한
* UI 처리와 로깅 경로 분리

### 18.4 유지보수 최적화

* 모듈 간 인터페이스 명확화
* 단위 테스트 가능한 utility 분리
* 설정 구조 versioning
* 로깅 포맷 versioning
* PC converter와 포맷 문서 동기화

---

## 19. 권장 설정값(초기안)

### 19.1 기본 동작 프리셋

* **Normal Logging**

  * 400Hz
  * 표준 필터
  * 긴 배터리 시간
* **Gyroflow High Quality**

  * 1000Hz
  * gyro ±2000dps
  * accel ±16g
  * aggressive buffering
* **Low Power Motion**

  * motion wake only
  * 저속 대기
  * 기록 시에만 full speed

### 19.2 초기 부팅 정책

* 전원 인가
* self-test
* RTC 확인
* SD mount
* IMU init
* 3초 정지 감지 auto calibration
* READY 진입

---

## 20. 검증/시험 항목

### 20.1 기능 시험

* 버튼 입력 전부
* LED 상태 전부
* SD 삽입/미삽입/교체
* RTC 시간 유지
* Wi-Fi AP 접속
* 설정 저장/복원
* USB 연결/해제
* 파일 분할/종료/복구

### 20.2 성능 시험

* 1000Hz 30분/1시간/장시간 로깅
* SD worst-case latency 조건에서 drop 여부
* Wi-Fi 끈 상태/켜진 상태 비교
* LED 밝기 최대 시 전원 안정성
* 배터리 저전압 종료 시 데이터 보존

### 20.3 신뢰성 시험

* 강제 전원 차단
* SD 카드 제거 시나리오
* 진동/충격 환경
* 고온/저온 드리프트
* 반복 파일 생성/삭제/포맷

### 20.4 데이터 품질 시험

* 샘플 간격 jitter 측정
* drop/duplicate/overflow 통계
* orientation 적용 정확도
* calibration 전/후 drift 비교
* Gyroflow 실제 안정화 결과 비교

---

## 21. 권장 개발 단계

### Phase 1. Bring-up

* 전원/USB/버튼/LED/BMI270/DS3231/SD 개별 동작 확인

### Phase 2. Core Logging

* BMI270 취득 + binary logging + double buffer + pre-allocate 구현

### Phase 3. Reliability

* segment rotation, recovery, checksum, watchdog, latency monitor 구현

### Phase 4. UX/Power

* LED UX, sleep/wake, motion wake, battery 보호 구현

### Phase 5. Connectivity

* Wi-Fi config UI, time sync, USB MSC/CDC export 구현

### Phase 6. Production Hardening

* stress test, 호환성 테스트, 포맷 안정화, factory diagnostics 구현

---

## 22. 최종 권고안

이 장치는 단순한 “센서 읽기 보드”가 아니라, **고주파 실시간 데이터 로거 + 전원/파일 무결성 장치 + UX 제품**으로 접근해야 합니다.
따라서 구현 우선순위는 아래 순서가 적절합니다.

1. **1000Hz 안정 취득**
2. **버퍼링/SD 쓰기 무결성 확보**
3. **전원 차단 내성 및 자동 복구**
4. **상태 머신 기반 운용 안정화**
5. **LED/버튼 UX 정교화**
6. **Wi-Fi/USB/설정 기능 확장**

특히 상용 수준 완성도를 위해서는 다음 4가지를 사실상 필수로 보는 것이 좋습니다.

* **Double Buffering 이상 구조**
* **사전 할당(pre-allocation) + 순차 Binary Logging**
* **자동 파일 분할 + 복구 로직**
* **motion wake + 저전압 보호 + 안전 flush 종료**

---

## 23. 참고 사항

* ESP32-S3는 네이티브 USB device 기능과 sleep/wakeup 기능을 제공하며, USB MSC와 GPIO/light-sleep wakeup 구성이 가능합니다. ([docs.espressif.com](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_msc.html?utm_source=chatgpt.com))
* BMI270는 gyroscope ODR 최대 6.4kHz, accelerometer ODR 최대 1.6kHz와 on-chip motion-triggered interrupt 기능을 제공하므로 1000Hz 로깅과 motion wake 설계에 적합합니다. ([bosch-sensortec.com](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf?utm_source=chatgpt.com))
* SdFat는 contiguous file / preallocation 및 dedicated SPI mode 사용 시 성능 향상 이점을 명시하고 있습니다. ([github.com](https://github.com/greiman/SdFat?utm_source=chatgpt.com))

---

## 24. 다음 확장 가능 항목

* 카메라 셔터 동기 GPIO
* BLE remote control
* OLED/메모리 상태 표시
* GPS timestamp 보조 동기화
* 온도 기반 bias compensation
* PC용 bin→gyroflow 변환 도구
* 공장 보정/서비스 메뉴

