작성해주신 v009 최신본에서 오타 수정 및 안정성 강화를 위해 수정이 필요한 3가지 포인트의 전/후 비교입니다.
1. [A10_Main_009.h] 디버그 출력 오타 수정
변수명이 포맷 스트링(%) 안에 잘못 포함되어 있어 시리얼 출력이 비정상적으로 나올 수 있는 부분을 수정합니다.
수정 전:
Serial.printf("[T03] Roll:%.1f Pitch:%.1f Yaw:%.1f Mot:%v_sensor_data\n",
              v_sensor_data.euler[0], v_sensor_data.euler[1], v_sensor_data.euler[2], v_sensor_data.motion);

수정 후:
// %v_sensor_data를 %d로 수정하여 실제 motion(bool) 값이 출력되도록 변경
Serial.printf("[T03] Roll:%.1f Pitch:%.1f Yaw:%.1f Mot:%d\n",
              v_sensor_data.euler[0], v_sensor_data.euler[1], v_sensor_data.euler[2], (int)v_sensor_data.motion);

2. [A10_Main_009.h] SD 로깅 태스크 안정화
파일 오픈 실패 시 빈 포인터(v_file)에 접근하여 시스템이 멈추는 것을 방지합니다.
수정 전:
File v_file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);
if (!v_file) {
    Serial.println("!!! SD: Failed to open log file for appending");
}
// 이후 v_file 체크 없이 진행될 위험 있음

수정 후:
File v_file = SD_MMC.open(g_A10_SdMMC.getPath(), FILE_APPEND);
if (!v_file) {
    Serial.println("!!! SD: Failed to open log file - Task Terminated");
    vTaskDelete(NULL); // 태스크를 안전하게 종료하여 시스템 크래시 방지
    return;
}

3. [A10_Main_009.h] 세마포어 생성 시점 (A10_init)
센서가 시작되어 인터럽트가 발생하기 전에 세마포어가 반드시 준비되어 있어야 합니다.
수정 전:
// 2. 센서 초기화
g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);
// ... 이후 큐 생성 등 ...

수정 후:
void A10_init() {
    // [추가] 인터럽트 발생 전 세마포어 핸들 정의 및 생성
    if (g_SB10_Sem_FIFO == NULL) {
        g_SB10_Sem_FIFO = xSemaphoreCreateBinary();
    }

    // 1. SD 및 설정 로드
    // ... 기존 코드 유지 ...

    // 2. 센서 초기화 (이제 내부에서 인터럽트가 발생해도 세마포어가 준비됨)
    g_A10_Imu.begin(g_A10_ImuOptions, &g_A10_SdMMC);
    // ... 이하 동일 ...

💡 시스템 동작 원리 (세마포어 동기화)
위 그림처럼 BMI270 하드웨어가 데이터를 준비하면 ISR이 세마포어를 던지고(Give), 대기 중이던 SensorTask가 즉시 깨어나(Take) 처리하는 구조입니다. 이 수정사항들을 반영하면 타이밍 오차 없는 완벽한 200Hz 데이터 수집이 가능해집니다.
수정된 부분들을 파일에 적용해 보시겠어요? 컴파일 후 데이터 로깅 결과가 어떻게 나오는지 궁금합니다.


