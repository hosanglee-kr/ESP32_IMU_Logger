/* app_216_005.js */

const API = "/api/t20";
let socket;
let charts = {};
let isLearning = false;
let diagTimer = null;

// ==========================================
// 1. UI 및 유틸리티 로직
// ==========================================
function showTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    
    if(event && event.target) {
        event.target.classList.add('active');
    }

    if (tabId === 'tab-diag') {
        fetchDiagnostics();
        diagTimer = setInterval(fetchDiagnostics, 1000);
    } else if (diagTimer) {
        clearInterval(diagTimer);
    }
}

function showToast(msg, isError = false) {
    const toast = document.getElementById("toast");
    toast.innerText = msg;
    toast.className = isError ? "toast show error" : "toast show";
    setTimeout(() => { toast.className = toast.className.replace("show", ""); }, 3000);
}

function toggleStaticIpFields() {
    const isStatic = document.getElementById("wifi_use_static").value === "true";
    document.getElementById("static_ip_fields").style.display = isStatic ? "block" : "none";
}

// ==========================================
// 2. Chart.js & Waterfall 초기화
// ==========================================
function initCharts() {
    const opt = { animation: false, responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, elements: { point: { radius: 0 } } };
    charts.wave = new Chart(document.getElementById('chart-waveform'), { type: 'line', data: { labels: Array(256).fill(''), datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1 }] }, options: { ...opt, scales: { y: { min: -2.0, max: 2.0 } } } });
    charts.spec = new Chart(document.getElementById('chart-spectrum'), { type: 'line', data: { labels: Array(129).fill(''), datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)' }] }, options: opt });
    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { type: 'bar', data: { labels: Array(39).fill(''), datasets: [{ data: [], backgroundColor: '#2196f3' }] }, options: opt });
}

function getHeatmapColor(value, min, max) {
    let norm = (value - min) / (max - min);
    if (norm < 0) norm = 0; if (norm > 1) norm = 1;
    const r = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.5))))));
    const g = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.25))))));
    const b = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * norm)))));
    return `rgb(${r},${g},${b})`;
}

class WaterfallChart {
    constructor(canvasId, dataLen, minVal, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas ? this.canvas.getContext('2d', { alpha: false, willReadFrequently: true }) : null;
        this.minVal = minVal; this.maxVal = maxVal;
        if (this.canvas) { this.canvas.width = 600; this.canvas.height = dataLen; }
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

const waterfallSpec = new WaterfallChart('chart-waterfall-spec', 129, 0.0, 15.0); 
const waterfallMfcc = new WaterfallChart('chart-waterfall-mfcc', 39, -20.0, 20.0);

// ==========================================
// 3. WebSocket 로직
// ==========================================
function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    socket = new WebSocket(`${protocol}//${window.location.host}${API}/ws`);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => { document.getElementById('status-tag').className = "tag online"; document.getElementById('status-tag').innerText = "WS CONNECTED"; };
    socket.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
            const floats = new Float32Array(event.data);
            const waveData = floats.slice(0, 256), specData = floats.slice(256, 385), mfccData = floats.slice(385, 424);
            
            charts.wave.data.datasets[0].data = waveData; charts.spec.data.datasets[0].data = specData; charts.mfcc.data.datasets[0].data = mfccData;
            charts.wave.update('none'); charts.spec.update('none'); charts.mfcc.update('none');
            waterfallSpec.draw(specData); waterfallMfcc.draw(mfccData);
        }
    };
    socket.onclose = () => { document.getElementById('status-tag').className = "tag offline"; document.getElementById('status-tag').innerText = "WS DISCONNECTED"; setTimeout(connectWebSocket, 2000); };
}

// ==========================================
// 4. API 제어 및 동적 설정
// ==========================================
document.getElementById('btn-start').onclick = () => fetch(`${API}/recorder_begin`, {method: 'POST'}).then(() => showToast("Measurement Started"));
document.getElementById('btn-stop').onclick = () => fetch(`${API}/recorder_end`, {method: 'POST'}).then(() => showToast("Measurement Stopped"));
document.getElementById('btn-learn').onclick = () => { isLearning = !isLearning; fetch(`${API}/noise_learn?active=${isLearning}`, {method: 'POST'}).then(() => showToast(isLearning ? "Noise Learning ON" : "Noise Learning OFF")); };

document.getElementById('btn-calibrate').onclick = async () => {
    showToast("Calibrating... Please keep the device perfectly flat.", false);
    document.getElementById('btn-calibrate').disabled = true;
    try {
        const res = await fetch(`${API}/calibrate_sensor`, {method: 'POST'});
        if ((await res.json()).ok) showToast("Calibration successful and saved!");
        else showToast("Calibration failed!", true);
    } catch(e) { showToast("Network error during calibration", true); } 
    finally { document.getElementById('btn-calibrate').disabled = false; }
};

// 원격 재부팅 로직
async function rebootDevice() {
    if (!confirm("Are you sure you want to reboot the device?\nNetwork connection will be lost temporarily.")) {
        return;
    }
    try {
        await fetch(`${API}/reboot`, { method: 'POST' });
        showToast("Rebooting device... Please wait.");
        setTimeout(() => location.reload(), 6000);
    } catch(e) {
        showToast("Reboot command sent.", true);
        setTimeout(() => location.reload(), 6000);
    }
}

async function loadSettings() {
    try {
        const res = await fetch(`${API}/runtime_config`);
        const cfg = await res.json();
        const form = document.getElementById('form-config');
        
        // Sensor & DSP
        if (cfg.sensor_axis !== undefined) form.sensor_axis.value = cfg.sensor_axis;
        if (cfg.accel_range !== undefined) form.accel_range.value = cfg.accel_range;
        if (cfg.gyro_range !== undefined) form.gyro_range.value = cfg.gyro_range;
        if (cfg.hop_size !== undefined) form.hop_size.value = cfg.hop_size;
        if (cfg.mfcc_coeffs !== undefined) form.mfcc_coeffs.value = cfg.mfcc_coeffs;
        if (cfg.pre_alpha !== undefined) form.pre_alpha.value = cfg.pre_alpha;
        if (cfg.filter_cutoff !== undefined) form.filter_cutoff.value = cfg.filter_cutoff;
        if (cfg.noise_mode !== undefined) form.noise_mode.value = cfg.noise_mode;
        if (cfg.sub_strength !== undefined) form.sub_strength.value = cfg.sub_strength;

        // Network
        if (cfg.wifi_mode !== undefined) form.wifi_mode.value = cfg.wifi_mode;
        if (cfg.wifi_ap_ssid !== undefined) form.wifi_ap_ssid.value = cfg.wifi_ap_ssid;
        if (cfg.wifi_ap_pass !== undefined) form.wifi_ap_pass.value = cfg.wifi_ap_pass;
        if (cfg.wifi_use_static !== undefined) {
            form.wifi_use_static.value = cfg.wifi_use_static ? "true" : "false";
            toggleStaticIpFields(); // UI 토글 동기화
        }
        if (cfg.wifi_local_ip !== undefined) form.wifi_local_ip.value = cfg.wifi_local_ip;
        if (cfg.wifi_gateway !== undefined) form.wifi_gateway.value = cfg.wifi_gateway;
        if (cfg.wifi_subnet !== undefined) form.wifi_subnet.value = cfg.wifi_subnet;
        if (cfg.wifi_dns1 !== undefined) form.wifi_dns1.value = cfg.wifi_dns1;
        if (cfg.wifi_dns2 !== undefined) form.wifi_dns2.value = cfg.wifi_dns2;

        // WiFi Multi Array 역직렬화
        if (cfg.wifi_multi_ap && Array.isArray(cfg.wifi_multi_ap)) {
            cfg.wifi_multi_ap.forEach((ap, idx) => {
                if (idx < 3) {
                    form[`multi_ssid_${idx}`].value = ap.ssid || "";
                    form[`multi_pass_${idx}`].value = ap.pass || "";
                }
            });
        }
    } catch(e) { console.error("Config load fail", e); }
}

async function saveSettings() {
    const formData = new FormData(document.getElementById('form-config'));
    const configData = {};
    const multiApArray = []; 
    
    formData.forEach((value, key) => {
        if (key.startsWith('multi_ssid_')) {
            const idx = parseInt(key.split('_')[2]);
            if (!multiApArray[idx]) multiApArray[idx] = {};
            multiApArray[idx].ssid = value;
        } else if (key.startsWith('multi_pass_')) {
            const idx = parseInt(key.split('_')[2]);
            if (!multiApArray[idx]) multiApArray[idx] = {};
            multiApArray[idx].pass = value;
        } else if (key === 'wifi_use_static') {
            configData[key] = (value === 'true');
        } else {
            configData[key] = isNaN(value) || value === '' ? value : Number(value);
        }
    });

    configData.wifi_multi_ap = multiApArray.filter(ap => ap && ap.ssid && ap.ssid.trim() !== "");

    showToast("Applying settings... (Network changes require hardware reboot)", false);

    try {
        const res = await fetch(`${API}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData)
        });
        if(res.ok) {
            setTimeout(() => showToast("Settings successfully saved!"), 500);
        } else {
            showToast("Failed to apply settings", true);
        }
    } catch (e) {
        showToast("Network error", true);
    }
}

// ==========================================
// 5. 스토리지 및 진단 기능
// ==========================================
async function refreshFileList() {
    const list = document.getElementById('file-list');
    list.innerHTML = '<tr><td colspan="4">Loading...</td></tr>';
    try {
        const res = await fetch(`${API}/recorder_index`);
        const data = await res.json();
        list.innerHTML = data.items.map(f => `<tr><td>${f.path.split('/').pop()}</td><td>${f.size_bytes}</td><td>${f.record_count}</td><td><button class="btn primary" onclick="window.location.href='${API}/recorder/download?path=${f.path}'">Download</button></td></tr>`).join('');
    } catch(e) { list.innerHTML = '<tr><td colspan="4">Error loading files</td></tr>'; }
}

setInterval(async () => {
    if (document.getElementById('status-tag').classList.contains('offline')) return;
    try {
        const res = await fetch(`${API}/live_debug`);
        const d = await res.json();
        document.getElementById('val-fps').innerText = d.process_hz.toFixed(1);
        document.getElementById('val-dropped').innerText = d.integrity.dropped_frames;
        document.getElementById('val-samples').innerText = d.sample_count;
    } catch(e) {}
}, 500);

async function fetchDiagnostics() {
    try {
        document.getElementById('diag-health').innerText = JSON.stringify(await (await fetch(`${API}/live_debug`)).json(), null, 2);
        document.getElementById('diag-sensor').innerText = JSON.stringify(await (await fetch(`${API}/bmi270_bridge_state`)).json(), null, 2);
    } catch(e) {}
}

window.onload = () => { initCharts(); connectWebSocket(); loadSettings(); refreshFileList(); };

