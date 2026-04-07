/* /src/T20_MFCC_212/216/T217_Def_Net_216.h */

#include "T210_Def_Com_216.h"

/* ----------------------------------------------------------------------------
 * [NEW] 네트워크 설정 구조체
 * ---------------------------------------------------------------------------- */
typedef enum {
    EN_T20_WIFI_STA_ONLY = 0,   // 공유기 접속만 시도
    EN_T20_WIFI_AP_ONLY,        // 자체 AP 모드만 켬
    EN_T20_WIFI_AUTO_FALLBACK   // STA 시도 후 실패 시 AP 전환 (권장)
} EM_T20_WiFiMode_t;

typedef struct {
    char ssid[32];
    char password[64];
} ST_T20_WiFiCredential_t;

typedef struct {
    EM_T20_WiFiMode_t       mode;
    ST_T20_WiFiCredential_t multi_ap[T20::C10_Net::WIFI_MULTI_MAX];
    
    char                    ap_ssid[32];
    char                    ap_password[64];
    
    bool                    use_static_ip;
    char                    local_ip[16];
    char                    gateway[16];
    char                    subnet[16];
    char                    dns1[16];
    char                    dns2[16];
    
} ST_T20_ConfigWiFi_t;



