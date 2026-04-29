// ==========================================
// T4_i18n_009_002.js (Localization Engine)
// ==========================================
const dict = {
    en: {
        header_sub: "Edge AI", tab_dash: "Dashboard", tab_dsp: "DSP & Feature", tab_decision: "Decision & Storage", tab_sys: "System & Comm",
        title_control: "System Control", btn_rec_start: "REC START", btn_rec_stop: "REC STOP", btn_noise_learn: "NOISE LEARN",
        sub_health: "Health Check", sub_charts: "Charts & Visualization", chart_placeholder: "[Chart rendering area (coming soon)]",
        title_dsp: "DSP Pipeline", lbl_window_ms: "Window (ms)", lbl_hop_ms: "Hop (ms)", lbl_median_win: "Median Window",
        sub_filters: "Notch & FIR Filters", lbl_notch1: "Notch 1 (Hz)", lbl_notch2: "Notch 2 (Hz)", lbl_notch_q: "Notch Q-Factor", lbl_fir_lpf: "FIR LPF Cutoff", lbl_fir_hpf: "FIR HPF Cutoff",
        title_noise: "Noise & Beamforming", lbl_pre_emp: "Pre-emphasis Alpha", lbl_beam_gain: "Beamforming Gain", lbl_noise_gate: "Noise Gate Thresh", lbl_learn_frames: "Noise Learn Frames", lbl_spectral_sub: "Spectral Sub Gain",
        sub_spatial: "Spatial & Cepstrum", lbl_spatial_freq: "Spatial Freq (Hz)", lbl_ceps_targets: "Ceps Target (Hz)",
        title_multiband: "Multi-Band Energy RMS", lbl_band_count: "Active Band Count",
        opt_band1: "1 Band", opt_band2: "2 Bands", opt_band3: "3 Bands", sub_band1: "Band 1", sub_band2: "Band 2", sub_band3: "Band 3",
        btn_save_dsp: "SAVE DSP & FEATURE",
        title_decision: "Decision Rules", lbl_sta_lta: "STA/LTA Thresh", lbl_rule_enrg: "Rule Energy Thresh", lbl_rule_stddev: "Rule StdDev Thresh", lbl_test_ng: "Test NG Min Energy", lbl_min_trigger: "Min Trigger Count", lbl_noise_prof: "Noise Profile (Sec)", lbl_valid_start: "Valid Start (Sec)", lbl_valid_end: "Valid End (Sec)",
        title_storage: "Storage Logging", lbl_pre_trig: "Pre-Trigger (Sec)", lbl_rotate_mb: "Rotate Size (MB)", lbl_rotate_min: "Rotate Time (Min)", lbl_idle_flush: "Idle Flush (ms)",
        sub_recorded: "Recorded Files", tbl_file: "File Name", tbl_action: "Action", tbl_coming_soon: "File explorer coming soon...",
        btn_save_decision: "SAVE DECISION & STORAGE",
        title_wifi: "Wi-Fi Connection Mode", lbl_op_mode: "Operation Mode", sub_softap: "Soft-AP Settings", sub_multi_ap: "Target APs (Multi-AP)", lbl_password: "Password",
        opt_ap_only: "0: Access Point (AP) Only", opt_sta_only: "1: Station (STA) Only", opt_ap_sta: "2: AP + STA", opt_auto_fb: "3: Auto Fallback",
        title_mqtt_admin: "MQTT & Admin", lbl_retry_ms: "Retry Interval (ms)", sub_dev_mgmt: "Device Management", btn_backup: "BACKUP CFG", btn_restore: "RESTORE CFG", btn_factory: "FACTORY RESET", btn_reboot: "REBOOT DEVICE",
        title_ota: "Firmware OTA Update", btn_upload_flash: "UPLOAD & FLASH", btn_save_sys: "SAVE SYSTEM & COMM",
        
        // JS Messages & Placeholders
        plc_ceps: "ex) 60, 120, 240", plc_start_hz: "Start Hz", plc_end_hz: "End Hz", plc_broker: "broker.hivemq.com",
        msg_rec_start: "Recording Started", msg_rec_stop: "Recording Stopped", msg_learn: "Noise Learning Triggered",
        msg_reboot_conf: "Reboot device?", msg_factory_conf: "Factory reset will wipe settings. Proceed?", msg_restore_conf: "Restore config and reboot?",
        msg_ota_file: "Please select a file", msg_ota_conf: "Do not power off during update. Proceed?",
        msg_ota_success: "OTA Success! Rebooting.", msg_ota_fail: "OTA Failed by Device",
        msg_save_success: "Settings Saved! Rebooting...", msg_save_fail: "Failed to save (Check Input)",
        msg_net_err: "Network Error", msg_api_off: "API Disconnected...", opt_dhcp: "DHCP", opt_static: "Static IP"
    },
    ko: {
        header_sub: "엣지 AI 제어반", tab_dash: "대시보드", tab_dsp: "신호 & 특징량", tab_decision: "판정 & 저장", tab_sys: "시스템 & 통신",
        title_control: "시스템 제어", btn_rec_start: "녹음 시작", btn_rec_stop: "녹음 정지", btn_noise_learn: "배경소음 학습",
        sub_health: "상태 모니터링", sub_charts: "실시간 차트", chart_placeholder: "[차트 렌더링 영역 (구현 예정)]",
        title_dsp: "DSP 신호 처리", lbl_window_ms: "윈도우 (ms)", lbl_hop_ms: "홉 간격 (ms)", lbl_median_win: "미디언 윈도우",
        sub_filters: "노치 및 FIR 필터", lbl_notch1: "노치 1 (Hz)", lbl_notch2: "노치 2 (Hz)", lbl_notch_q: "노치 Q-팩터", lbl_fir_lpf: "로우패스 컷오프", lbl_fir_hpf: "하이패스 컷오프",
        title_noise: "노이즈 및 빔포밍", lbl_pre_emp: "프리엠파시스 알파", lbl_beam_gain: "빔포밍 증폭", lbl_noise_gate: "노이즈 게이트 임계", lbl_learn_frames: "소음 학습 프레임", lbl_spectral_sub: "스펙트럼 감산비",
        sub_spatial: "위상 및 켑스트럼", lbl_spatial_freq: "위상 분석 대역 (Hz)", lbl_ceps_targets: "켑스트럼 타겟 (Hz)",
        title_multiband: "멀티-밴드 에너지 RMS", lbl_band_count: "활성 대역 개수",
        opt_band1: "1 밴드", opt_band2: "2 밴드", opt_band3: "3 밴드", sub_band1: "대역 1", sub_band2: "대역 2", sub_band3: "대역 3",
        btn_save_dsp: "신호 및 특징량 설정 저장",
        title_decision: "판정 규칙", lbl_sta_lta: "충격음(STA/LTA) 임계", lbl_rule_enrg: "에너지 불량 임계", lbl_rule_stddev: "편차 불량 임계", lbl_test_ng: "센서 불량 임계", lbl_min_trigger: "최소 트리거 횟수", lbl_noise_prof: "학습 프로파일 (초)", lbl_valid_start: "판정 시작 (초)", lbl_valid_end: "판정 종료 (초)",
        title_storage: "저장소 (SD카드) 로깅", lbl_pre_trig: "프리-트리거 (초)", lbl_rotate_mb: "분할 용량 (MB)", lbl_rotate_min: "분할 시간 (분)", lbl_idle_flush: "유휴 저장 대기 (ms)",
        sub_recorded: "저장된 데이터 파일", tbl_file: "파일명", tbl_action: "작업", tbl_coming_soon: "탐색기 준비중...",
        btn_save_decision: "판정 및 저장소 설정 갱신",
        title_wifi: "Wi-Fi 접속 모드", lbl_op_mode: "동작 모드", sub_softap: "독립 AP", sub_multi_ap: "접속 공유기 목록 (순회 접속)", lbl_password: "비밀번호",
        opt_ap_only: "0: 독립 AP 모드", opt_sta_only: "1: 공유기 접속 모드", opt_ap_sta: "2: AP + 공유기 동시", opt_auto_fb: "3: 자동 전환 (Fallback)",
        title_mqtt_admin: "MQTT 및 기기 관리", lbl_retry_ms: "재접속 간격 (ms)", sub_dev_mgmt: "기기 관리", btn_backup: "설정 백업", btn_restore: "설정 복원", btn_factory: "공장 초기화", btn_reboot: "기기 재부팅",
        title_ota: "무선 펌웨어(OTA) 업데이트", btn_upload_flash: "업로드 및 굽기", btn_save_sys: "시스템 및 통신 설정 저장",
        
        // JS Messages & Placeholders
        plc_ceps: "예) 60, 120, 240", plc_start_hz: "시작 주파수", plc_end_hz: "종료 주파수", plc_broker: "broker.hivemq.com",
        msg_rec_start: "녹음이 시작되었습니다", msg_rec_stop: "녹음이 정지되었습니다", msg_learn: "배경소음 학습 트리거됨",
        msg_reboot_conf: "기기를 재부팅하시겠습니까?", msg_factory_conf: "공장 초기화를 진행할까요? 모든 설정이 삭제됩니다.", msg_restore_conf: "설정 파일을 복원하고 재부팅하시겠습니까?",
        msg_ota_file: "파일을 선택하세요", msg_ota_conf: "업데이트 중 전원을 끄지 마세요. 진행할까요?",
        msg_ota_success: "OTA 성공! 기기 재부팅 중...", msg_ota_fail: "OTA 실패 (기기 거부)",
        msg_save_success: "설정 저장됨! 기기 재부팅 중...", msg_save_fail: "입력값을 확인하세요 (저장 실패)",
        msg_net_err: "네트워크 통신 오류", msg_api_off: "API 연결 끊김...", opt_dhcp: "자동(DHCP)", opt_static: "고정 IP"
    }
};

let currentLang = localStorage.getItem('smea_lang') || 'en';

function t(key) {
    return dict[currentLang][key] || key;
}

function applyLanguage() {
    document.querySelectorAll('[data-i18n]').forEach(el => {
        el.innerText = t(el.getAttribute('data-i18n'));
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
        el.placeholder = t(el.getAttribute('data-i18n-placeholder'));
    });
    document.getElementById('btn-lang').innerText = currentLang === 'en' ? "🇰🇷 KOR" : "🇺🇸 ENG";
}

function toggleLanguage() {
    currentLang = currentLang === 'en' ? 'ko' : 'en';
    localStorage.setItem('smea_lang', currentLang);
    applyLanguage();
    
    // JS 동적 생성 영역(라우터 리스트) 재렌더링
    if (typeof initUI === 'function') initUI(true); 
}
