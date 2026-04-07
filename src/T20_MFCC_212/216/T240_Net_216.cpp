/* ============================================================================
 * File: T240_Net_216.cpp
 * Summary: Wi-Fi 네트워크 관리 (WiFi Multi, Static IP, AP Fallback)
 * ========================================================================== */

#include "T221_Mfcc_Inter_216.h"
#include <WiFi.h>
#include <WiFiMulti.h>

WiFiMulti wifiMulti;

bool T20_initNetwork(CL_T20_Mfcc::ST_Impl* p) {
    if (p == nullptr) return false;

    ST_T20_ConfigWiFi_t* wCfg = &p->cfg.wifi;
    
    // 모듈 초기화 전 Wi-Fi를 끄고 대기하여 하드웨어 꼬임 방지
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(100));

    // [1] AP Only 모드 강제 실행
    if (wCfg->mode == EN_T20_WIFI_AP_ONLY) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(wCfg->ap_ssid, wCfg->ap_password);
        Serial.printf("[WiFi] Started AP Only Mode: %s\n", wCfg->ap_ssid);
        return true;
    }

    // [2] 고정 IP(Static IP) 처리 (DHCP 비활성화)
    if (wCfg->use_static_ip) {
        IPAddress local_IP, gateway, subnet, dns1, dns2;
        
        // IP 형식이 올바른지 검증 후 적용
        if (local_IP.fromString(wCfg->local_ip) && 
            gateway.fromString(wCfg->gateway) && 
            subnet.fromString(wCfg->subnet)) {
            
            if (wCfg->dns1[0] != 0) dns1.fromString(wCfg->dns1);
            if (wCfg->dns2[0] != 0) dns2.fromString(wCfg->dns2); // [NEW] DNS2 파싱 적용
            
            // dns2까지 인자로 넘겨서 네트워크 설정 (ESP32 Arduino Core 지원 규격)
            if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
                Serial.println("[WiFi] ERR: Static IP Configuration Failed");
            } else {
                Serial.println("[WiFi] Static IP Configured");
            }
        } else {
            Serial.println("[WiFi] ERR: Invalid Static IP Format");
        }
    }

    // [3] WiFi Multi (STA 모드) 다중 AP 등록
    WiFi.mode(WIFI_STA);
    int ap_added = 0;
    for (uint8_t i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
        if (strlen(wCfg->multi_ap[i].ssid) > 0) {
            wifiMulti.addAP(wCfg->multi_ap[i].ssid, wCfg->multi_ap[i].password);
            ap_added++;
        }
    }

    // [4] 접속 시도 (타임아웃 적용)
    Serial.print("[WiFi] Connecting to WiFi (STA)");
    if (ap_added > 0) {
        uint32_t start_ms = millis();
        // 등록된 AP 중 가장 신호가 강한 곳으로 자동 접속 시도
        while (wifiMulti.run() != WL_CONNECTED && (millis() - start_ms < T20::C10_Net::WIFI_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(500));
            Serial.print(".");
        }
    }

    // [5] 결과 확인 및 Fallback (AP 모드 전환)
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] STA Connected!");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("\n[WiFi] ERR: STA Connection Failed!");
        
        // AUTO_FALLBACK 모드일 경우 AP 모드로 전환하여 웹 설정 페이지 접근 보장
        if (wCfg->mode == EN_T20_WIFI_AUTO_FALLBACK) {
            Serial.println("[WiFi] Switching to AP Mode (Fallback)");
            WiFi.mode(WIFI_AP);
            // 패스워드가 8자리 미만이면 ESP32 자체 정책으로 Open(비밀번호 없음)으로 열림 주의
            WiFi.softAP(wCfg->ap_ssid, wCfg->ap_password);
            Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
            return true;
        }
    }
    
    return false;
}
