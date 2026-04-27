/* ============================================================================
 * File: T400_Main_002.hpp (main.cpp에서 include 및 호출)
 * Summary: SMEA-100 System Entry Point & Global Interrupt Handler
 * * [AI 메모: 제공 기능 요약]
 * 1. 하드웨어 리소스(I2S, GPIO, SDMMC) 초기화 및 FSM 실행.
 * 2. 외부 DC 트리거 신호를 위한 고속 ISR(Interrupt Service Routine) 관리.
 * * [AI 메모: 보완 및 방어 사항 적용]
 * 1. [링커 방어] 헤더 파일 내 구현체로 인한 다중 정의(Multiple Definition) 
 * 에러를 막기 위해 static 및 inline 키워드 적용.
 * 2. [블라인드 스팟 차단] 부팅 시 이미 트리거가 HIGH인 상태를 감지하지 
 * 못하는 버그를 막기 위해 초기 상태(Initial State) 강제 동기화 추가.
 * ========================================================================== */
#pragma once

#include <Arduino.h>
#include "T410_Config_001.hpp"
#include "T450_FsmMgr_001.hpp"

// [적발 2 보완] static 선언으로 ODR(One Definition Rule) 위반 링커 에러 방어
static T450_FsmManager& v_fsm = T450_FsmManager::getInstance();

/**
 * @brief 외부 트리거(DC High/Low) 인터럽트 서비스 루틴
 */
void IRAM_ATTR handleTriggerISR() {
    bool v_isActive = digitalRead(SmeaConfig::Hardware::PIN_TRIGGER);
    v_fsm.handleExternalTrigger(v_isActive);
}

// [적발 2 보완] inline 선언으로 다중 참조 허용
inline void T4_init() {
    Serial.begin(SmeaConfig::Hardware::SERIAL_BAUD);
    delay(1000);
    Serial.println("[SMEA-100] Starting System...");

    // 하드웨어 핀 모드 설정
    pinMode(SmeaConfig::Hardware::PIN_TRIGGER, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(SmeaConfig::Hardware::PIN_TRIGGER), handleTriggerISR, CHANGE);

    // [적발 1 보완] 부팅 시점의 핀 초기 상태(Initial State) 강제 동기화
    // (이미 장비 전원이 켜질 때 트리거가 들어와 있는 상황 완벽 방어)
    bool v_initialState = digitalRead(SmeaConfig::Hardware::PIN_TRIGGER);
    v_fsm.handleExternalTrigger(v_initialState);

    // FSM 매니저 초기화 및 태스크 기동
    v_fsm.begin();

    Serial.println("[SMEA-100] Setup Completed. Waiting for Trigger...");
}

/**
 * @brief 메인 루프 (FreeRTOS 태스크들이 Core 0/1을 점유하므로 최소한의 감시만 수행)
 */
inline void T4_run() {
    // 1. 시스템 헬스체크 디버그
    static uint32_t v_lastAliveCheck = 0;
    if (millis() - v_lastAliveCheck > SmeaConfig::Task::ALIVE_CHECK_MS) {
        v_lastAliveCheck = millis();
        // Serial.printf("[ALIVE] Heap: %u, State: %d\n", ESP.getFreeHeap(), v_fsm.getCurrentState());
    }

    // 2. 강력한 방어 로직(네트워크 재연결, 좀비 소켓 회수, OOM 플러시 등) 실행
    // 이 함수 내부에서 통신과 스토리지의 백그라운드 작업이 처리됩니다.
    v_fsm.runMaintenanceTask();
    
    // CPU 양보 (메인 루프는 가벼운 백그라운드 작업만 하므로 10ms 지연으로 충분함)
    vTaskDelay(pdMS_TO_TICKS(10));
}
