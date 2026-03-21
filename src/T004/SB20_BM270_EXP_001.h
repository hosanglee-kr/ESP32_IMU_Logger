
#include <BMI270_Sensor.h> // 사용 중인 라이브러리에 맞게 수정


// 장착 방향 공통 함수화
// 장착 각도(회전 및 기울기)를 입력받아 내부적으로 bmi2_remap 구조체를 설정하는 함수입니다. 90도 단위의 직교 좌표계 회전을 처리합니다

#include <Arduino.h>
#include "BMI270_Sensor.h" // 사용하시는 라이브러리에 맞게 수정

// 장착 상태를 정의하는 구조체
struct MountingConfig {
    int yaw_deg;   // 정면 기준 좌우 회전 (0, 90, 180, 270)
    int pitch_deg; // 앞뒤 기울기 (0, 90, 180, 270)
};

void setImuOrientation(BMI270 &imu, MountingConfig config) {
    bmi2_remap axes;
    
    // 기본값 (정방향 장착 가정)
    axes.x = BMI2_AXIS_POS_X;
    axes.y = BMI2_AXIS_POS_Y;
    axes.z = BMI2_AXIS_POS_Z;

    // 실제 프로젝트의 기구적 배치에 따라 
    // 입력받은 yaw_deg와 pitch_deg를 조합하여 axes를 매핑하는 로직을 수행합니다.
    // 아래 예시는 단순히 파라미터화된 인터페이스의 예시입니다.
    
    // TODO: 프로젝트의 기준 좌표계(Base Frame)에 맞춰 
    // 회전 행렬 연산 결과를 아래 매핑 값에 대입합니다.
    // 예: 90도 회전 시 axes.x = BMI2_AXIS_POS_Y; axes.y = BMI2_AXIS_NEG_X;
    
    imu.remapAxes(axes);
}






// 자동 장착 방향 인식 함수 (Auto-Detection)
// 기가 정지 상태일 때 중력 가속도(1g)가 어느 축으로 쏠리는지 분석하여 방향을 찾아내는 방식입니다. 
// 초기 부팅 시 1회 수행하거나 보정 모드에서 사용합니다.

void autoDetectOrientation() {
    float ax, ay, az;
    // 센서에서 원본 가속도 데이터 읽기 (Remap 적용 전 순수 데이터 권장)
    imu.readAcceleration(ax, ay, az); 

    bmi2_remap detected_axes;
    
    // 1. Z축 방향 결정 (어떤 축이 하늘/땅을 향하는가?)
    if (abs(az) > 0.8) { // 평평하게 놓임
        detected_axes.z = (az > 0) ? BMI2_AXIS_POS_Z : BMI2_AXIS_NEG_Z;
        // 가속도가 미미한 X, Y 중 정면 정의에 따라 X, Y 결정 (여기서는 기본 매핑 유지)
        detected_axes.x = BMI2_AXIS_POS_X;
        detected_axes.y = BMI2_AXIS_POS_Y;
    } 
    else if (abs(ay) > 0.8) { // 세워져 있음 (Y축이 중력 방향)
        detected_axes.z = (ay > 0) ? BMI2_AXIS_POS_Y : BMI2_AXIS_NEG_Y;
        detected_axes.y = BMI2_AXIS_POS_Z; // 기존 Z를 Y로 보냄
        detected_axes.x = BMI2_AXIS_POS_X;
    }
    else if (abs(ax) > 0.8) { // 옆으로 세워져 있음 (X축이 중력 방향)
        detected_axes.z = (ax > 0) ? BMI2_AXIS_POS_X : BMI2_AXIS_NEG_X;
        detected_axes.x = BMI2_AXIS_POS_Z;
        detected_axes.y = BMI2_AXIS_POS_Y;
    }

    imu.remapAxes(detected_axes);
    Serial.println("Orientation auto-detected and applied.");
}

