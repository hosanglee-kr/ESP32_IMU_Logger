// ==========================================
// T4_app_009_005.js (uPlot Renderer & App Logic)
// ==========================================
const UI_CONST = { TOAST_MS: 3000, DIAG_INTERVAL_MS: 1000, REBOOT_RELOAD_MS: 5000, MAX_ROUTERS: 3 };
let diagTimer = null;
let lastDomUpdateMs = 0;

const plots = {};

class WaterfallChart {
    constructor(canvasId, minVal, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas ? this.canvas.getContext('2d', { alpha: false, willReadFrequently: true }) : null;
        this.minVal = minVal; this.maxVal = maxVal;
        if (this.canvas) {
            const rect = this.canvas.parentElement.getBoundingClientRect();
            this.canvas.width = rect.width;
            this.canvas.height = 39; 
        }
    }
    draw(dataArray) {
        if (!this.ctx || !dataArray) return;
        const w = this.canvas.width, h = this.canvas.height;
        this.ctx.drawImage(this.canvas, 1, 0, w - 1, h, 0, 0, w - 1, h); 
        for (let y = 0; y < h; y++) {
            let norm = (dataArray[y] - this.minVal) / (this.maxVal - this.minVal);
            norm = Math.max(0, Math.min(1, norm));
            const r = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.5))))));
            const g = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.25))))));
            const b = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * norm)))));
            this.ctx.fillStyle = `rgb(${r},${g},${b})`;
            this.ctx.fillRect(w - 1, y, 1, 1); 
        }
    }
}
const waterfallMfcc = new WaterfallChart('canvas-waterfall', -15.0, 15.0);

// ==========================================
// uPlot 차트 초기화 (DOM 크기 0 에러 방어)
// ==========================================
function initCharts() {
    // 탭 렌더링 지연시간 확보를 위한 비동기 처리
    setTimeout(() => {
        const getW = (id) => Math.max(document.getElementById(id).clientWidth, 300); // 0 넓이 방어
        const commonOpts = { height: 200, cursor: { sync: { key: "syncGroup" } }, legend: { show: false } };

        // 1. 파형 차트 (X축 Time 속성 강제 끄기)
        plots.wave = new uPlot({
            ...commonOpts, width: getW('uplot-waveform'), title: "Waveform (1024 Samples)",
            scales: { x: { time: false }, y: { auto: false, range: [-2, 2] } }, 
            axes: [ { space: 50 }, {} ],
            series: [ {}, { stroke: "#4caf50", width: 1, points: { show: false }, paths: uPlot.paths.linear() } ] 
        }, window.dashState.wave, document.getElementById('uplot-waveform'));

        // 2. 스펙트럼 차트 (Top Peak 오버레이)
        plots.spec = new uPlot({
            ...commonOpts, width: getW('uplot-spectrum'), title: "Power Spectrum",
            scales: { x: { time: false }, y: { auto: true } },
            axes: [ { space: 50 }, {} ],
            series: [ {}, { stroke: "#ff9800", fill: "rgba(255, 152, 0, 0.2)", width: 1, points: { show: false } } ],
            hooks: {
                draw: [
                    u => {
                        if (!u.data || !u.data[0].length) return; // NaN 방어
                        u.ctx.save(); u.ctx.fillStyle = "red";
                        window.dashState.peaks.forEach(p => {
                            if(p.f > 0 && p.a > 0) {
                                let cx = u.valToPos(p.f, 'x', true), cy = u.valToPos(p.a, 'y', true);
                                if (!isNaN(cx) && !isNaN(cy)) {
                                    u.ctx.beginPath(); u.ctx.arc(cx, cy, 4, 0, 2*Math.PI); u.ctx.fill();
                                }
                            }
                        });
                        u.ctx.restore();
                    }
                ]
            }
        }, window.dashState.spec, document.getElementById('uplot-spectrum'));

        // 3. 텔레메트리 (이것만 UNIX Time 적용)
        plots.telemetry = new uPlot({
            ...commonOpts, width: getW('uplot-telemetry'), title: "Energy & STA/LTA Ratio",
            scales: { x: { time: true } }, 
            axes: [ {}, { scale: 'rms', stroke: '#4caf50' }, { scale: 'stalta', side: 1, stroke: '#ff9800' } ],
            series: [ {}, { scale: 'rms', stroke: "#4caf50", width: 2, points:{show:false} }, { scale: 'stalta', stroke: "#ff9800", width: 2, points:{show:false} } ],
            hooks: {
                draw: [
                    u => {
                        if (!u.data || !u.data[0].length) return;
                        u.ctx.save(); u.ctx.strokeStyle = "rgba(244, 67, 54, 0.8)"; u.ctx.setLineDash([5, 5]); u.ctx.lineWidth = 2;
                        let yRms = u.valToPos(window.dashState.thresh.rms, 'rms', true);
                        if(!isNaN(yRms)) { u.ctx.beginPath(); u.ctx.moveTo(u.bbox.left, yRms); u.ctx.lineTo(u.bbox.left + u.bbox.width, yRms); u.ctx.stroke(); }
                        let ySta = u.valToPos(window.dashState.thresh.stalta, 'stalta', true);
                        u.ctx.strokeStyle = "rgba(255, 152, 0, 0.8)";
                        if(!isNaN(ySta)) { u.ctx.beginPath(); u.ctx.moveTo(u.bbox.left, ySta); u.ctx.lineTo(u.bbox.left + u.bbox.width, ySta); u.ctx.stroke(); }
                        u.ctx.restore();
                    }
                ]
            }
        }, window.dashState.telemetry, document.getElementById('uplot-telemetry'));

        // 4. MFCC Line (시간)
        plots.mfccLine = new uPlot({
            ...commonOpts, width: getW('uplot-mfcc-line'), title: "MFCC Time-Series",
            scales: { x: { time: true } },
            series: [ {}, { stroke: "#03a9f4", width: 2, points:{show:false} }, { stroke: "#9c27b0", width: 2, points:{show:false} } ]
        }, window.dashState.mfccLine, document.getElementById('uplot-mfcc-line'));

        // 5. MFCC Bar (배열 인덱스 - Time 아님)
        plots.mfccBar = new uPlot({
            ...commonOpts, width: getW('uplot-mfcc-bar'), title: "MFCC 39D Snapshot",
            scales: { x: { time: false }, y: { auto: false, range: [-20, 20] } },
            axes: [ { space: 50 }, {} ],
            series: [ {}, { stroke: "#4caf50", fill: "rgba(76, 175, 80, 0.3)", width: 1, points: {show:false}, paths: uPlot.paths.stepped({ align: 1 }) } ]
        }, window.dashState.mfccBar, document.getElementById('uplot-mfcc-bar'));

        // 창 크기 변경 시 차트 폭 동기화
        const ro = new ResizeObserver(entries => {
            for (let entry of entries) {
                const w = Math.max(entry.contentRect.width, 300);
                if (plots.wave) plots.wave.setSize({width: w, height: 200});
                if (plots.spec) plots.spec.setSize({width: w, height: 200});
                if (plots.telemetry) plots.telemetry.setSize({width: w, height: 200});
                if (plots.mfccLine) plots.mfccLine.setSize({width: w, height: 200});
                if (plots.mfccBar) plots.mfccBar.setSize({width: w, height: 200});
                if (waterfallMfcc && waterfallMfcc.canvas) waterfallMfcc.canvas.width = w;
            }
        });
        ro.observe(document.getElementById('uplot-waveform').parentElement);

    }, 50); // DOM 레이아웃 정렬 대기
}

// 60Hz 일괄 렌더링 루프 (데이터 갱신 적용 및 DOM Null 방어)
function renderLoop() {
    if (window.dashState.isDirty && document.getElementById('tab-dash').classList.contains('active')) {
        const now = Date.now();
        
        // 텍스트 DOM 갱신은 부하 방지를 위해 10Hz(100ms) 쓰로틀링
        if (now - lastDomUpdateMs > 100) {
            const elRms = document.getElementById('val-rms');
            if (elRms) elRms.innerText = window.dashState.rms.toFixed(4);
            
            const elStaLta = document.getElementById('val-stalta');
            if (elStaLta) elStaLta.innerText = window.dashState.sta_lta.toFixed(2);
            
            const elCentroid = document.getElementById('val-centroid');
            if (elCentroid) elCentroid.innerText = Math.round(window.dashState.centroid) + ' Hz';
            
            // [버그 수정] HTML에서 삭제된 peak-list 표가 있을 때만 렌더링하도록 Null Check 방어
            const elPeakList = document.getElementById('peak-list');
            if (elPeakList) {
                let html = '';
                for (let i = 0; i < 5; i++) {
                    const f = window.dashState.peaks[i].f;
                    const a = window.dashState.peaks[i].a;
                    html += `<tr><td>${i+1}</td><td>${f > 0 ? f.toFixed(1) : '-'}</td><td>${a > 0 ? a.toFixed(4) : '-'}</td></tr>`;
                }
                elPeakList.innerHTML = html;
            }
            
            lastDomUpdateMs = now;
        }

        // uPlot 일괄 업데이트 (setData 호출 시 uPlot이 배열 변경 감지)
        if(plots.wave) plots.wave.setData(window.dashState.wave);
        if(plots.spec) plots.spec.setData(window.dashState.spec);
        if(plots.telemetry) plots.telemetry.setData(window.dashState.telemetry);
        if(plots.mfccLine) plots.mfccLine.setData(window.dashState.mfccLine);
        if(plots.mfccBar) plots.mfccBar.setData(window.dashState.mfccBar);
        if(waterfallMfcc) waterfallMfcc.draw(window.dashState.mfccBar[1]); 

        window.dashState.isDirty = false;
    }
    requestAnimationFrame(renderLoop);
}


/*
// 60Hz 일괄 렌더링 루프 (데이터 갱신 적용)
function renderLoop() {
    if (window.dashState.isDirty && document.getElementById('tab-dash').classList.contains('active')) {
        const now = Date.now();
        // 텍스트 DOM 갱신은 부하 방지를 위해 10Hz(100ms) 쓰로틀링
        if (now - lastDomUpdateMs > 100) {
            document.getElementById('val-rms').innerText = window.dashState.rms.toFixed(4);
            document.getElementById('val-stalta').innerText = window.dashState.sta_lta.toFixed(2);
            document.getElementById('val-centroid').innerText = Math.round(window.dashState.centroid) + ' Hz';
            
            let html = '';
            for (let i = 0; i < 5; i++) {
                const f = window.dashState.peaks[i].f;
                const a = window.dashState.peaks[i].a;
                html += `<tr><td>${i+1}</td><td>${f > 0 ? f.toFixed(1) : '-'}</td><td>${a > 0 ? a.toFixed(4) : '-'}</td></tr>`;
            }
            document.getElementById('peak-list').innerHTML = html;
            lastDomUpdateMs = now;
        }

        // uPlot 일괄 업데이트 (setData 호출 시 uPlot이 배열 변경 감지)
        if(plots.wave) plots.wave.setData(window.dashState.wave);
        if(plots.spec) plots.spec.setData(window.dashState.spec);
        if(plots.telemetry) plots.telemetry.setData(window.dashState.telemetry);
        if(plots.mfccLine) plots.mfccLine.setData(window.dashState.mfccLine);
        if(plots.mfccBar) plots.mfccBar.setData(window.dashState.mfccBar);
        if(waterfallMfcc) waterfallMfcc.draw(window.dashState.mfccBar[1]); 

        window.dashState.isDirty = false;
    }
    requestAnimationFrame(renderLoop);
}
*/

// ==========================================
// 탭 전환 및 창 크기 조절
// ==========================================
function showTab(tabId, e = null) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    if(e && e.currentTarget) e.currentTarget.classList.add('active');

    if (tabId === 'tab-dash') {
        // [방어] 대시보드 탭 진입 시 uPlot 리사이즈 강제 호출로 깨짐 방지
        setTimeout(() => window.dispatchEvent(new Event('resize')), 50);
        pollDiagnostics();
        diagTimer = setInterval(pollDiagnostics, UI_CONST.DIAG_INTERVAL_MS);
    } else if (diagTimer) {
        clearInterval(diagTimer);
    }
}

// ... (기존 이벤트 바인딩, syncSettingsToUI, saveSettings 등 100% 동일 유지) ...
function showToast(msg, isError = false) { const toast = document.getElementById("toast"); toast.innerText = msg; toast.className = isError ? "toast show error" : "toast show"; setTimeout(() => { toast.className = toast.className.replace("show", ""); }, UI_CONST.TOAST_MS); }
async function pollDiagnostics() { if (document.getElementById('chk-mock').checked) return; try { const d = await SMEA_API.fetchDiagnostics(); document.getElementById('diag-health').innerText = JSON.stringify(d, null, 2); const tag = document.getElementById('status-tag'); if (d.sys_state !== undefined) { switch(d.sys_state) { case 0: tag.className = "tag offline"; tag.innerText = "INIT"; break; case 1: tag.className = "tag ready"; tag.innerText = "READY"; break; case 2: tag.className = "tag monitoring"; tag.innerText = "MONITORING"; break; case 3: tag.className = "tag recording"; tag.innerText = "RECORDING"; break; default: tag.className = "tag online"; tag.innerText = "UNKNOWN"; break; } } } catch(e) { document.getElementById('diag-health').innerText = t('msg_api_off'); document.getElementById('status-tag').className = "tag offline"; document.getElementById('status-tag').innerText = "API OFFLINE"; } }
function renderRouterHTML(idx) { return ` <div style="border-bottom: 1px solid #444; padding-bottom: 10px; margin-bottom: 10px;"> <label style="color:#aaa; font-size:0.85em; display:block; margin-bottom:6px;">AP ${idx + 1}</label> <div style="display:flex; flex-wrap:wrap; gap:8px; margin-bottom:8px;"> <input type="text" name="multi_ssid_${idx}" data-i18n-placeholder="plc_ssid" placeholder="SSID" style="flex: 1 1 40%; min-width: 100px;"> <input type="password" name="multi_pass_${idx}" data-i18n-placeholder="lbl_password" placeholder="Password" style="flex: 1 1 40%; min-width: 100px;"> <select name="multi_static_${idx}" onchange="document.getElementById('rip_${idx}').style.display = this.value === 'true' ? 'grid' : 'none'" style="flex: 1 1 100%;"> <option value="false">${t('opt_dhcp')}</option><option value="true">${t('opt_static')}</option> </select> </div> <div id="rip_${idx}" style="display:none; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap:8px;"> <input type="text" name="multi_ip_${idx}" placeholder="IP"> <input type="text" name="multi_gw_${idx}" placeholder="Gateway"> <input type="text" name="multi_sn_${idx}" placeholder="Subnet"> <input type="text" name="multi_dns1_${idx}" placeholder="DNS 1"> </div> </div>`; }

window.initUI = function(isReload = false) {
    const container = document.getElementById('routers_container'); if(container && !isReload) { let html = ""; for(let i=0; i < UI_CONST.MAX_ROUTERS; i++) html += renderRouterHTML(i); container.innerHTML = html; }
    const modeSel = document.getElementById("wifi_mode"); if(modeSel && !isReload) { modeSel.addEventListener('change', (e) => { document.getElementById("section_ap_config").style.display = (e.target.value === "1") ? "none" : "block"; document.getElementById("section_sta_config").style.display = (e.target.value === "0") ? "none" : "block"; }); }
}
document.getElementById('chk-mock').addEventListener('change', (e) => toggleMockMode(e.target.checked)); document.getElementById('btn-start').onclick = () => SMEA_API.postCommand('/recorder_begin').then(() => showToast(t('msg_rec_start'))); document.getElementById('btn-stop').onclick = () => SMEA_API.postCommand('/recorder_end').then(() => showToast(t('msg_rec_stop'))); document.getElementById('btn-learn').onclick = () => SMEA_API.postCommand('/noise_learn').then(() => showToast(t('msg_learn'))); document.getElementById('btn-reboot').onclick = async () => { if (confirm(t('msg_reboot_conf'))) { await SMEA_API.postCommand('/reboot'); setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS); }}; document.getElementById('btn-factory').onclick = async () => { if (confirm(t('msg_factory_conf'))) { await SMEA_API.postCommand('/factory_reset'); setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000); }}; document.getElementById('btn-backup').onclick = async () => { try { await SMEA_API.downloadBackup(); showToast("Backup Downloaded"); } catch (e) { showToast("Download Failed", true); }};
const setNested = (obj, path, value) => { const keys = path.split('.'); let current = obj; for (let i = 0; i < keys.length - 1; i++) { if (!current[keys[i]]) current[keys[i]] = {}; current = current[keys[i]]; } current[keys[keys.length - 1]] = value; };
async function syncSettingsToUI() { try { const cfg = await SMEA_API.loadRuntimeConfig(); if (cfg && cfg.decision) { window.dashState.thresh.rms = cfg.decision.rule_enrg_threshold || 0.05; window.dashState.thresh.stalta = cfg.decision.sta_lta_threshold || 3.0; } ['form-dsp', 'form-decision', 'form-sysadmin'].forEach(formId => { const form = document.getElementById(formId); if(!form) return; form.querySelectorAll('select, input').forEach(el => { if (el.name.startsWith('multi_') || el.name.startsWith('band_')) return; if (cfg) { const val = el.name.split('.').reduce((acc, part) => acc && acc[part] !== undefined ? acc[part] : undefined, cfg); if (val !== undefined) el.value = typeof val === 'boolean' ? val.toString() : val; } }); }); if(cfg && cfg.feature && Array.isArray(cfg.feature.ceps_targets)) document.getElementById('ceps_targets_input').value = cfg.feature.ceps_targets.map(v => v > 0 ? (1.0 / v).toFixed(1) : 0).filter(v => v > 0).join(', '); if (cfg && cfg.feature && Array.isArray(cfg.feature.band_ranges)) { const f_dsp = document.getElementById('form-dsp'); cfg.feature.band_ranges.forEach((band, i) => { if(i > 2) return; f_dsp[`band_${i}_start`].value = band[0] || ""; f_dsp[`band_${i}_end`].value = band[1] || ""; }); } if (cfg && cfg.wifi && Array.isArray(cfg.wifi.multi_ap)) { const f_adm = document.getElementById('form-sysadmin'); cfg.wifi.multi_ap.forEach((ap, i) => { if (i >= UI_CONST.MAX_ROUTERS) return; f_adm[`multi_ssid_${i}`].value = ap.ssid || ""; f_adm[`multi_pass_${i}`].value = ap.password || ""; f_adm[`multi_static_${i}`].value = ap.use_static_ip ? "true" : "false"; f_adm[`multi_ip_${i}`].value = ap.local_ip || ""; f_adm[`multi_gw_${i}`].value = ap.gateway || ""; f_adm[`multi_sn_${i}`].value = ap.subnet || ""; f_adm[`multi_dns1_${i}`].value = ap.dns1 || ""; document.getElementById(`rip_${i}`).style.display = ap.use_static_ip ? 'grid' : 'none'; }); } document.getElementById("wifi_mode").dispatchEvent(new Event('change')); } catch(e) { console.warn("Config sync error", e); } }
async function saveSettings() { const configData = {}; ['form-dsp', 'form-decision', 'form-sysadmin'].forEach(formId => { const form = document.getElementById(formId); if(!form) return; new FormData(form).forEach((value, key) => { if (key.startsWith('multi_') || key.startsWith('band_')) return; setNested(configData, key, (value === 'true') ? true : (value === 'false') ? false : (!isNaN(value) && value.trim() !== '') ? Number(value) : value); }); }); const cepsInput = document.getElementById('ceps_targets_input').value; if (cepsInput) { if(!configData.feature) configData.feature = {}; configData.feature.ceps_targets = cepsInput.split(',').map(v => 1.0 / Number(v.trim())); } const f_dsp = document.getElementById('form-dsp'); const bandRanges = []; for(let i=0; i<3; i++) bandRanges.push([Number(f_dsp[`band_${i}_start`].value) || 0, Number(f_dsp[`band_${i}_end`].value) || 0]); if(!configData.feature) configData.feature = {}; configData.feature.band_ranges = bandRanges; const multiApArray = []; const f_adm = document.getElementById('form-sysadmin'); for(let i=0; i < UI_CONST.MAX_ROUTERS; i++) { const ssid = f_adm.querySelector(`[name="multi_ssid_${i}"]`).value; if (ssid.trim()) multiApArray.push({ ssid: ssid, password: f_adm.querySelector(`[name="multi_pass_${i}"]`).value, use_static_ip: f_adm.querySelector(`[name="multi_static_${i}"]`).value === 'true', local_ip: f_adm.querySelector(`[name="multi_ip_${i}"]`).value, gateway: f_adm.querySelector(`[name="multi_gw_${i}"]`).value, subnet: f_adm.querySelector(`[name="multi_sn_${i}"]`).value, dns1: f_adm.querySelector(`[name="multi_dns1_${i}"]`).value }); } if(!configData.wifi) configData.wifi = {}; configData.wifi.multi_ap = multiApArray; try { const isOk = await SMEA_API.saveRuntimeConfig(configData); if(isOk) { showToast(t('msg_save_success')); setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS); } else showToast(t('msg_save_fail'), true); } catch (e) { showToast(t('msg_net_err'), true); } }
document.getElementById('btn-save-dsp').onclick = saveSettings; document.getElementById('btn-save-decision').onclick = saveSettings; document.getElementById('btn-save-sys').onclick = saveSettings;
document.getElementById('btn-ota').onclick = () => { const fileInput = document.getElementById('ota-file'); if (!fileInput.files.length) return showToast(t('msg_ota_file'), true); if (!confirm(t('msg_ota_conf'))) return; const progressText = document.getElementById('ota-progress'); progressText.style.display = 'block'; SMEA_API.uploadOTA(fileInput.files[0], (e) => { if (e.lengthComputable) progressText.innerText = `Uploading: ${Math.round((e.loaded / e.total) * 100)}%`; }, () => { progressText.innerText = "Success! Rebooting..."; showToast(t('msg_ota_success')); setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS + 2000); }, () => { progressText.style.display = 'none'; showToast(t('msg_ota_fail'), true); }); };
document.getElementById('config-file').onchange = (event) => { const file = event.target.files[0]; if (!file) return; const reader = new FileReader(); reader.onload = async function(e) { try { JSON.parse(e.target.result); if (!confirm(t('msg_restore_conf'))) { event.target.value = ""; return; } const res = await fetch(`${API_BASE}/runtime_config`, { method: 'POST', headers: {'Content-Type': 'application/json'}, body: e.target.result }); if (res.ok) { showToast(t('msg_save_success')); setTimeout(() => location.reload(), UI_CONST.REBOOT_RELOAD_MS); } else showToast("Restore Failed", true); } catch (err) { showToast("Invalid JSON file", true); } event.target.value = ""; }; reader.readAsText(file); };

document.addEventListener('DOMContentLoaded', () => {
    applyLanguage();
    window.initUI();
    syncSettingsToUI();
    initCharts(); // uPlot 초기화 (지연 로직 적용됨)
    connectWebSocket(); 
    requestAnimationFrame(renderLoop); 
});


