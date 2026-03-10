

# 002_implementation_rules_003.md

# IMU Logger 프로젝트 최종 구현 / 명명 규칙

본 문서는 프로젝트 전반에 적용되는 **코딩 규칙 / 네이밍 규칙 / JSON 규칙 / 모듈 규칙 / 아키텍처 규칙**의 최종 표준을 정의한다.

모든 소스 코드는 반드시 본 규칙을 따른다.

---

# 1. 기본 구현 규칙

* ESPAsyncWebServer 사용

[https://github.com/ESP32Async/ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)

필수 규칙

* 항상 **소스 시작 주석 블록 구조 유지**
* 구현 규칙 / 네이밍 규칙 **수정 금지**
* ArduinoJson **v7.x.x만 사용**
* ArduinoJson v6 이하 사용 금지

---

# 2. ArduinoJson 사용 규칙

허용 타입

```
JsonDocument
```

금지 타입

```
DynamicJsonDocument
StaticJsonDocument
```

금지 API

```
createNestedArray
createNestedObject
containsKey
```

## 대체 패턴

### Key 존재 확인

```cpp
JsonVariant v_value = v_doc["wifi"]["ssid"];
if (!v_value.isNull())
{
}
```

### Nested Object 접근

```cpp
JsonObject v_wifi = v_doc["wifi"].to<JsonObject>();
JsonObject v_ap = v_wifi["ap"].to<JsonObject>();
```

### Array 접근

```cpp
JsonArray v_items = v_doc["items"].to<JsonArray>();
```

---

# 3. 초기화 규칙

기본 구조 초기화

```
memset + strlcpy
```

예

```cpp
memset(&v_cfg,0,sizeof(v_cfg));
strlcpy(v_cfg.name,"default",sizeof(v_cfg.name));
```

memset 사용 제한

* POD 구조체만 허용
* class 객체 사용 금지
* virtual 함수 포함 객체 금지
* STL 객체 금지

권장

* 구조체 초기화 전용 init 함수 제공

---

# 4. JSON 필드 규칙

원칙

* JSON key = 스키마 문서와 동일
* 구조체 멤버명 가능하면 JSON key와 동일
* 다른 경우 반드시 주석에 JSON key 명시
* JSON 숫자 필드는 항상 명시적 타입으로 변환

예

```cpp
uint16_t sample_rate_hz; // sample_rate_hz
uint16_t rate = v_doc["rate"].as<uint16_t>();
```

---

# 5. 모듈 규칙

모듈명 형식

```
영문3자리 + 숫자2자리
```

예

```
CFG10
LOG10
STR10
SDM10
UIF10
```

설명

* 앞 3자리 = 모듈 약어
* 숫자 2자리 = 모듈 계열 식별

---

# 6. 파일명 규칙

형식

```
모듈명_기능_버전
```

예

```
CFG10_CFG_001.h
CFG10_CFG_Core_001.cpp
CFG10_CFG_Load_001.cpp
CFG10_CFG_Save_001.cpp

LOG10_LOG_001.h
```

설명

* 모듈명 = 기능 영역
* 기능 = 세부 기능
* 버전 = 파일 버전

---

# 7. 사용자 타입 규칙

형식

```
모듈명_이름_t
```

예

```
CFG10_CONFIG_t
CFG10_RUNTIME_t
LOG10_CHUNK_t
LOG10_MARKER_t
STR10_FILEINFO_t
```

---

# 8. enum 규칙

형식

```cpp
typedef enum
{
    EN_CFG10_STATE_IDLE = 0,
    EN_CFG10_STATE_RUN  = 1
} CFG10_STATE_t;
```

규칙

* enum 상수 prefix = `EN_모듈명_`
* enum 타입명 = `모듈명_이름_t`
* 모든 enum 값은 명시적으로 지정

---

# 9. struct 규칙

```cpp
typedef struct
{
    uint16_t sample_rate_hz;
    uint8_t enable;

} ST_CFG10_CONFIG_t;
```

---

# 10. namespace 규칙

형식

```
namespace 모듈명
```

예

```cpp
namespace CFG10
{
    constexpr uint32_t kDefaultTimeout = 1000;
}
```

규칙

* namespace 내부 상수는 prefix 생략
* 외부 API에는 namespace prefix 사용

---

# 11. 전역 네이밍

전역 상수

```
G_모듈명_
```

전역 변수

```
g_모듈명_
```

전역 함수

```
모듈명_
```

예

```
G_CFG10_MAX_CONFIG

g_CFG10_runtimeConfig

CFG10_loadConfig()
```

---

# 12. 클래스 규칙

클래스 형식

```
CL_모듈명_이름
```

예

```
CL_CFG10_ConfigManager
CL_LOG10_Logger
CL_STR10_FileManager
```

private 멤버

```
_변수명
```

static 멤버

```
변수명_s
```

---

# 13. 함수 변수 규칙

함수 인자 prefix

```
p_i_   : 입력 parameter
p_o_   : 출력 parameter
p_io_  : 입력/출력 parameter
```

로컬 변수

```
v_
```

예

```cpp
bool CFG10_loadConfig(const char* p_i_path, CFG10_CONFIG_t* p_o_cfg)
{
    bool v_ok = false;

    if (p_i_path == nullptr || p_o_cfg == nullptr)
    {
        return false;
    }

    // guard clause
}
```

함수 규칙

* 모든 외부 입력 포인터는 **null check 우선 수행**
* 함수 시작 시 **guard clause 권장**
* pointer / reference는 반드시 유효성 검증

---


# 14. bool 변수 규칙

prefix

```
is_
has_
can_
need_
```

예

```
is_ready
has_error
can_save
need_flush
```

---

# 15. 정수 타입 규칙

금지

```
int
long
```

권장

```
uint8_t
uint16_t
uint32_t
int32_t
```

설명

* 하드웨어 / 파일 포맷 / 통신 구조체는 고정폭 타입 사용

---

# 16. 파일 구조 규칙

원칙

```
클래스 1개 = 파일 1개
```

예

```
CFG10_CFG_001.h
CFG10_CFG_Core_001.cpp
CFG10_CFG_Load_001.cpp
CFG10_CFG_Save_001.cpp
```

설명

* 하나의 파일에는 하나의 주요 클래스만 둔다

---

# 17. 메모리 규칙

금지

```
hot path malloc/free
new/delete 금지
String 사용 금지 (hot path)
```

권장

```
RingBuffer
BufferPool
```

설명

* 고속 로깅 경로에서는 동적 할당 금지

---

# 18. 오류 처리

반환 방식

```
bool
error enum
```

fatal 오류 전달

```
SystemEventQueue
```

---

# 19. 로그 규칙

로그 prefix

```
[모듈명]
```

예

```
[CFG10] load config
[LOG10] write chunk
[STR10] open file
```

---

# 20. Config 접근 규칙

직접 접근 금지

```
config_*.json
```

반드시

```
ConfigManager
```

---

# 21. ISR 규칙

ISR에서 금지

```
malloc
SD write
SPI long transaction
JSON parsing
state transition
ISR에서 로그 출력 금지
```

ISR 허용

```
flag set
task notify
queue push
```

---

# 22. 아키텍처 핵심 규칙

```
raw sample → ring buffer
control event → queue
write ownership → SdWriteTask
state transition → SystemTask
config access → ConfigManager
```

---

# 23. 매크로 사용 규칙

매크로 최소화

원칙

```
constexpr 우선
```

허용

```
헤더 가드
전처리 조건
특수 목적 매크로
```

금지

```
단순 상수 매크로
```

예

```cpp
constexpr uint32_t kMaxBufferSize = 4096;
```

---

# 24. 최종 원칙

```
1. hot path dynamic allocation 금지
2. single ownership resource
3. JSON 구조 = 코드 구조
4. 상태 전이 단일 모듈
5. 로그 파이프라인 분리
```

---

---

# 25. Thread Safety 규칙

FreeRTOS 환경에서 다중 Task가 동일 자원을 접근할 수 있으므로  
공유 자원 접근 시 반드시 thread safety 규칙을 따른다.

## 기본 원칙

shared resource 접근 시 **mutex 사용**

예

- SD card
- ConfigManager
- Logger 상태
- FileManager
- Shared buffer

## 권장 정책

- 동일 자원에 대한 **single ownership 설계 우선 적용**
- 여러 Task가 접근해야 하는 경우 **mutex 보호 필수**
- mutex 보호 범위는 **최소화**
- 긴 작업은 mutex 내부에서 수행하지 않는다

## 예

```cpp
xSemaphoreTake(g_LOG10_sdMutex, portMAX_DELAY);

// shared resource 접근

xSemaphoreGive(g_LOG10_sdMutex);
```

## 금지
- mutex 없이 shared resource 접근
- ISR에서 mutex 사용
- nested mutex 남용
## 권장 아키텍처
- SD write ownership → SdWriteTask
- state transition ownership → SystemTask
- config access ownership → ConfigManager
- 
가능한 경우 mutex보다 single owner task 구조를 우선 적용한다.
