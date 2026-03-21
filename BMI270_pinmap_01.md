제공해주신 이미지는 Bosch BMI270 6축 가속도/자이로 센서 모듈(V1.3)입니다.  
이 모듈은 I2C와 SPI 통신을 모두 지원하며,  
ESP32-S3와 연결하여 정밀한 모션 트래킹을 구현할 수 있습니다.
요청하신 핀 맵 정리와 ESP32-S3 결선 방법을 상세히 설명해 드리겠습니다.   

# 1. BMI270 모듈 핀 기능 (Pinout)

| 핀 이름 | 기능 | 상세 설명 |
|---|---|---|
| VCC | 전원 입력 | 보통 3.3V 연결 (모듈에 레귤레이터가 있는 경우 5V도 가능하나 3.3V 권장) |
| GND | 그라운드 | ESP32-S3의 GND에 연결 |
| AD0 / MISO | I2C 주소 설정 / SPI 데이터 출력 | I2C: High(VCC)면 0x69, Low(GND)면 0x68 주소 사용 / SPI: MISO |
| SDA / MOSI | I2C 데이터 / SPI 데이터 입력 | I2C: 데이터선 / SPI: MOSI |
| SCL / SCLK | I2C 클럭 / SPI 클럭 | I2C: 클럭선 / SPI: Serial Clock |
| CS | Chip Select | I2C 사용 시: VCC에 연결 (I2C 모드 활성화) / SPI 사용 시: CS 제어 핀 |
| INT1 / INT2 | 인터럽트 출력 | 특정 동작(충격, 자유낙하 등) 감지 시 ESP32에 신호를 보냄 (옵션) |
| SDX / SCX | 보조(Aux) I2C | 외부 자기계(Magnetometer) 등을 추가로 연결할 때 사용 (일반적으로 비워둠) |

# 2. ESP32-S3 결선 방법 (통신 방식별)

ESP32-S3는 핀 매핑이 자유롭지만,   
일반적으로 사용되는 기본 핀(Default GPIO)을 기준으로 설명드립니다.

  ## A. I2C 통신 방식 (가장 권장)
배선이 간단하여 가장 많이 사용됩니다.
| BMI270 핀 | ESP32-S3 핀 | 비고 |
|---|---|---|
| VCC | 3.3V |  |
| GND | GND |  |
| SCL | GPIO 9 (SCL) | ESP32-S3 기본 I2C 클럭 |
| SDA | GPIO 8 (SDA) | ESP32-S3 기본 I2C 데이터 |
| AD0 | GND (또는 VCC) | GND 연결 시 주소 0x68 |
| CS | 3.3V (VCC) | 필수: I2C 모드로 작동시키기 위해 High 유지 |

  ## B. SPI 통신 방식
고속 데이터 샘플링이 필요할 때 사용합니다.
| BMI270 핀 | ESP32-S3 핀 (FSPI) | 비고 |
|---|---|---|
| VCC | 3.3V |  |
| GND | GND |  |
| SCLK | GPIO 12 (SCK) |  |
| MISO | GPIO 13 (MISO) |  |
| MOSI | GPIO 11 (MOSI) |  |
| CS | GPIO 10 (SS) | 사용자가 원하는 임의의 GPIO 가능 |

# 3. PlatformIO 설정 및 라이브러리 (Arduino Core)
platformio.ini 설정과 함께 ArduinoJson 7.4.x를 포함한 라이브러리 설정을 예시로 적어드립니다.

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps =
    # BMI270 라이브러리 (Bosch 공식 또는 추천 라이브러리)
    finani/BMI270_Sensor @ ^1.0.1
    bblanchon/ArduinoJson @ ~7.4.0
```

**주의사항**
 * 전압 레벨: BMI270 칩 자체는 1.8V~3.6V 사이에서 작동합니다.   
 * ESP32-S3의 로직 레벨(3.3V)과 호환되므로 직접 연결해도 무방합니다.
 * I2C 풀업 저항: 모듈에 이미 저항이 달려 있는 경우가 많으나, 통신 에러가 발생하면 SDA/SCL 라인에 4.7kΩ 저항을 3.3V에 연결해 보세요.
 * CS 핀: I2C 사용 시 CS 핀을 플로팅(연결 안 함) 상태로 두면 SPI 모드로 오인할 수 있으므로 반드시 VCC에 묶어주세요.

---
# 1. ESP32-S3-Zero & BMI270 SPI 결선표

| BMI270 모듈 핀 | ESP32-S3-Zero 핀 | 기능 (SPI 모드) | 코드 내 변수명 |
|---|---|---|---|
| VCC | 3.3V | 전원 공급 | - |
| GND | GND | 그라운드 | - |
| CS | GPIO 10 | Chip Select (Active Low) | BMI_CS |
| SCL / SCLK | GPIO 12 | SPI Clock | BMI_SCK |
| SDA / MOSI | GPIO 11 | Master Out Slave In | BMI_MOSI |
| AD0 / MISO | GPIO 13 | Master In Slave Out | BMI_MISO |
| INT1 | GPIO 9 | 하드웨어 인터럽트 | BMI_INT1 |

# 2. 하드웨어 구성 시 필수 체크리스트
⚠️ I2C/SPI 자동 전환 주의
대부분의 BMI270 브레이크아웃 보드는 CS 핀의 상태를 보고 통신 방식을 결정합니다.
 * SPI 모드: MCU의 BMI_CS 핀(GPIO 10)이 BMI270의 CS 핀에 연결되어야 합니다.
 * 코드에서 digitalWrite(BMI_CS, HIGH);로 초기화한 후, 통신할 때만 LOW로 떨어뜨려야 합니다. (라이브러리가 자동으로 처리함)
⚠️ INT1 핀 설정
BMI_INT1로 설정하신 GPIO 9는 ESP32-S3에서 일반적인 입력 핀으로 사용하기 적합합니다. 다만, 센서 초기화 코드에서 다음과 같이 설정되어 있는지 확인하세요.
 * Push-Pull 모드: 센서가 전압을 직접 밀어주므로 ESP32에서 별도의 풀업 저항이 필요 없습니다. (pinMode(BMI_INT1, INPUT))
 * Open-Drain 모드: 만약 센서 설정이 Open-drain이라면 pinMode(BMI_INT1, INPUT_PULLUP)으로 설정해야 신호가 튑니다.
# 3. PlatformIO / Arduino 환경 코드 팁
 SPI 객체를 초기화할 때 아래와 같이 사용하시면 됩니다.

```cpp
#include <SPI.h>

void setup() {
    // ESP32-S3는 SPI 핀을 임의 지정할 수 있습니다.
    // SPI.begin(SCK, MISO, MOSI, CS);
    SPI.begin(BMI_SCK, BMI_MISO, BMI_MOSI, BMI_CS);
    
    // 인터럽트 핀 설정
    pinMode(BMI_INT1, INPUT); 
    // attachInterrupt(digitalPinToInterrupt(BMI_INT1), yourInterruptHandler, RISING);
}
```


---

BMI270 센서에서 INT1과 INT2는 센서 내부에서 발생한 특정 이벤트(인터럽트)를   
MCU(ESP32-S3)에게 알리기 위한 출력 신호 핀입니다.   
두 핀의 하드웨어적 구조는 동일하며, 소프트웨어 설정을 통해 용도를 자유롭게 지정할 수 있습니다.   
주요 차이점과 상세 용도는 다음과 같습니다.

# 1. INT1 vs INT2: 무엇이 다른가?
결론부터 말씀드리면, **"기능상의 차이는 없으며, 효율적인 자원 관리를 위해 두 개가 존재"**합니다.
 * 매핑의 자유도: BMI270 내부의 모든 인터럽트 엔진(걸음 수 계산, 충격 감지, 데이터 준비 완료 등)은 INT1이나 INT2 중 원하는 곳으로, 혹은 양쪽 모두로 출력되도록 설정할 수 있습니다.
 * 우선순위 분리: 중요한 신호(예: 충격 감지)는 INT1에, 일반적인 신호(예: 데이터 준비 완료)는 INT2에 할당하여 MCU가 신호의 출처를 즉각 파악하게 할 수 있습니다.
 * 입력 기능 (동기화): 드문 경우지만, 외부 클럭 신호를 센서로 입력받아 데이터 샘플링 시간을 맞추는 '입력 핀'으로도 설정 가능합니다.

# 2. 주요 용도 (언제 사용하는가?)
BMI270은 전력 소모를 줄이기 위해 **"이벤트 기반 방식"**을 권장합니다.   
MCU가 계속 센서 데이터를 읽는 대신, 센서가 감지했을 때만 MCU를 깨우는 식입니다.
대표적인 활용 사례
 * Any-motion / No-motion (움직임 감지): 기기가 움직이기 시작할 때 INT1을 통해 MCU에 신호를 주어 시스템을 깨웁니다(Wake-up). 반대로 움직임이 멈추면 슬립 모드로 들어갑니다.
 * Step Counter (만보기): 걸음이 발생할 때마다 인터럽트를 발생시켜 정밀한 걸음 수를 기록합니다.
 * Data Ready (drdy): 가속도/자이로 데이터의 새로운 샘플링이 완료될 때마다 신호를 줍니다. 고속 데이터 처리를 할 때 루프(Loop) 문보다 훨씬 안정적입니다.
 * Tap / Double Tap: 화면이나 케이스를 톡톡 두드리는 동작을 감지하여 특정 기능을 실행합니다.


# 3. ESP32-S3 연결 및 활용 팁
ESP32-S3와 연결할 때는 보통 하나만 연결해도 충분하지만, 복합적인 기능을 구현할 때는 다음과 같이 구성합니다.
| 시나리오 | INT1 연결 | INT2 연결 |
|---|---|---|
| 단순 데이터 읽기 | 연결 안 함 (Polling 방식) | 연결 안 함 |
| 저전력 모니터링 | GPIO에 연결 (Wake-up용) | 연결 안 함 |
| 고성능 모션 분석 | GPIO 1 (데이터 준비 완료용) | GPIO 2 (충격/오류 감지용) |
💡 주의사항
 * 전압 레벨: BMI270 모듈은 3.3V 로직을 사용하므로 ESP32-S3와 직접 연결해도 안전합니다.
 * Active Level: 기본적으로 'Active High'로 설정되어 있으나, 소프트웨어에서 'Active Low'로 변경 가능합니다. MCU의 인터럽트 설정(RISING / FALLING)과 맞춰야 합니다.
 * Floating 방지: 센서 내부에서 Push-pull 또는 Open-drain 방식을 설정할 수 있습니다. 일반적으로 Push-pull 방식을 사용하면 별도의 풀업 저항이 필요 없습니다.

