// ==========================================
// 0. 시스템 전역 상수 및 동적 상태 (v220.010)
// ==========================================
const T20_CONST = {
    API_BASE: "/api/t20",
    WS: {
        RECONNECT_MS: 2000,
        PROTOCOL: window.location.protocol === 'https:' ? 'wss:' : 'ws:',
    },
    CHART_LIMITS: {
        WAVE: { MIN: -2.0, MAX: 2.0 },
        SPEC: { MIN: 0.0, MAX: 15.0 },
        MFCC_STATIC: { MIN: -20.0, MAX: 20.0 },
        MFCC_DELTA: { MIN: -5.0, MAX: 5.0 },
        MFCC_ACCEL: { MIN: -2.0, MAX: 2.0 }
    },
    UI: { TOAST_MS: 3000, DIAG_INTERVAL_MS: 1000, REBOOT_RELOAD_MS: 6000, MAX_ROUTERS: 3 }
};

let sysState = {
    fft_size: 256,
    axis_count: 1,
    mfcc_coeffs: 13,
    sequence_frames: 16
};

let socket;
let charts = {};
let isLearning = false;
let diagTimer = null;

// [렌더링 버퍼 최적화 변수]
let pendingWave = null;
let pendingSpec = null;
let pendingMfcc = null;
let pendingSeqFrames = [];
let isSinglePending = false;
let isSeqPending = false;

// ==========================================
// 1. UI 및 유틸리티 로직
// ==========================================
function showTab(tabId, e = null) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');

    if(e && e.currentTarget) e.currentTarget.classList.add('active');

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
    setTimeout(() => { toast.className = toast.className.replace("show", ""); }, T20_CONST.UI.TOAST_MS);
}

function toggleWifiFields() {
    const mode = document.querySelector('[name="wifi.mode"]').value;
    document.getElementById("section_ap_config").style.display = (mode === "0") ? "none" : "block";
    document.getElementById("section_sta_config").style.display = (mode === "1") ? "none" : "block";
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
             <input type="text" name="multi_ip_${idx}" placeholder="IP" style="width: 48%;">
             <input type="text" name="multi_gw_${idx}" placeholder="Gateway" style="width: 48%;">
             <input type="text" name="multi_sn_${idx}" placeholder="Subnet" style="width: 32%;">
             <input type="text" name="multi_dns1_${idx}" placeholder="DNS 1" style="width: 32%;">
             <input type="text" name="multi_dns2_${idx}" placeholder="DNS 2" style="width: 32%;">
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
// 2. 동적 차트 엔진 제어
// ==========================================
function initCharts() {
    const baseOpt = { 
        animation: false, responsive: true, maintainAspectRatio: false, 
        plugins: { legend: { display: false } }, 
        elements: { point: { radius: 0 } } 
    };

    charts.wave = new Chart(document.getElementById('chart-waveform'), { 
        type: 'line', data: { labels: [], datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1 }] }, 
        options: { ...baseOpt, scales: { y: { min: T20_CONST.CHART_LIMITS.WAVE.MIN, max: T20_CONST.CHART_LIMITS.WAVE.MAX } } } 
    });

    charts.spec = new Chart(document.getElementById('chart-spectrum'), { 
        type: 'line', data: { labels: [], datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)' }] }, 
        options: baseOpt 
    });

    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { 
        type: 'bar', data: { labels: [], datasets: [{ data: [], backgroundColor: [] }] }, 
        options: baseOpt 
    });
}

function updateChartDimensions() {
    const N = sysState.fft_size;
    const Bins = Math.floor(N / 2) + 1;
    const mfccTotal = sysState.mfcc_coeffs * 3 * sysState.axis_count;

    charts.wave.data.labels = Array(N).fill('');
    charts.spec.data.labels = Array(Bins).fill('');
    charts.mfcc.data.labels = Array(mfccTotal).fill('');
    
    const colors = [];
    for(let i=0; i < mfccTotal; i++) {
        const axisIdx = Math.floor(i / (sysState.mfcc_coeffs * 3));
        const featType = Math.floor((i % (sysState.mfcc_coeffs * 3)) / sysState.mfcc_coeffs); 
        
        const alpha = featType === 0 ? '1.0' : (featType === 1 ? '0.7' : '0.4');
        let rgb = axisIdx === 0 ? '76, 175, 80' : (axisIdx === 1 ? '255, 152, 0' : '244, 67, 54');
        colors.push(`rgba(${rgb}, ${alpha})`);
    }
    charts.mfcc.data.datasets[0].backgroundColor = colors;
    
    charts.wave.update('none');
    charts.spec.update('none');
    charts.mfcc.update('none');

    waterfallSpec.resize(Bins);
    waterfallMfccStatic.resize(sysState.mfcc_coeffs);
    waterfallMfccDelta.resize(sysState.mfcc_coeffs);
    waterfallMfccAccel.resize(sysState.mfcc_coeffs);
}

class WaterfallChart {
    constructor(canvasId, minVal, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas ? this.canvas.getContext('2d', { alpha: false, willReadFrequently: true }) : null;
        this.minVal = minVal; this.maxVal = maxVal;
        if (this.canvas) this.canvas.width = 600; 
    }
    resize(newLen) {
        if(this.canvas && this.canvas.height !== newLen) {
            this.canvas.height = newLen;
            this.ctx.fillStyle = '#000';
            this.ctx.fillRect(0,0, this.canvas.width, this.canvas.height);
        }
    }
    draw(dataArray) {
        if (!this.ctx || !dataArray) return;
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

const waterfallSpec = new WaterfallChart('chart-waterfall-spec', T20_CONST.CHART_LIMITS.SPEC.MIN, T20_CONST.CHART_LIMITS.SPEC.MAX);
const waterfallMfccStatic = new WaterfallChart('chart-waterfall-mfcc-static', T20_CONST.CHART_LIMITS.MFCC_STATIC.MIN, T20_CONST.CHART_LIMITS.MFCC_STATIC.MAX);
const waterfallMfccDelta = new WaterfallChart('chart-waterfall-mfcc-delta', T20_CONST.CHART_LIMITS.MFCC_DELTA.MIN, T20_CONST.CHART_LIMITS.MFCC_DELTA.MAX);
const waterfallMfccAccel = new WaterfallChart('chart-waterfall-mfcc-deltadelta', T20_CONST.CHART_LIMITS.MFCC_ACCEL.MIN, T20_CONST.CHART_LIMITS.MFCC_ACCEL.MAX);

function drawMfccWaterfalls(axis0Mfcc) {
    const dim = sysState.mfcc_coeffs;
    waterfallMfccStatic.draw(axis0Mfcc.subarray(0, dim));
    waterfallMfccDelta.draw(axis0Mfcc.subarray(dim, dim * 2));
    waterfallMfccAccel.draw(axis0Mfcc.subarray(dim * 2, dim * 3));
}

// ==========================================
// 3. WebSocket 및 60Hz 렌더링 최적화 루프
// ==========================================
function renderLoop() {
    if (isSinglePending && pendingWave && pendingSpec && pendingMfcc) {
        charts.wave.data.datasets[0].data = pendingWave;
        charts.spec.data.datasets[0].data = pendingSpec;
        charts.mfcc.data.datasets[0].data = pendingMfcc;
        
        charts.wave.update('none');
        charts.spec.update('none');
        charts.mfcc.update('none');
        
        waterfallSpec.draw(pendingSpec);
        const singleAxisDim = sysState.mfcc_coeffs * 3;
        drawMfccWaterfalls(pendingMfcc.subarray(0, singleAxisDim));
        
        isSinglePending = false;
    } 
    else if (isSeqPending && pendingMfcc) {
        charts.mfcc.data.datasets[0].data = pendingMfcc;
        charts.mfcc.update('none');
        
        pendingSeqFrames.forEach(frame => drawMfccWaterfalls(frame));
        pendingSeqFrames = [];
        isSeqPending = false;
    }
    requestAnimationFrame(renderLoop);
}

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

        const N = sysState.fft_size;
        const Bins = Math.floor(N / 2) + 1;
        const singleAxisDim = sysState.mfcc_coeffs * 3;
        const totalMfccDim = singleAxisDim * sysState.axis_count;
        
        const expectedSingle = N + Bins + totalMfccDim;
        const expectedSeq = sysState.sequence_frames * totalMfccDim;

        if (floats.length === expectedSingle) {
            pendingWave = floats.subarray(0, N);
            pendingSpec = floats.subarray(N, N + Bins);
            pendingMfcc = floats.subarray(N + Bins, expectedSingle);
            isSinglePending = true;
        }
        else if (floats.length === expectedSeq) {
            pendingMfcc = floats.subarray(expectedSeq - totalMfccDim, expectedSeq);
            pendingSeqFrames = [];
            for(let i=0; i < sysState.sequence_frames; i++) {
                pendingSeqFrames.push(floats.subarray(i * totalMfccDim, i * totalMfccDim + singleAxisDim));
            }
            isSeqPending = true;
        }
    };

    socket.onclose = () => { 
        const tag = document.getElementById('status-tag');
        tag.className = "tag offline"; tag.innerText = "WS DISCONNECTED"; 
        setTimeout(connectWebSocket, T20_CONST.WS.RECONNECT_MS); 
    };
}

// ==========================================
// 4. API 제어 및 OTA 업데이트
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

async function uploadOTA() {
    const fileInput = document.getElementById('ota-file');
    if (!fileInput.files.length) return showToast("Please select a .bin file", true);
    if (!confirm("펌웨어 업데이트 중에는 전원을 끄지 마십시오. 진행하시겠습니까?")) return;

    const formData = new FormData();
    formData.append("update", fileInput.files[0]);

    const progressText = document.getElementById('ota-progress');
    progressText.style.display = 'block';
    
    const xhr = new XMLHttpRequest();
    xhr.open("POST", `${T20_CONST.API_BASE}/ota_update`, true);
    
    xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            progressText.innerText = `Uploading & Flashing: ${percent}%`;
        }
    };
    
    xhr.onload = () => {
        if (xhr.status === 200) {
            progressText.innerText = "Success! Rebooting...";
            showToast("OTA Success! Device is rebooting.");
            setTimeout(() => location.reload(), T20_CONST.UI.REBOOT_RELOAD_MS + 2000);
        } else {
            progressText.style.display = 'none';
            showToast("OTA Failed.", true);
        }
    };
    xhr.send(formData);
}

// ==========================================
// 5. 설정 동기화 (Multi-Band Trigger 포함)
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
        
        sysState.fft_size = parseInt(cfg.feature?.fft_size || 256);
        sysState.axis_count = parseInt(cfg.feature?.axis_count || 1);
        sysState.mfcc_coeffs = parseInt(cfg.feature?.mfcc_coeffs || 13);
        sysState.sequence_frames = parseInt(cfg.output?.sequence_frames || 16);
        updateChartDimensions();

        const formConfig = document.getElementById('form-config');
        const formMqtt = document.getElementById('form-mqtt');

        [formConfig, formMqtt].forEach(form => {
            if(!form) return;
            form.querySelectorAll('select, input').forEach(el => {
                if (el.name.startsWith('multi_') || el.name.startsWith('band_')) return;
                const val = getNested(cfg, el.name);
                if (val !== undefined) el.value = typeof val === 'boolean' ? val.toString() : val;
            });
        });

        if(cfg.trigger && cfg.trigger.bands) {
            cfg.trigger.bands.forEach((b, i) => {
                if(i > 2) return;
                formConfig[`band_${i}_enable`].value = b.enable ? "true" : "false";
                formConfig[`band_${i}_start`].value = b.start_hz || "";
                formConfig[`band_${i}_end`].value = b.end_hz || "";
                formConfig[`band_${i}_thresh`].value = b.threshold || "";
            });
        }

        if (cfg.wifi && cfg.wifi.multi_ap && Array.isArray(cfg.wifi.multi_ap)) {
            cfg.wifi.multi_ap.forEach((ap, i) => {
                if (i >= T20_CONST.UI.MAX_ROUTERS) return;
                const f = formMqtt || formConfig;
                f[`multi_ssid_${i}`].value = ap.ssid || "";
                f[`multi_pass_${i}`].value = ap.password || "";
                f[`multi_static_${i}`].value = ap.use_static_ip ? "true" : "false";
                f[`multi_ip_${i}`].value = ap.local_ip || "";
                f[`multi_gw_${i}`].value = ap.gateway || "";
                f[`multi_sn_${i}`].value = ap.subnet || "";
                f[`multi_dns1_${i}`].value = ap.dns1 || "";
                f[`multi_dns2_${i}`].value = ap.dns2 || "";
                document.getElementById(`rip_${i}`).style.display = ap.use_static_ip ? 'flex' : 'none';
            });
        }
        toggleWifiFields();
    } catch(e) { console.warn("Config load fail", e); }
}

async function saveSettings() {
    const configData = {};
    
    ['form-config', 'form-mqtt'].forEach(formId => {
        const form = document.getElementById(formId);
        if(!form) return;
        new FormData(form).forEach((value, key) => {
            if (key.startsWith('multi_') || key.startsWith('band_')) return;
            let parsed = (value === 'true') ? true : (value === 'false') ? false : (!isNaN(value) && value.trim() !== '') ? Number(value) : value;
            setNested(configData, key, parsed);
        });
    });

    const formConfig = document.getElementById('form-config');
    const bands = [];
    for(let i=0; i<3; i++) {
        bands.push({
            enable: formConfig[`band_${i}_enable`].value === 'true',
            start_hz: Number(formConfig[`band_${i}_start`].value),
            end_hz: Number(formConfig[`band_${i}_end`].value),
            threshold: Number(formConfig[`band_${i}_thresh`].value)
        });
    }
    if(!configData.trigger) configData.trigger = {};
    configData.trigger.bands = bands;

    const multiApArray = [];
    const f = document.getElementById('form-mqtt') || formConfig;
    for(let i=0; i < T20_CONST.UI.MAX_ROUTERS; i++) {
        const ssid = f.querySelector(`[name="multi_ssid_${i}"]`).value;
        if (ssid.trim()) {
            multiApArray.push({
                ssid: ssid, 
                password: f.querySelector(`[name="multi_pass_${i}"]`).value,
                use_static_ip: f.querySelector(`[name="multi_static_${i}"]`).value === 'true',
                local_ip: f.querySelector(`[name="multi_ip_${i}"]`).value,
                gateway: f.querySelector(`[name="multi_gw_${i}"]`).value,
                subnet: f.querySelector(`[name="multi_sn_${i}"]`).value,
                dns1: f.querySelector(`[name="multi_dns1_${i}"]`).value,
                dns2: f.querySelector(`[name="multi_dns2_${i}"]`).value
            });
        }
    }
    if(!configData.wifi) configData.wifi = {};
    configData.wifi.multi_ap = multiApArray;

    try {
        const res = await fetch(`${T20_CONST.API_BASE}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData)
        });
        if(res.ok) showToast("Settings saved! Rebooting...");
        else showToast("Save failed", true);
    } catch (e) { showToast("Network error", true); }
}

// ==========================================
// 6. 스토리지 및 진단 로직 
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
    
    // 60Hz 렌더링 최적화 루프 엔진 가동
    requestAnimationFrame(renderLoop);
};
