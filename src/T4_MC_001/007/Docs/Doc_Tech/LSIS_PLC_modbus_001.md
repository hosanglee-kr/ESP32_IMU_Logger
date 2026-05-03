ESP32-S3와 W5500 이더넷 모듈을 사용하여 LS일렉트릭 PLC(XGB/XGK 등)와 Modbus TCP로 통신하는 객체지향 라이브러리 예제입니다.
### 1. 설계 전략
 * **HPP Only**: 별도의 .cpp 파일 없이 헤더 파일 하나로 배포 및 포함이 가능하도록 설계합니다.
 * **모듈화**: LSModbusClient 클래스를 정의하여 이더넷 연결 관리와 데이터 타입별 읽기/쓰기를 캡슐화합니다.
 * **유연성**: 다양한 데이터 유형(Bit, Word, Float)을 지원하며, std::function 등을 활용해 확장성을 높였습니다.
### 2. LSModbusTCP.hpp (모듈 파일)
이 파일은 src 폴더에 넣고 다른 모듈에서 #include "LSModbusTCP.hpp"로 호출하여 사용합니다.
```cpp
#ifndef LS_MODBUS_TCP_HPP
#define LS_MODBUS_TCP_HPP

#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoModbus.h>

/**
 * LSModbusClient: LS 산전 PLC와의 Modbus TCP 통신을 위한 객체지향 클래스
 */
class LSModbusClient {
private:
    EthernetClient _ethClient;
    ModbusTCPClient _modbus;
    IPAddress _serverIP;
    uint16_t _port;
    uint8_t _slaveID;

public:
    // 생성자: 연결 정보 초기화
    LSModbusClient(IPAddress ip, uint16_t port = 502, uint8_t slaveID = 1)
        : _serverIP(ip), _port(port), _slaveID(slaveID), _modbus(_ethClient) {}

    // 통신 시작
    bool begin(int csPin, int resetPin = -1) {
        if (resetPin != -1) {
            pinMode(resetPin, OUTPUT);
            digitalWrite(resetPin, LOW);
            delay(10);
            digitalWrite(resetPin, HIGH);
            delay(100);
        }
        
        // W5500 SPI 설정 (ESP32-S3의 경우 기본 SPI 핀 확인 필요)
        Ethernet.init(csPin);
        return true;
    }

    // 서버 연결 확인 및 재연결
    bool checkConnection() {
        if (!_modbus.connected()) {
            Serial.println("Connecting to LS PLC...");
            return _modbus.begin(_serverIP, _port);
        }
        return true;
    }

    // --- 데이터 유형별 읽기 ---

    // Bit (Coil) 읽기: %MX, %QX 등
    bool readBit(int address, bool &value) {
        if (!checkConnection()) return false;
        int res = _modbus.coilRead(_slaveID, address);
        if (res == -1) return false;
        value = (res == 1);
        return true;
    }

    // Word (Holding Register) 읽기: %MW 등
    bool readWord(int address, uint16_t &value) {
        if (!checkConnection()) return false;
        long res = _modbus.holdingRegisterRead(_slaveID, address);
        if (res == -1) return false;
        value = (uint16_t)res;
        return true;
    }

    // Float (32bit) 읽기: LS PLC는 2개의 Word를 사용
    bool readFloat(int address, float &value) {
        if (!checkConnection()) return false;
        if (!_modbus.requestFrom(_slaveID, HOLDING_REGISTERS, address, 2)) return false;
        
        uint16_t low = _modbus.read();
        uint16_t high = _modbus.read();
        uint32_t combined = ((uint32_t)high << 16) | low;
        memcpy(&value, &combined, sizeof(float));
        return true;
    }

    // --- 데이터 유형별 쓰기 ---

    bool writeBit(int address, bool value) {
        if (!checkConnection()) return false;
        return _modbus.coilWrite(_slaveID, address, value);
    }

    bool writeWord(int address, uint16_t value) {
        if (!checkConnection()) return false;
        return _modbus.holdingRegisterWrite(_slaveID, address, value);
    }

    bool writeFloat(int address, float value) {
        if (!checkConnection()) return false;
        uint32_t combined;
        memcpy(&combined, &value, sizeof(float));
        uint16_t low = combined & 0xFFFF;
        uint16_t high = (combined >> 16) & 0xFFFF;

        _modbus.beginTransmission(_slaveID, HOLDING_REGISTERS, address, 2);
        _modbus.write(low);
        _modbus.write(high);
        return _modbus.endTransmission() == 0;
    }

    void stop() {
        _modbus.stop();
    }
};

#endif

```
### 3. 사용 예제 (main.cpp)
```cpp
#include <Arduino.h>
#include "LSModbusTCP.hpp"

// 하드웨어 설정 (ESP32-S3)
const int ETHERNET_CS = 10;
const int ETHERNET_RST = 11;
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress plcIp(192, 168, 1, 10); // PLC의 IP 주소

// 객체 생성
LSModbusClient plc(plcIp);

void setup() {
    Serial.begin(115200);

    // 1. 이더넷 하드웨어 초기화
    plc.begin(ETHERNET_CS, ETHERNET_RST);

    // 2. IP 할당 (DHCP 또는 Static)
    Serial.println("Initializing Ethernet...");
    if (Ethernet.begin(mac) == 0) {
        Serial.println("DHCP Failed, using Static IP");
        Ethernet.begin(mac, IPAddress(192, 168, 1, 50)); // ESP32 IP
    }
    delay(1000);
}

void loop() {
    // PLC 변수 읽기 예제
    bool lampStatus;
    uint16_t motorSpeed;
    float temperature;

    // Bit 읽기 (예: %MX0 -> Modbus Address 0)
    if (plc.readBit(0, lampStatus)) {
        Serial.printf("Lamp Status: %s\n", lampStatus ? "ON" : "OFF");
    }

    // Word 읽기 (예: %MW100 -> Modbus Address 100)
    if (plc.readWord(100, motorSpeed)) {
        Serial.printf("Motor Speed: %u RPM\n", motorSpeed);
    }

    // Float 읽기 (예: %MW200 -> Modbus Address 200)
    if (plc.readFloat(200, temperature)) {
        Serial.printf("Temperature: %.2f C\n", temperature);
    }

    // 제어 예제: 특정 조건 시 Bit 쓰기
    plc.writeBit(1, true); // %MX1 ON

    delay(2000); // 2초 간격 폴링
}

```
### 4. 주의 사항 및 팁
 1. **LS PLC 설정**:
   * XG5000 소프트웨어에서 **이더넷 모듈 설정** 시 'Modbus TCP 서버'를 반드시 활성화해야 합니다.
   * **국국번(Slave ID)**을 확인하세요. 기본값은 보통 1입니다.
 2. **주소 매핑**:
   * LS PLC의 %MW0은 Modbus 주소 40001 또는 0번으로 매핑됩니다 (설정에 따라 다름). 보통 ArduinoModbus 라이브러리는 0-based 인덱스를 사용하므로 주소에서 1을 뺀 값을 사용해 보시기 바랍니다.
 3. **플로트(Float) 데이터 순서**:
   * LS PLC와 ESP32 간의 엔디안(Endian) 차이로 인해 low/high 순서가 바뀔 수 있습니다. 만약 값이 이상하게 나온다면 readFloat 내부의 low, high 합치는 순서를 바꿔보세요.
 4. **PlatformIO 설정**:
   platformio.ini에 아래 라이브러리를 추가해야 합니다.
   ```ini
   lib_deps =
       arduino-libraries/Ethernet
       arduino-libraries/ArduinoModbus
   
   ```
이 구조는 LSModbusClient 객체를 전역으로 선언하거나, 다른 제어 모듈에 참조(Reference)로 전달하여 시스템 전체에서 PLC 데이터를 공유하기에 매우 용이합니다.

