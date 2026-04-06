#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>           // ESP32 NVS 라이브러리
#include <SparkFun_BMI270_Arduino_Library.h>

// 객체 선언
BMI270      _imu;
Preferences _prefs;

// NVS 네임스페이스 및 키 정의
const char* G_B10_PREFS_CAL_NAMESPACE = "imu_cal";
const char* G_B10_PREFS_CAL_KEY       = "offset_data";

// 보정값 저장 구조체 (float로 저장하여 가독성 확보)
struct ST_imu_calOffset_t {
    float accX, accY, accZ;
    float gyrX, gyrY, gyrZ;
    uint32_t lastCalTime; // 마지막 교정 시간
};

// --- [공통 함수 1: NVS에서 보정값 로드 및 적용] ---
bool loadAndApplyOffsets() {
    
    ST_imu_calOffset_t   v_offsets;
    _prefs.begin(G_B10_PREFS_CAL_NAMESPACE, true); // Read-only 모드
    
    size_t res = _prefs.getBytes(G_B10_PREFS_CAL_KEY, &v_offsets, sizeof(ST_imu_calOffset_t));
    _prefs.end();

    if (res == sizeof(ST_imu_calOffset_t)) {
        Serial.printf("[NVS] 보정값 로드 성공 (마지막 교정: %u)\n", v_offsets.lastCalTime);
        // 라이브러리 API를 통해 센서에 오프셋 적용
        // 주의: SparkFun 라이브러리의 내부 오프셋 적용 방식에 따라 _imu.setOffsets() 호출
        // (현재 라이브러리 구조상 직접적인 set 메서드가 없다면 수동 차감 로직 활용 가능)
        return true;
    }
    Serial.println("[NVS] 저장된 보정값이 없습니다.");
    return false;
}

// --- [공통 함수 2: 센서 교정 수행 및 NVS 저장] ---
void runCalibrationAndSave() {
    Serial.println("\n[CALIB] 캘리브레이션을 시작합니다. 기기를 수평으로 고정하세요...");
    delay(2000);

    // 1. CRT (자이로 정밀 보정 - 하드웨어 최적화)
    _imu.performComponentRetrim();
    
    // 2. 오프셋 계산 (Z축 상단 기준)
    _imu.performAccelOffsetCalibration(BMI2_GRAVITY_POS_Z);
    _imu.performGyroOffsetCalibration();

    // 3. 교정된 결과값 추출 (50샘플 평균)
    ST_imu_calOffset_t newOffsets = {0};
    int samples = 100;
    for(int i=0; i<samples; i++) {
        _imu.getSensorData();
        newOffsets.accX += _imu.data.accelX;
        newOffsets.accY += _imu.data.accelY;
        newOffsets.accZ += (_imu.data.accelZ - 1.0f); // 1G 제외한 오차값
        newOffsets.gyrX += _imu.data.gyroX;
        newOffsets.gyrY += _imu.data.gyroY;
        newOffsets.gyrZ += _imu.data.gyroZ;
        delay(5);
    }
    newOffsets.accX /= samples; newOffsets.accY /= samples; newOffsets.accZ /= samples;
    newOffsets.gyrX /= samples; newOffsets.gyrY /= samples; newOffsets.gyrZ /= samples;
    newOffsets.lastCalTime = millis();

    // 4. ESP32 NVS에 저장 (BMI270 NVM은 건드리지 않음)
    _prefs.begin(G_B10_PREFS_CAL_NAMESPACE, false); // Read-Write 모드
    _prefs.putBytes(G_B10_PREFS_CAL_KEY, &newOffsets, sizeof(ST_imu_calOffset_t));
    _prefs.end();

    Serial.println("[NVS] 새로운 보정값이 플래시에 저장되었습니다.");
}

// --- [Main Setup] ---
void setup() {
    Serial.begin(115200);
    Wire.begin();

    // 1. 센서 초기화
    if (_imu.beginI2C() != BMI2_OK) {
        Serial.println("BMI270 연결 실패!");
        while(1);
    }

    // 2. 부팅 시 NVS에서 보정값 로드 시도
    if (!loadAndApplyOffsets()) {
        // 저장된 값이 없으면 최초 1회 교정 수행
        runCalibrationAndSave();
    }

    Serial.println("시스템 준비 완료. 'c'를 입력하면 재교정을 수행합니다.");
}

void loop() {
    // 사용자가 'c'를 입력하면 언제든 재교정 가능 (NVM 횟수 제한 없음)
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'c') {
            runCalibrationAndSave();
        }
    }

    // 실제 데이터 출력 (1000Hz 로깅 루프 가정)
    _imu.getSensorData();
    // 여기서 NVS에서 로드한 오프셋 값을 차감하여 정밀 데이터 사용 가능
}


