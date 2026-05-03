// ==========================================
// T4_app_009_002.js (Core UI & State Manager)
// ==========================================
const UI_CONST = { TOAST_MS: 3000, DIAG_INTERVAL_MS: 1000, REBOOT_RELOAD_MS: 5000, MAX_ROUTERS: 3 };
let diagTimer = null;

function showToast(msg, isError = false) {
    const toast = document.getElementById("toast");
    toast.innerText = msg;
    toast.className = isError ? "toast show error" : "toast show";
    setTimeout(() => { toast.className = toast.className.replace("show", ""); }, UI_CONST.TOAST_MS);
}

function showTab(tabId, e = null) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    if(e && e.currentTarget) e.currentTarget.classList.add('active');

    if (tabId === 'tab-dash') {
        pollDiagnostics();
        diagTimer = setInterval(pollDiagnostics, UI_CONST.DIAG_INTERVAL_MS);
    } else if (diagTimer) {
        clearInterval(diagTimer);
    }
}

async function pollDiagnostics() {
    try {
        const d = await SMEA_API.fetchDiagnostics();
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
        document.getElementById('diag-health').innerText = t('msg_api_off'); 
        document.getElementById('status-tag').className = "tag offline"; 
        document.getElementById('status-tag').innerText = "API OFFLINE";
    }
}

// 동적 공유기 설정부 HTML 렌더링 (다국어 호환)
function renderRouterHTML(idx) {
    return `
    <div style="border-bottom: 1px solid #444; padding-bottom: 10px; margin-bottom: 10px;">
        <label style="color:#aaa; font-size:0.85em; display:block; margin-bottom:6px;">AP ${idx + 1}</label>
        <div style="display:flex; flex-wrap:wrap; gap:8px; margin-bottom:8px;">
            <input type="text" name="multi_ssid_${idx}" data-i18n-placeholder="plc_ssid" placeholder="SSID" style="flex: 1 1 40%; min-width: 100px;">
            <input type="password" name="multi_pass_${idx}" data-i18n-placeholder="lbl_password" placeholder="Password" style="flex: 1 1 40%; min-width: 100px;">
            <select name="multi_static_${idx}" onchange="document.getElementById('rip_${idx}').style.display = this.value === 'true' ? 'grid' : 'none'" style="flex: 1 1 100%;">
                <option value="false">${t('opt_dhcp')}</option><option value="true">${t('opt_static')}</option>
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

function initUI(isReload = false) {
    const container = document.getElementById('routers_container');
    if(container && !isReload) { 
        let html = "";
        for(let i=0; i < UI_CONST.MAX_ROUTERS; i++) html += renderRouterHTML(i);
        container.innerHTML = html;
    }
    document.getElementById("wifi_mode").addEventListener('change', (e) => {
        document.getElementById("section_ap_config").style.display = (e.target.value === "1") ? "none" : "block"; 
        document.getElementById("section_sta_config").style.display = (e.target.value === "0") ? "none" : "block"; 
    });
    document.getElementById("wifi_mode").dispatchEvent(new Event('change'));
}

// ==========================================
// 버튼 이벤트 바인딩 (FSM 제어)
// ==========================================
document.getElementById('btn-start').onclick = () => SMEA_API.postCommand('/recorder_begin').then(() => showToast(t('msg_rec_start')));
document.getElementById('btn-stop').onclick = () => SMEA_API.postCommand('/recorder_end').then(() => showToast(t('msg_rec_stop')));
document.getElementById('btn-learn').onclick = () => SMEA_API.postCommand('/noise_learn').then(() => showToast(t('msg_learn')));

document.getElementById('btn-reboot').onclick = async () => {
    if (!confirm(t('msg_reboot_conf'))) return;
    try {
        await SMEA_API.postCommand('/reboot');
        showToast("Rebooting...");
        setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
    } catch(e) { showToast(t('msg_net_err'), true); }
};

document.getElementById('btn-factory').onclick = async () => {
    if (!confirm(t('msg_factory_conf'))) return;
    try {
        await SMEA_API.postCommand('/factory_reset');
        showToast("Factory Reset. Rebooting...");
        setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000);
    } catch(e) { showToast(t('msg_net_err'), true); }
};

document.getElementById('btn-backup').onclick = async () => {
    try { await SMEA_API.downloadBackup(); showToast("Backup Downloaded"); } 
    catch (e) { showToast("Download Failed", true); }
};

// ==========================================
// 설정(Config) 직렬화 및 로드
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

async function syncSettingsToUI() {
    try {
        const cfg = await SMEA_API.loadRuntimeConfig();
        
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
        document.getElementById("wifi_mode").dispatchEvent(new Event('change'));
    } catch(e) { console.warn("Config sync error", e); }
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
        const isOk = await SMEA_API.saveRuntimeConfig(configData);
        if(isOk) {
            showToast(t('msg_save_success'));
            setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
        } else {
            showToast(t('msg_save_fail'), true);
        }
    } catch (e) { showToast(t('msg_net_err'), true); }
}

// 저장 버튼 공통 바인딩
document.getElementById('btn-save-dsp').onclick = saveSettings;
document.getElementById('btn-save-decision').onclick = saveSettings;
document.getElementById('btn-save-sys').onclick = saveSettings;

// ==========================================
// OTA 및 복원 파일 업로드 이벤트 연결
// ==========================================
document.getElementById('btn-ota').onclick = () => {
    const fileInput = document.getElementById('ota-file');
    if (!fileInput.files.length) return showToast(t('msg_ota_file'), true);
    if (!confirm(t('msg_ota_conf'))) return;

    const progressText = document.getElementById('ota-progress');
    progressText.style.display = 'block';
    
    SMEA_API.uploadOTA(
        fileInput.files[0],
        (e) => {
            if (e.lengthComputable) {
                const percent = Math.round((e.loaded / e.total) * 100);
                progressText.innerText = `Uploading: ${percent}%`;
            }
        },
        () => {
            progressText.innerText = "Success! Rebooting...";
            showToast(t('msg_ota_success'));
            setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000);
        },
        () => {
            progressText.style.display = 'none';
            showToast(t('msg_ota_fail'), true);
        }
    );
};

document.getElementById('config-file').onchange = (event) => {
    const file = event.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async function(e) {
        try {
            JSON.parse(e.target.result); 
            if (!confirm(t('msg_restore_conf'))) { event.target.value = ""; return; }
            
            const res = await fetch(`${API_BASE}/runtime_config`, {
                method: 'POST', headers: {'Content-Type': 'application/json'}, body: e.target.result
            });
            if (res.ok) {
                showToast(t('msg_save_success'));
                setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS);
            } else showToast("Restore Failed", true);
        } catch (err) { showToast("Invalid JSON file", true); }
        event.target.value = "";
    };
    reader.readAsText(file);
};

// ==========================================
// 부팅 완료
// ==========================================
document.addEventListener('DOMContentLoaded', () => {
    applyLanguage();
    initUI();
    syncSettingsToUI();
});

