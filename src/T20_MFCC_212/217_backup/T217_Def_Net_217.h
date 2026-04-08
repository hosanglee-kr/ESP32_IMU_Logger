/* ============================================================================
 * File: T217_Def_Net_217.h
 * Summary: Network & WiFi Configuration Types (v217)
 * ========================================================================== */
#pragma once
#include "T210_Def_Com_217.h"

typedef enum {
    EN_T20_WIFI_STA_ONLY = 0,
    EN_T20_WIFI_AP_ONLY,
    EN_T20_WIFI_AP_STA,
    EN_T20_WIFI_AUTO_FALLBACK
} EM_T20_WiFiMode_t;

typedef struct {
    char ssid[32];
    char password[64];
} ST_T20_WiFiCredential_t;

typedef struct {
    EM_T20_WiFiMode_t mode;
    ST_T20_WiFiCredential_t multi_ap[3]; // T20::C10_Net::WIFI_MULTI_MAX 대응
    
    char ap_ssid[32];
    char ap_password[64];
    
    bool use_static_ip;
    char local_ip[16];
    char gateway[16];
    char subnet[16];
    char dns1[16];
    char dns2[16];
} ST_T20_ConfigWiFi_t;
