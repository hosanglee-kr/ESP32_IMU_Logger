/* ============================================================================
 * File: T240_Net_216.cpp
 * Summary: Wi-Fi 네트워크 관리 (WiFi Multi, Static IP, AP Fallback)
 * ========================================================================== */

#include "T221_Mfcc_Inter_216.h"
#include <WiFi.h>
#include <WiFiMulti.h>

WiFiMulti g_T240_wifiMulti;

/* ============================================================================
 * [Wi-Fi 초기화 및 접속 매니저] (T240_Net...cpp 또는 메인 설정부)
 * ========================================================================== */
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

    // [2] 새로 추가된 AP + STA 동시 모드 설정
    if (wCfg->mode == EN_T20_WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA); // 두 모드를 동시 활성화
        WiFi.softAP(wCfg->ap_ssid, wCfg->ap_password);
        Serial.printf("[WiFi] AP+STA Dual Mode. AP Started: %s\n", wCfg->ap_ssid);
    } else {
        // AUTO_FALLBACK 또는 STA_ONLY 모드
        WiFi.mode(WIFI_STA);
    }

    // [3] 고정 IP(Static IP) 처리 (DHCP 비활성화)
    if (wCfg->use_static_ip) {
        IPAddress local_IP, gateway, subnet, dns1, dns2;
        if (local_IP.fromString(wCfg->local_ip) && 
            gateway.fromString(wCfg->gateway) && 
            subnet.fromString(wCfg->subnet)) {
            
            if (wCfg->dns1[0] != 0) dns1.fromString(wCfg->dns1);
            if (wCfg->dns2[0] != 0) dns2.fromString(wCfg->dns2); 
            
            if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
                Serial.println("[WiFi] ERR: Static IP Configuration Failed");
            }
        }
    }

    // [4] WiFi Multi (STA 모드) 다중 AP 등록
    int ap_added = 0;
    for (uint8_t i = 0; i < T20::C10_Net::WIFI_MULTI_MAX; i++) {
        if (strlen(wCfg->multi_ap[i].ssid) > 0) {
            g_T240_wifiMulti.addAP(wCfg->multi_ap[i].ssid, wCfg->multi_ap[i].password);
            ap_added++;
        }
    }

    // [5] 접속 시도 (타임아웃 적용)
    if (ap_added > 0) {
        Serial.print("[WiFi] Connecting to WiFi (STA)");
        uint32_t start_ms = millis();
        while (g_T240_wifiMulti.run() != WL_CONNECTED && (millis() - start_ms < T20::C10_Net::WIFI_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(500));
            Serial.print(".");
        }
    }

    // [6] 결과 확인 및 Fallback 처리
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] STA Connected!");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("\n[WiFi] ERR: STA Connection Failed!");
        
        if (wCfg->mode == EN_T20_WIFI_AUTO_FALLBACK) {
            Serial.println("[WiFi] Switching to AP Mode (Fallback)");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(wCfg->ap_ssid, wCfg->ap_password);
            return true;
        } else if (wCfg->mode == EN_T20_WIFI_AP_STA) {
            // AP_STA 모드는 STA 접속에 실패하더라도 이미 AP가 열려있으므로 True 반환
            Serial.println("[WiFi] STA Failed, but AP is still running.");
            return true;
        }
    }
    
    return false;
}

