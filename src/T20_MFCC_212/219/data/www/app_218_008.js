/* app_218_008.js - v218.009 (Constantized) */

// ==========================================
// 0. 시스템 전역 상수 (Constants)
// ==========================================
const T20_CONST = {
    API_BASE: "/api/t20",
    WS: {
        RECONNECT_MS: 2000,
        PROTOCOL: window.location.protocol === 'https:' ? 'wss:' : 'ws:',
    },
    DATA_LEN: {
        FFT_SIZE: 256,
        FFT_BINS: 129,
        MFCC_COEFFS: 13,
        VECTOR_39D: 39,
        TENSOR_624: 624,   // 16 frames * 39D
        LEGACY_424: 424,   // 256 + 129 + 39
        SEQUENCE_MAX: 16
    },
    CHART_LIMITS: {
        WAVE: { MIN: -2.0, MAX: 2.0 },
        SPEC: { MIN: 0.0, MAX: 15.0 },
        MFCC_STATIC: { MIN: -20.0, MAX: 20.0 },
        MFCC_DELTA: { MIN: -5.0, MAX: 5.0 },
        MFCC_ACCEL: { MIN: -2.0, MAX: 2.0 }
    },
    UI: {
        TOAST_MS: 3000,
        DIAG_INTERVAL_MS: 1000,
        REBOOT_RELOAD_MS: 6000,
        BTN_DEBOUNCE_MS: 500,
        MAX_ROUTERS: 3
    }
};

let socket;
let charts = {};
let isLearning = false;
let diagTimer = null;

// ==========================================
// 1. UI 및 유틸리티 로직
// ==========================================
function showTab(tabId, e = null) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');

    if(e && e.currentTarget) {
        e.currentTarget.classList.add('active');
    }

    if (tabId === 'tab-diag') {
        fetchDiagnostics();
        diagTimer = setInterval(fetchDiagnostics, T20_CONST.UI.DIAG_INTERVAL_MS);
    } else if (diagTimer) {
        clearInterval(diagTimer);
    }
}

function showToast(msg, isError = false) {
    const toast = document.getElementById("toast");
    toast.innerText = msg;
    toast.className = isError ? "toast show error" : "toast show";
    setTimeout(() => { 
        toast.className = toast.className.replace("show", ""); 
    }, T20_CONST.UI.TOAST_MS);
}

function toggleWifiFields() {
    const mode = document.getElementById("wifi_mode").value;
    const apSection = document.getElementById("section_ap_config");
    const staSection = document.getElementById("section_sta_config");

    // 0: STA Only, 1: AP Only
    apSection.style.display = (mode === "0") ? "none" : "block";
    staSection.style.display = (mode === "1") ? "none" : "block";
}

function renderRouterHTML(idx) {
    return `
    <div style="border-bottom: 1px solid #444; padding-bottom: 10px; margin-bottom: 10px;">
        <label style="color:#aaa; font-weight:bold; display:block; margin-bottom:5px;">Router ${idx + 1}</label>
        <div style="display:flex; gap:5px; margin-bottom:5px;">
            <input type="text" name="multi_ssid_${idx}" placeholder="SSID" style="flex:2;">
            <input type="password" name="multi_pass_${idx}" placeholder="Password" style="flex:2;">
            <select name="multi_static_${idx}" onchange="document.getElementById('rip_${idx}').style.display = this.value === 'true' ? 'flex' : 'none'" style="flex:1;">
                <option value="false">DHCP</option><option value="true">Static</option>
            </select>
        </div>
        <div id="rip_${idx}" style="display:none; flex-wrap:wrap; gap:5px;">
             <input type="text" name="multi_ip_${idx}" placeholder="IP" style="flex:1;">
             <input type="text" name="multi_gw_${idx}" placeholder="Gateway" style="flex:1;">
             <input type="text" name="multi_sn_${idx}" placeholder="Subnet" style="flex:1;">
        </div>
    </div>`;
}

function initUI() {
    const container = document.getElementById('routers_container');
    if(container) { 
        let html = "";
        for(let i=0; i < T20_CONST.UI.MAX_ROUTERS; i++) html += renderRouterHTML(i);
        container.innerHTML = html;
    }
    toggleWifiFields();
}

// ==========================================
// 2. 차트 엔진 초기화
// ==========================================
function initCharts() {
    const baseOpt = { 
        animation: false, responsive: true, maintainAspectRatio: false, 
        plugins: { legend: { display: false } }, 
        elements: { point: { radius: 0 } } 
    };

    const mfccColors = Array(T20_CONST.DATA_LEN.MFCC_COEFFS).fill('#4caf50')
                      .concat(Array(T20_CONST.DATA_LEN.MFCC_COEFFS).fill('#ff9800'))
                      .concat(Array(T20_CONST.DATA_LEN.MFCC_COEFFS).fill('#f44336'));

    charts.wave = new Chart(document.getElementById('chart-waveform'), { 
        type: 'line', 
        data: { labels: Array(T20_CONST.DATA_LEN.FFT_SIZE).fill(''), datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1 }] }, 
        options: { ...baseOpt, scales: { y: { min: T20_CONST.CHART_LIMITS.WAVE.MIN, max: T20_CONST.CHART_LIMITS.WAVE.MAX } } } 
    });

    charts.spec = new Chart(document.getElementById('chart-spectrum'), { 
        type: 'line', 
        data: { labels: Array(T20_CONST.DATA_LEN.FFT_BINS).fill(''), datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)' }] }, 
        options: baseOpt 
    });

    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { 
        type: 'bar', 
        data: { labels: Array(T20_CONST.DATA_LEN.VECTOR_39D).fill(''), datasets: [{ data: [], backgroundColor: mfccColors }] }, 
        options: baseOpt 
    });
}

class WaterfallChart {
    constructor(canvasId, dataLen, minVal, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas ? this.canvas.getContext('2d', { alpha: false, willReadFrequently: true }) : null;
        this.minVal = minVal; this.maxVal = maxVal;
        if (this.canvas) { 
            this.canvas.width = 600; 
            this.canvas.height = dataLen; 
        }
    }
    draw(dataArray) {
        if (!this.ctx) return;
        const w = this.canvas.width, h = this.canvas.height;
        this.ctx.drawImage(this.canvas, 1, 0, w - 1, h, 0, 0, w - 1, h);
        for (let y = 0; y < h; y++) {
            this.ctx.fillStyle = getHeatmapColor(dataArray[h - 1 - y], this.minVal, this.maxVal);
            this.ctx.fillRect(w - 1, y, 1, 1);
        }
    }
}

function getHeatmapColor(value, min, max) {
    let norm = (value - min) / (max - min);
    norm = Math.max(0, Math.min(1, norm));
    const r = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.5))))));
    const g = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.25))))));
    const b = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * norm)))));
    return `rgb(${r},${g},${b})`;
}

const waterfallSpec = new WaterfallChart('chart-waterfall-spec', T20_CONST.DATA_LEN.FFT_BINS, T20_CONST.CHART_LIMITS.SPEC.MIN, T20_CONST.CHART_LIMITS.SPEC.MAX);
const waterfallMfccStatic = new WaterfallChart('chart-waterfall-mfcc-static', T20_CONST.DATA_LEN.MFCC_COEFFS, T20_CONST.CHART_LIMITS.MFCC_STATIC.MIN, T20_CONST.CHART_LIMITS.MFCC_STATIC.MAX);
const waterfallMfccDelta = new WaterfallChart('chart-waterfall-mfcc-delta', T20_CONST.DATA_LEN.MFCC_COEFFS, T20_CONST.CHART_LIMITS.MFCC_DELTA.MIN, T20_CONST.CHART_LIMITS.MFCC_DELTA.MAX);
const waterfallMfccAccel = new WaterfallChart('chart-waterfall-mfcc-deltadelta', T20_CONST.DATA_LEN.MFCC_COEFFS, T20_CONST.CHART_LIMITS.MFCC_ACCEL.MIN, T20_CONST.CHART_LIMITS.MFCC_ACCEL.MAX);

function drawMfccWaterfalls(floats39) {
    const dim = T20_CONST.DATA_LEN.MFCC_COEFFS;
    waterfallMfccStatic.draw(floats39.subarray(0, dim));
    waterfallMfccDelta.draw(floats39.subarray(dim, dim * 2));
    waterfallMfccAccel.draw(floats39.subarray(dim * 2, dim * 3));
}

// ==========================================
// 3. WebSocket 로직 (스로틀링 대응)
// ==========================================
function connectWebSocket() {
    socket = new WebSocket(`${T20_CONST.WS.PROTOCOL}//${window.location.host}${T20_CONST.API_BASE}/ws`);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => { 
        const tag = document.getElementById('status-tag');
        tag.className = "tag online"; tag.innerText = "WS CONNECTED"; 
    };

    socket.onmessage = (event) => {
        if (!(event.data instanceof ArrayBuffer)) return;
        const floats = new Float32Array(event.data);

        if (floats.length === T20_CONST.DATA_LEN.VECTOR_39D) {
            charts.mfcc.data.datasets[0].data = floats;
            charts.mfcc.update('none');
            drawMfccWaterfalls(floats);
        }
        else if (floats.length === T20_CONST.DATA_LEN.TENSOR_624) {
            // 시퀀스 모드: 가장 마지막 프레임을 대표로 차트 표시
            const lastFrame = floats.subarray(T20_CONST.DATA_LEN.TENSOR_624 - 39, T20_CONST.DATA_LEN.TENSOR_624);
            charts.mfcc.data.datasets[0].data = lastFrame;
            charts.mfcc.update('none');
            // 워터폴에는 시퀀스 전체를 빠르게 그리기
            for(let i=0; i < T20_CONST.DATA_LEN.SEQUENCE_MAX; i++) {
                drawMfccWaterfalls(floats.subarray(i * 39, (i + 1) * 39));
            }
        }
        else if (floats.length === T20_CONST.DATA_LEN.LEGACY_424) {
            const wave = floats.subarray(0, 256), spec = floats.subarray(256, 385), mfcc = floats.subarray(385, 424);
            charts.wave.data.datasets[0].data = wave; 
            charts.spec.data.datasets[0].data = spec; 
            charts.mfcc.data.datasets[0].data = mfcc;
            charts.wave.update('none'); charts.spec.update('none'); charts.mfcc.update('none');
            waterfallSpec.draw(spec); drawMfccWaterfalls(mfcc);
        }
    };

    socket.onclose = () => { 
        const tag = document.getElementById('status-tag');
        tag.className = "tag offline"; tag.innerText = "WS DISCONNECTED"; 
        setTimeout(connectWebSocket, T20_CONST.WS.RECONNECT_MS); 
    };
}

// ==========================================
// 4. API 제어 (REST)
// ==========================================
const callAPI = (path, method = 'POST') => fetch(`${T20_CONST.API_BASE}${path}`, { method });

document.getElementById('btn-start').onclick = () => callAPI('/recorder_begin').then(() => showToast("Measurement Started"));
document.getElementById('btn-stop').onclick = () => callAPI('/recorder_end').then(() => showToast("Measurement Stopped"));
document.getElementById('btn-learn').onclick = () => { 
    isLearning = !isLearning; 
    callAPI(`/noise_learn?active=${isLearning}`).then(() => showToast(isLearning ? "Noise Learning ON" : "Noise Learning OFF")); 
};

document.getElementById('btn-calibrate').onclick = async () => {
    if(!confirm("Z-축이 위를 향하도록 수평 상태를 유지하십시오. 계속하시겠습니까?")) return;
    showToast("Calibrating...");
    try {
        const res = await callAPI('/calibrate');
        if ((await res.json()).ok) showToast("Calibration Success!");
        else showToast("Calibration Failed", true);
    } catch(e) { showToast("Network Error", true); }
};

async function rebootDevice() {
    if (!confirm("장치를 재부팅하시겠습니까?")) return;
    try {
        await callAPI('/reboot');
        showToast("Rebooting... Please wait.");
        setTimeout(() => location.reload(), T20_CONST.UI.REBOOT_RELOAD_MS);
    } catch(e) { showToast("Reboot command sent.", true); setTimeout(() => location.reload(), T20_CONST.UI.REBOOT_RELOAD_MS); }
}

// ==========================================
// 5. 설정 동기화 (Nested JSON Logic)
// ==========================================
const getNested = (obj, path) => path.split('.').reduce((acc, part) => acc && acc[part] !== undefined ? acc[part] : undefined, obj);
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
        const res = await fetch(`${T20_CONST.API_BASE}/runtime_config`);
        const cfg = await res.json();
        const form = document.getElementById('form-config');

        form.querySelectorAll('select, input').forEach(el => {
            if (el.name.startsWith('multi_')) return; // Multi-WiFi는 별도 처리
            const val = getNested(cfg, el.name);
            if (val !== undefined) el.value = typeof val === 'boolean' ? val.toString() : val;
        });

        // WiFi Multi-AP Mapping
        if (cfg.wifi_multi_ap && Array.isArray(cfg.wifi_multi_ap)) {
            cfg.wifi_multi_ap.forEach((ap, i) => {
                if (i >= T20_CONST.UI.MAX_ROUTERS) return;
                form[`multi_ssid_${i}`].value = ap.ssid || "";
                form[`multi_pass_${i}`].value = ap.pass || "";
                form[`multi_static_${i}`].value = ap.use_static_ip ? "true" : "false";
                form[`multi_ip_${i}`].value = ap.local_ip || "";
                form[`multi_gw_${i}`].value = ap.gateway || "";
                form[`multi_sn_${i}`].value = ap.subnet || "";
                document.getElementById(`rip_${i}`).style.display = ap.use_static_ip ? 'flex' : 'none';
            });
        }
        toggleWifiFields();
    } catch(e) { console.warn("Config load fail", e); }
}

async function saveSettings() {
    const formData = new FormData(document.getElementById('form-config'));
    const configData = {};
    const multiApArray = [];

    formData.forEach((value, key) => {
        if (key.startsWith('multi_')) return;
        let parsed = (value === 'true') ? true : (value === 'false') ? false : (!isNaN(value) && value.trim() !== '') ? Number(value) : value;
        setNested(configData, key, parsed);
    });

    for(let i=0; i < T20_CONST.UI.MAX_ROUTERS; i++) {
        const ssid = document.querySelector(`[name="multi_ssid_${i}"]`).value;
        if (ssid.trim()) {
            multiApArray.push({
                ssid: ssid,
                pass: document.querySelector(`[name="multi_pass_${i}"]`).value,
                use_static_ip: document.querySelector(`[name="multi_static_${i}"]`).value === 'true',
                local_ip: document.querySelector(`[name="multi_ip_${i}"]`).value,
                gateway: document.querySelector(`[name="multi_gw_${i}"]`).value,
                subnet: document.querySelector(`[name="multi_sn_${i}"]`).value
            });
        }
    }
    configData.wifi_multi_ap = multiApArray;

    try {
        const res = await fetch(`${T20_CONST.API_BASE}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData)
        });
        if(res.ok) showToast("Settings saved & Applied!");
        else showToast("Save failed", true);
    } catch (e) { showToast("Network error", true); }
}

// ==========================================
// 6. 스토리지 및 진단
// ==========================================
async function refreshFileList() {
    const list = document.getElementById('file-list');
    list.innerHTML = '<tr><td colspan="4">Loading...</td></tr>';
    try {
        const res = await fetch(`${T20_CONST.API_BASE}/recorder_index`);
        const data = await res.json();
        list.innerHTML = data.items.map(f => `
            <tr>
                <td>${f.path.split('/').pop()}</td>
                <td>${(f.size_bytes/1024).toFixed(1)} KB</td>
                <td>${f.record_count}</td>
                <td><button class="btn primary" onclick="window.location.href='${T20_CONST.API_BASE}/recorder/download?path=${f.path}'">Down</button></td>
            </tr>`).join('');
    } catch(e) { list.innerHTML = '<tr><td colspan="4">No record index found.</td></tr>'; }
}

async function fetchDiagnostics() {
    try {
        const res = await fetch(`${T20_CONST.API_BASE}/status`);
        const d = await res.json();
        document.getElementById('val-dropped').innerText = "0";
        document.getElementById('val-samples').innerText = d.hash || "0";
        document.getElementById('diag-health').innerText = JSON.stringify(d, null, 2);
        document.getElementById('diag-sensor').innerText = d.sensor_status || "Active";
    } catch(e) { document.getElementById('diag-health').innerText = "API Error"; }
}

window.onload = () => {
    initUI();
    initCharts();
    connectWebSocket();
    loadSettings();
    refreshFileList();
};

