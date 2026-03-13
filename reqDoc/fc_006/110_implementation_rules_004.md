# 110_implementation_rules_004.md

# IMU Logger 프로젝트 최종 구현 / 명명 규칙 (보완판)

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
* 외부 라이브러리 추가 시 **라이선스 / 메모리 영향 / 유지보수성** 사전 검토

---

# 2. ArduinoJson 사용 규칙

허용 타입

```cpp
JsonDocument
```

금지 타입

```cpp
DynamicJsonDocument
StaticJsonDocument
```

금지 API

```cpp
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

### 역직렬화 오류 처리

```cpp
DeserializationError v_err = deserializeJson(v_doc, p_i_payload);
if (v_err)
{
    return false;
}
```

---

# 3. 초기화 규칙

기본 구조 초기화

```cpp
memset + strlcpy
```

예

```cpp
memset(&v_cfg, 0, sizeof(v_cfg));
strlcpy(v_cfg.name, "default", sizeof(v_cfg.name));
```

memset 사용 제한

* POD 구조체만 허용
* class 객체 사용 금지
* virtual 함수 포함 객체 금지
* STL 객체 금지

권장

* 구조체 초기화 전용 init 함수 제공
* 초기화 함수는 기본값 정책(샘플링 주기, 버퍼 크기)을 문서화

---

# 4. JSON 필드 규칙

원칙

* JSON key = 스키마 문서와 동일
* 구조체 멤버명 가능하면 JSON key와 동일
* 다른 경우 반드시 주석에 JSON key 명시
* JSON 숫자 필드는 항상 명시적 타입으로 변환
* 필수 키 누락 시 기본값 대입 또는 즉시 오류 반환 정책을 함수 주석에 명시

예

```cpp
uint16_t sample_rate_hz; // sample_rate_hz
uint16_t rate = v_doc["rate"].as<uint16_t>();
```

---

# 5. 모듈 규칙

모듈명 형식

```text
영문3자리 + 숫자2자리
```

예

```text
CFG10
LOG10
STR10
SDM10
UIF10
```

설명

* 앞 3자리 = 모듈 약어
* 숫자 2자리 = 모듈 계열 식별
* 신규 모듈 추가 시 기존 약어와 충돌 금지

---

# 6. 파일명 규칙

형식

```text
모듈명_기능_버전
```

예

```text
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
* 버전 증가 시 변경 이력(무엇이 바뀌었는지) 커밋 메시지에 명확히 기록

---

# 7. 사용자 타입 규칙

형식

```text
모듈명_이름_t
```

예

```text
CFG10_CONFIG_t
CFG10_RUNTIME_t
LOG10_CHUNK_t
LOG10_MARKER_t
STR10_FILEINFO_t
```

보완

* typedef struct 이름과 실제 사용 타입 이름은 동일 의도를 유지
* 약어 남용 금지(의미가 불명확한 1~2글자 명칭 지양)

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
* 외부 저장(파일/통신)에 사용되는 enum은 값 변경 금지

---

# 9. struct 규칙

```cpp
typedef struct
{
    uint16_t sample_rate_hz;
    uint8_t enable;
} CFG10_CONFIG_t;
```

규칙

* 구조체 필드는 하드웨어/저장 포맷 호환을 위해 고정폭 타입 우선 사용
* 직렬화 대상 구조체는 필드 순서 변경 시 호환성 영향 검토 필수

---

# 10. namespace 규칙

형식

```cpp
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
* `using namespace` 전역 사용 금지

---

# 11. 전역 네이밍

전역 상수

```text
G_모듈명_
```

전역 변수

```text
g_모듈명_
```

전역 함수

```text
모듈명_
```

예

```text
G_CFG10_MAX_CONFIG

g_CFG10_runtimeConfig

CFG10_loadConfig()
```

---

# 12. 클래스 규칙

클래스 형식

```text
CL_모듈명_이름
```

예

```text
CL_CFG10_ConfigManager
CL_LOG10_Logger
CL_STR10_FileManager
```

private 멤버

```text
_변수명
```

static 멤버

```text
변수명_s
```

보완

* 클래스는 단일 책임 원칙(SRP) 준수
* public API는 최소화하고, 내부 구현 상세는 private/protected로 캡슐화

---

# 13. 함수 변수 규칙

함수 인자 prefix

```text
p_i_   : 입력 parameter
p_o_   : 출력 parameter
p_io_  : 입력/출력 parameter
```

로컬 변수

```text
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
    return v_ok;
}
```

함수 규칙

* 모든 외부 입력 포인터는 **null check 우선 수행**
* 함수 시작 시 **guard clause 권장**
* pointer / reference는 반드시 유효성 검증
* 함수 길이 80~120라인 초과 시 기능 분리 검토

---

# 14. bool 변수 규칙

prefix

```text
is_
has_
can_
need_
```

예

```text
is_ready
has_error
can_save
need_flush
```

---

# 15. 정수 타입 규칙

금지

```text
int
long
```

권장

```text
uint8_t
uint16_t
uint32_t
int32_t
```

설명

* 하드웨어 / 파일 포맷 / 통신 구조체는 고정폭 타입 사용
* 부호 여부가 중요한 필드는 타입 선택 이유를 주석으로 보강

---

# 16. 파일 구조 규칙

원칙

```text
클래스 1개 = 파일 1개
```

예

```text
CFG10_CFG_001.h
CFG10_CFG_Core_001.cpp
CFG10_CFG_Load_001.cpp
CFG10_CFG_Save_001.cpp
```

설명

* 하나의 파일에는 하나의 주요 클래스만 둔다
* 헤더는 선언, 소스는 구현을 분리
* include 순서: 표준 라이브러리 → 서드파티 → 프로젝트 헤더

---

# 17. 메모리 규칙

금지

```text
hot path malloc/free
new/delete 금지
String 사용 금지 (hot path)
```

권장

```text
RingBuffer
BufferPool
```

설명

* 고속 로깅 경로에서는 동적 할당 금지
* ISR/실시간 경로에서는 lock 경합 최소화
* 대용량 버퍼는 정적/사전할당 방식 우선

---

# 18. 오류 처리

반환 방식

```text
bool
error enum
```

fatal 오류 전달

```text
SystemEventQueue
```

보완

* 오류 로그는 모듈 prefix + 오류코드 + 핵심 문맥(함수명/라인/키 값) 포함
* 복구 가능한 오류와 치명 오류를 구분하여 처리

---

# 19. 로그 규칙

로그 prefix

```text
[모듈명]
```

예

```text
[CFG10] load config
[LOG10] write chunk
[STR10] open file
```

보완

* loop에서 과도한 로그 출력 금지(샘플링 또는 rate limit 적용)
* 배포 빌드에서 디버그 로그 레벨 제어 가능해야 함

---

# 20. 동시성/태스크 규칙

* 태스크 간 공유 데이터는 소유권(Owner) 모듈을 명확히 정의
* 큐 메시지 구조체는 고정 크기 사용을 우선 검토
* mutex 사용 시 잠금 범위를 최소화하고 중첩 잠금 금지
* ISR에서 블로킹 API 호출 금지

---

# 21. 코드 리뷰 체크리스트

* 네이밍 규칙 위반 여부 확인
* JSON 파싱 실패/누락 키 처리 여부 확인
* hot path 동적 메모리 사용 여부 확인
* 경계값(0, 최대값, 비정상 입력) 처리 여부 확인
* 로그 레벨/출력량이 운영 환경에 적절한지 확인

---

# 22. 문서/변경 이력 규칙

* 규칙 문서를 변경할 때는 파일 버전 증가(예: `_003` → `_004`) 적용
* 변경 사유와 영향 범위를 커밋 메시지/PR 본문에 명시
* 하위 호환성에 영향이 있는 규칙 변경은 예시 코드 포함
