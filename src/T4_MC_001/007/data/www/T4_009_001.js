const API_BASE = "/api";
const UI_CONST = { TOAST_MS: 3000, DIAG_INTERVAL_MS: 1000, REBOOT_RELOAD_MS: 5000, MAX_ROUTERS: 3 };

let diagTimer = null;
let currentLang = 'en';

// ==========================================
// 1. 다국어(i18n) 번역 사전
// ==========================================
const i18n = {
    en: {
        header_sub: "Edge AI",
        tab_dash: "Dashboard", tab_dsp: "DSP & Feature", tab_decision: "Decision & Storage", tab_sys: "System & Comm",
        title_control: "System Control", btn_rec_start: "REC START", btn_rec_stop: "REC STOP", btn_noise_learn: "NOISE LEARN",
        sub_health: "Health Check", sub_charts: "Charts & Visualization", chart_placeholder: "[Chart rendering area (coming soon)]",
        title_dsp: "DSP Pipeline", lbl_window_ms: "Window (ms)", lbl_hop_ms: "Hop (ms)", lbl_median_win: "Median Window",
        sub_filters: "Notch & FIR Filters", lbl_notch1: "Notch 1 (Hz)", lbl_notch2: "Notch 2 (Hz)", lbl_notch_q: "Notch Q-Factor", lbl_fir_lpf: "FIR LPF Cutoff", lbl_fir_hpf: "FIR HPF Cutoff",
        title_noise: "Noise & Beamforming", lbl_pre_emp: "Pre-emphasis Alpha", lbl_beam_gain: "Beamforming Gain", lbl_noise_gate: "Noise Gate Thresh", lbl_learn_frames: "Noise Learn Frames", lbl_spectral_sub: "Spectral Sub Gain",
        sub_spatial: "Spatial & Cepstrum", lbl_spatial_freq: "Spatial Freq (Hz)", lbl_ceps_targets: "Ceps Target (Hz)",
        title_multiband: "Multi-Band Energy RMS", lbl_band_count: "Active Band Count",
        btn_save_dsp: "SAVE DSP & FEATURE",
        title_decision: "Decision Rules", lbl_sta_lta: "STA/LTA Thresh", lbl_rule_enrg: "Rule Energy Thresh", lbl_rule_stddev: "Rule StdDev Thresh", lbl_test_ng: "Test NG Min Energy", lbl_min_trigger: "Min Trigger Count", lbl_noise_prof: "Noise Profile (Sec)", lbl_valid_start: "Valid Start (Sec)", lbl_valid_end: "Valid End (Sec)",
        title_storage: "Storage Logging", lbl_pre_trig: "Pre-Trigger (Sec)", lbl_rotate_mb: "Rotate Size (MB)", lbl_rotate_min: "Rotate Time (Min)", lbl_idle_flush: "Idle Flush (ms)",
        sub_recorded: "Recorded Files", tbl_file: "File Name", tbl_action: "Action", tbl_coming_soon: "File explorer coming soon...",
        btn_save_decision: "SAVE DECISION & STORAGE",
        title_wifi: "Wi-Fi Connection Mode", lbl_op_mode: "Operation Mode", sub_softap: "Soft-AP Settings", sub_multi_ap: "Target Routers (Multi-AP)",
        title_mqtt_admin: "MQTT & Admin", sub_dev_mgmt: "Device Management", btn_backup: "BACKUP CFG", btn_restore: "RESTORE CFG", btn_factory: "FACTORY RESET", btn_reboot: "REBOOT DEVICE",
        title_ota: "Firmware OTA Update", btn_upload_flash: "UPLOAD & FLASH", btn_save_sys: "SAVE SYSTEM & COMM"
    },
    ko: {
        header_sub: "엣지 AI 제어반",
        tab_dash: "대시보드", tab_dsp: "신호 & 특징량", tab_decision: "판정 & 저장", tab_sys: "시스템 & 통신",
        title_control: "시스템 제어", btn_rec_start: "녹음 시작", btn_rec_stop: "녹음 정지", btn_noise_learn: "배경소음 학습",
        sub_health: "상태 모니터링", sub_charts: "실시간 차트", chart_placeholder: "[차트 렌더링 영역 (구현 예정)]",
        title_dsp: "DSP 신호 처리", lbl_window_ms: "윈도우 (ms)", lbl_hop_ms: "홉 간격 (ms)", lbl_median_win: "미디언 윈도우",
        sub_filters: "노치 및 FIR 필터", lbl_notch1: "노치 1 (Hz)", lbl_notch2: "노치 2 (Hz)", lbl_notch_q: "노치 Q-팩터", lbl_fir_lpf: "로우패스 컷오프", lbl_fir_hpf: "하이패스 컷오프",
        title_noise: "노이즈 및 빔포밍", lbl_pre_emp: "프리엠파시스 알파", lbl_beam_gain: "빔포밍 증폭", lbl_noise_gate: "노이즈 게이트 한계", lbl_learn_frames: "소음 학습 프레임", lbl_spectral_sub: "스펙트럼 감산비",
        sub_spatial: "위상 및 켑스트럼", lbl_spatial_freq: "위상 분석 대역 (Hz)", lbl_ceps_targets: "켑스트럼 타겟 (Hz)",
        title_multiband: "멀티-밴드 에너지 RMS", lbl_band_count: "활성 대역 개수",
        btn_save_dsp: "신호 및 특징량 설정 저장",
        title_decision: "판정(Decision) 규칙", lbl_sta_lta: "충격음(STA/LTA) 한계", lbl_rule_enrg: "에너지 불량 한계", lbl_rule_stddev: "편차 불량 한계", lbl_test_ng: "센서 불량 한계", lbl_min_trigger: "최소 트리거 횟수", lbl_noise_prof: "학습 프로파일 (초)", lbl_valid_start: "판정 시작 (초)", lbl_valid_end: "판정 종료 (초)",
        title_storage: "저장소 (SD카드) 로깅", lbl_pre_trig: "프리-트리거 (초)", lbl_rotate_mb: "분할 용량 (MB)", lbl_rotate_min: "분할 시간 (분)", lbl_idle_flush: "유휴 저장 대기 (ms)",
        sub_recorded: "저장된 데이터 파일", tbl_file: "파일명", tbl_action: "작업", tbl_coming_soon: "탐색기 준비중...",
        btn_save_decision: "판정 및 저장소 설정 갱신",
        title_wifi: "Wi-Fi 접속 모드", lbl_op_mode: "동작 모드", sub_softap: "Soft-AP (자체 공유기)", sub_multi_ap: "대상 공유기 (순회 접속)",
        title_mqtt_admin: "MQTT 및 기기 관리", sub_dev_mgmt: "기기 관리", btn_backup: "설정 백업", btn_restore: "설정 복원", btn_factory: "공장 초기화", btn_reboot: "기기 재부팅",
        title_ota: "무선 펌웨어(OTA) 업데이트", btn_upload_flash: "업로드 및 굽기", btn_save_sys: "시스템 및 통신 설정 저장"
    }
};

function applyLanguage() {
    const langObj = i18n[currentLang];
    document.querySelectorAll('[data-i18n]').forEach(el => {
        const key = el.getAttribute('data-i18n');
        if (langObj[key]) el.innerText = langObj[key];
    });
    document.getElementById('btn-lang').innerText = currentLang === 'en' ? "🇰🇷 KOR" : "🇺🇸 ENG";
}

function toggleLanguage() {
    currentLang = currentLang === 'en' ? 'ko' : 'en';
    localStorage.setItem('smea_lang', currentLang);
    applyLanguage();
}

// ==========================================
// 2. UI 유틸리티 및 렌더링 방어
// ==========================================
function showTab(tabId, e = null) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    if(e && e.currentTarget) e.currentTarget.classList.add('active');

    if (tabId === 'tab-dash') {
        fetchDiagnostics();
        diagTimer = setInterval(fetchDiagnostics, UI_CONST.DIAG_INTERVAL_MS);
    } else if (diagTimer) {
        clearInterval(diagTimer);
    }
}

function showToast(msg, isError = false) {
    const toast = document.getElementById("toast");
    toast.innerText = msg;
    toast.className = isError ? "toast show error" : "toast show";
    setTimeout(() => { toast.className = toast.className.replace("show", ""); }, UI_CONST.TOAST_MS);
}

function toggleWifiFields() {
    const mode = document.getElementById("wifi_mode").value;
    document.getElementById("section_ap_config").style.display = (mode === "1") ? "none" : "block"; 
    document.getElementById("section_sta_config").style.display = (mode === "0") ? "none" : "block"; 
}

function renderRouterHTML(idx) {
    return `
    <div style="border-bottom: 1px solid #444; padding-bottom: 10px; margin-bottom: 10px;">
        <label style="color:#aaa; font-size:0.85em; display:block; margin-bottom:6px;">Router ${idx + 1}</label>
        <div style="display:flex; flex-wrap:wrap; gap:8px; margin-bottom:8px;">
            <input type="text" name="multi_ssid_${idx}" placeholder="SSID" style="flex: 1 1 40%; min-width: 100px;">
            <input type="password" name="multi_pass_${idx}" placeholder="Password" style="flex: 1 1 40%; min-width: 100px;">
            <select name="multi_static_${idx}" onchange="document.getElementById('rip_${idx}').style.display = this.value === 'true' ? 'grid' : 'none'" style="flex: 1 1 100%;">
                <option value="false">DHCP</option><option value="true">Static IP</option>
            </select>
        </div>
        <div id="rip_${idx}" style="display:none; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap:8px;">
             <input type="text" name="multi_ip_${idx}" placeholder="IP">
             <input type="text" name="multi_gw_${idx}" placeholder="Gateway">
             <input type="text" name="multi_sn_${idx}" placeholder="Subnet">
             <input type="text" name="multi_dns1_${idx}" placeholder="DNS 1">
        </div>
    </div>`;
}

// ==========================================
// 3. API 제어 (FSM 연동)
// ==========================================
const callAPI = (path, method = 'POST') => fetch(`${API_BASE}${path}`, { method });

document.getElementById('btn-start').onclick = () => callAPI('/recorder_begin').then(() => showToast(currentLang === 'ko' ? "녹음 시작됨" : "Recording Started"));
document.getElementById('btn-stop').onclick = () => callAPI('/recorder_end').then(() => showToast(currentLang === 'ko' ? "녹음 정지됨" : "Recording Stopped"));
document.getElementById('btn-learn').onclick = () => callAPI(`/noise_learn`).then(() => showToast(currentLang === 'ko' ? "배경소음 학습 트리거" : "Noise Learning Triggered"));

async function rebootDevice() {
    if (!confirm(currentLang === 'ko' ? "기기를 재부팅하시겠습니까?" : "Reboot device?")) return;
    try {
        await callAPI('/reboot');
        showToast("Rebooting...");
        setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
    } catch(e) { showToast("Network Error", true); }
}

async function factoryReset() {
    if (!confirm(currentLang === 'ko' ? "공장 초기화를 진행하시겠습니까? 설정이 삭제됩니다." : "Factory reset will wipe settings. Proceed?")) return;
    try {
        await callAPI('/factory_reset');
        showToast("Factory Reset Executed. Rebooting...");
        setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000);
    } catch(e) { showToast("Network Error", true); }
}

// ==========================================
// 4. 설정 (Config) JSON 동기화 로직
// ==========================================
const setNested = (obj, path, value) => {
    const keys = path.split('.');
    let current = obj;
    for (let i = 0; i < keys.length - 1; i++) {
        if (!current[keys[i]]) current[keys[i]] = {};
        current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
};

async function loadSettings() {
    try {
        const res = await fetch(`${API_BASE}/runtime_config`);
        const cfg = await res.json();
        
        ['form-dsp', 'form-decision', 'form-sysadmin'].forEach(formId => {
            const form = document.getElementById(formId);
            if(!form) return;
            form.querySelectorAll('select, input').forEach(el => {
                if (el.name.startsWith('multi_') || el.name.startsWith('band_')) return; 
                if (cfg) {
                    const val = el.name.split('.').reduce((acc, part) => acc && acc[part] !== undefined ? acc[part] : undefined, cfg);
                    if (val !== undefined) el.value = typeof val === 'boolean' ? val.toString() : val;
                }
            });
        });

        if(cfg && cfg.feature && Array.isArray(cfg.feature.ceps_targets)) {
            const hzArr = cfg.feature.ceps_targets.map(v => v > 0 ? (1.0 / v).toFixed(1) : 0).filter(v => v > 0);
            document.getElementById('ceps_targets_input').value = hzArr.join(', ');
        }

        if (cfg && cfg.feature && Array.isArray(cfg.feature.band_ranges)) {
            const f_dsp = document.getElementById('form-dsp');
            cfg.feature.band_ranges.forEach((band, i) => {
                if(i > 2) return;
                f_dsp[`band_${i}_start`].value = band[0] || "";
                f_dsp[`band_${i}_end`].value = band[1] || "";
            });
        }

        if (cfg && cfg.wifi && Array.isArray(cfg.wifi.multi_ap)) {
            const f_adm = document.getElementById('form-sysadmin');
            cfg.wifi.multi_ap.forEach((ap, i) => {
                if (i >= UI_CONST.MAX_ROUTERS) return;
                f_adm[`multi_ssid_${i}`].value = ap.ssid || "";
                f_adm[`multi_pass_${i}`].value = ap.password || "";
                f_adm[`multi_static_${i}`].value = ap.use_static_ip ? "true" : "false";
                f_adm[`multi_ip_${i}`].value = ap.local_ip || "";
                f_adm[`multi_gw_${i}`].value = ap.gateway || "";
                f_adm[`multi_sn_${i}`].value = ap.subnet || "";
                f_adm[`multi_dns1_${i}`].value = ap.dns1 || "";
                document.getElementById(`rip_${i}`).style.display = ap.use_static_ip ? 'grid' : 'none';
            });
        }
        toggleWifiFields();
    } catch(e) { console.warn("Config load fail", e); }
}

async function saveSettings() {
    const configData = {};
    
    ['form-dsp', 'form-decision', 'form-sysadmin'].forEach(formId => {
        const form = document.getElementById(formId);
        if(!form) return;
        new FormData(form).forEach((value, key) => {
            if (key.startsWith('multi_') || key.startsWith('band_')) return;
            let parsed = (value === 'true') ? true : (value === 'false') ? false : (!isNaN(value) && value.trim() !== '') ? Number(value) : value;
            setNested(configData, key, parsed);
        });
    });

    const cepsInput = document.getElementById('ceps_targets_input').value;
    if (cepsInput) {
        if(!configData.feature) configData.feature = {};
        configData.feature.ceps_targets = cepsInput.split(',').map(v => 1.0 / Number(v.trim()));
    }

    const f_dsp = document.getElementById('form-dsp');
    const bandRanges = [];
    for(let i=0; i<3; i++) {
        bandRanges.push([
            Number(f_dsp[`band_${i}_start`].value) || 0,
            Number(f_dsp[`band_${i}_end`].value) || 0
        ]);
    }
    if(!configData.feature) configData.feature = {};
    configData.feature.band_ranges = bandRanges;

    const multiApArray = [];
    const f_adm = document.getElementById('form-sysadmin');
    for(let i=0; i < UI_CONST.MAX_ROUTERS; i++) {
        const ssid = f_adm.querySelector(`[name="multi_ssid_${i}"]`).value;
        if (ssid.trim()) {
            multiApArray.push({
                ssid: ssid, 
                password: f_adm.querySelector(`[name="multi_pass_${i}"]`).value,
                use_static_ip: f_adm.querySelector(`[name="multi_static_${i}"]`).value === 'true',
                local_ip: f_adm.querySelector(`[name="multi_ip_${i}"]`).value,
                gateway: f_adm.querySelector(`[name="multi_gw_${i}"]`).value,
                subnet: f_adm.querySelector(`[name="multi_sn_${i}"]`).value,
                dns1: f_adm.querySelector(`[name="multi_dns1_${i}"]`).value
            });
        }
    }
    if(!configData.wifi) configData.wifi = {};
    configData.wifi.multi_ap = multiApArray;

    try {
        const res = await fetch(`${API_BASE}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData) 
        });
        if(res.ok) {
            showToast(currentLang === 'ko' ? "설정 저장됨! 기기 재부팅 중..." : "Settings Saved! Rebooting...");
            setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
        } else {
            showToast(currentLang === 'ko' ? "입력값을 확인하세요" : "Failed to save (Check Input)", true);
        }
    } catch (e) { showToast("Network error", true); }
}

// ==========================================
// 5. OTA 및 진단 폴링
// ==========================================
async function uploadOTA() {
    const fileInput = document.getElementById('ota-file');
    if (!fileInput.files.length) return showToast(currentLang === 'ko' ? "파일을 선택하세요" : "Please select a file", true);
    if (!confirm(currentLang === 'ko' ? "업데이트 중 전원을 끄지 마세요. 진행할까요?" : "Do not power off. Proceed?")) return;

    const formData = new FormData();
    formData.append("update", fileInput.files[0]);

    const progressText = document.getElementById('ota-progress');
    progressText.style.display = 'block';
    
    const xhr = new XMLHttpRequest();
    xhr.open("POST", `${API_BASE}/ota`, true);
    
    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            progressText.innerText = `Uploading: ${percent}%`;
        }
    };
    
    xhr.onload = () => {
        if (xhr.status === 200) {
            progressText.innerText = "Success! Rebooting...";
            showToast("OTA Success! Rebooting.");
            setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000);
        } else {
            progressText.style.display = 'none';
            showToast("OTA Failed", true);
        }
    };
    xhr.send(formData);
}

async function downloadConfig() {
    try {
        const res = await fetch(`${API_BASE}/runtime_config`);
        const cfg = await res.json();
        const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(cfg, null, 2));
        const a = document.createElement('a');
        a.href = dataStr; a.download = "smea100_backup.json";
        a.click(); showToast("Backup Downloaded");
    } catch (e) { showToast("Download Failed", true); }
}

function uploadConfig(event) {
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async function(e) {
        try {
            JSON.parse(e.target.result); 
            if (!confirm(currentLang === 'ko' ? "설정 파일을 복원하고 재부팅하시겠습니까?" : "Restore config and reboot?")) return;
            const res = await fetch(`${API_BASE}/runtime_config`, {
                method: 'POST', headers: {'Content-Type': 'application/json'}, body: e.target.result
            });
            if (res.ok) {
                showToast("Config Restored! Rebooting...");
                setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
            } else showToast("Restore Failed", true);
        } catch (err) { showToast("Invalid JSON file", true); }
    };
    reader.readAsText(file);
}

async function fetchDiagnostics() {
    try {
        const res = await fetch(`${API_BASE}/status`);
        const d = await res.json();
        document.getElementById('diag-health').innerText = JSON.stringify(d, null, 2);

        const tag = document.getElementById('status-tag');
        if (d.sys_state !== undefined) {
            switch(d.sys_state) {
                case 0: tag.className = "tag offline"; tag.innerText = "INIT"; break;
                case 1: tag.className = "tag ready"; tag.innerText = "READY"; break;
                case 2: tag.className = "tag monitoring"; tag.innerText = "MONITORING"; break;
                case 3: tag.className = "tag recording"; tag.innerText = "RECORDING"; break;
                default: tag.className = "tag online"; tag.innerText = "UNKNOWN"; break;
            }
        }
    } catch(e) { 
        document.getElementById('diag-health').innerText = "API Disconnected..."; 
        document.getElementById('status-tag').className = "tag offline"; 
        document.getElementById('status-tag').innerText = "API OFFLINE";
    }
}

document.addEventListener('DOMContentLoaded', () => {
    // 저장된 언어 불러오기
    const savedLang = localStorage.getItem('smea_lang');
    if (savedLang) currentLang = savedLang;
    applyLanguage();

    const container = document.getElementById('routers_container');
    if(container) { 
        let html = "";
        for(let i=0; i < UI_CONST.MAX_ROUTERS; i++) html += renderRouterHTML(i);
        container.innerHTML = html;
    }
    loadSettings();
});
