/* app_218_008.js */

const API = "/api/t20";
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

    // 전역 event 객체 대신 매개변수 e를 사용합니다.
    // target 대신 currentTarget을 사용하여 버튼 내부 텍스트 클릭 시에도 정확히 버튼에 active 클래스가 붙도록 합니다.
    if(e && e.currentTarget) {
        e.currentTarget.classList.add('active');
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

function toggleWifiFields() {
    const mode = document.getElementById("wifi_mode").value;
    const apSection = document.getElementById("section_ap_config");
    const staSection = document.getElementById("section_sta_config");

    if (mode === "0") {
        apSection.style.display = "none";
        staSection.style.display = "block";
    } else if (mode === "1") {
        apSection.style.display = "block";
        staSection.style.display = "none";
    } else {
        apSection.style.display = "block";
        staSection.style.display = "block";
    }
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
             <input type="text" name="multi_dns1_${idx}" placeholder="DNS1" style="width:48%;">
             <input type="text" name="multi_dns2_${idx}" placeholder="DNS2" style="width:48%;">
        </div>
    </div>`;
}

function initUI() {
    const container = document.getElementById('routers_container');
    if(container) { container.innerHTML = [0, 1, 2].map(renderRouterHTML).join(''); }
    toggleWifiFields();
}

// ==========================================
// 2. Chart.js & Waterfall 초기화
// ==========================================
function initCharts() {
    const opt = { animation: false, responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, elements: { point: { radius: 0 } } };
    const mfccColors = Array(13).fill('#4caf50').concat(Array(13).fill('#ff9800')).concat(Array(13).fill('#f44336'));

    charts.wave = new Chart(document.getElementById('chart-waveform'), { type: 'line', data: { labels: Array(256).fill(''), datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1 }] }, options: { ...opt, scales: { y: { min: -2.0, max: 2.0 } } } });
    charts.spec = new Chart(document.getElementById('chart-spectrum'), { type: 'line', data: { labels: Array(129).fill(''), datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)' }] }, options: opt });
    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { type: 'bar', data: { labels: Array(39).fill(''), datasets: [{ data: [], backgroundColor: mfccColors }] }, options: opt });
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
const waterfallMfccStatic = new WaterfallChart('chart-waterfall-mfcc-static', 13, -20.0, 20.0);
const waterfallMfccDelta = new WaterfallChart('chart-waterfall-mfcc-delta', 13, -5.0, 5.0);
const waterfallMfccDeltaDelta = new WaterfallChart('chart-waterfall-mfcc-deltadelta', 13, -2.0, 2.0);

function drawMfccWaterfalls(floats39) {
    waterfallMfccStatic.draw(floats39.slice(0, 13));
    waterfallMfccDelta.draw(floats39.slice(13, 26));
    waterfallMfccDeltaDelta.draw(floats39.slice(26, 39));
}

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

            if (floats.length === 39) { // Single Vector
                charts.mfcc.data.datasets[0].data = floats;
                charts.mfcc.update('none');
                drawMfccWaterfalls(floats);
            }
            else if (floats.length === 624) { // Sequence Tensor
                const latestFrame = floats.subarray(624 - 39, 624);
                charts.mfcc.data.datasets[0].data = latestFrame;
                charts.mfcc.update('none');

                for(let i=0; i<16; i++) {
                    drawMfccWaterfalls(floats.slice(i * 39, (i + 1) * 39));
                }
            }
            else if (floats.length === 424) { // Legacy
                const waveData = floats.slice(0, 256), specData = floats.slice(256, 385), mfccData = floats.slice(385, 424);
                charts.wave.data.datasets[0].data = waveData; charts.spec.data.datasets[0].data = specData; charts.mfcc.data.datasets[0].data = mfccData;
                charts.wave.update('none'); charts.spec.update('none'); charts.mfcc.update('none');
                waterfallSpec.draw(specData); drawMfccWaterfalls(mfccData);
            }
        }
    };
    socket.onclose = () => { document.getElementById('status-tag').className = "tag offline"; document.getElementById('status-tag').innerText = "WS DISCONNECTED"; setTimeout(connectWebSocket, 2000); };
}

// ==========================================
// 4. API 제어 및 동적 설정 (중첩 JSON 지원)
// ==========================================
document.getElementById('btn-start').onclick = () => fetch(`${API}/recorder_begin`, {method: 'POST'}).then(() => showToast("Measurement Started"));
document.getElementById('btn-stop').onclick = () => fetch(`${API}/recorder_end`, {method: 'POST'}).then(() => showToast("Measurement Stopped"));
document.getElementById('btn-learn').onclick = () => { isLearning = !isLearning; fetch(`${API}/noise_learn?active=${isLearning}`, {method: 'POST'}).then(() => showToast(isLearning ? "Noise Learning ON" : "Noise Learning OFF")); };

document.getElementById('btn-calibrate').onclick = async () => {
    showToast("Calibrating... Please keep the device perfectly flat.", false);
    document.getElementById('btn-calibrate').disabled = true;
    try {
        const res = await fetch(`${API}/calibrate`, {method: 'POST'});
        if ((await res.json()).ok) showToast("Calibration successful and saved!");
        else showToast("Calibration failed!", true);
    } catch(e) { showToast("Network error during calibration", true); }
    finally { document.getElementById('btn-calibrate').disabled = false; }
};

async function rebootDevice() {
    if (!confirm("Are you sure you want to reboot the device?")) return;
    try {
        await fetch(`${API}/reboot`, { method: 'POST' });
        showToast("Rebooting device... Please wait.");
        setTimeout(() => location.reload(), 6000);
    } catch(e) {
        showToast("Reboot command sent.", true);
        setTimeout(() => location.reload(), 6000);
    }
}

// 중첩된 JSON 값을 읽어오는 헬퍼 함수
const getNestedValue = (obj, path) => path.split('.').reduce((acc, part) => acc && acc[part] !== undefined ? acc[part] : undefined, obj);

async function loadSettings() {
    try {
        const res = await fetch(`${API}/runtime_config`);
        const cfg = await res.json();
        const form = document.getElementById('form-config');

        // 1. T20 Nested Config 자동 매핑
        form.querySelectorAll('select:not([name^="wifi_"]):not([name^="multi_"]), input:not([name^="wifi_"]):not([name^="multi_"])').forEach(el => {
            const val = getNestedValue(cfg, el.name);
            if (val !== undefined) {
                el.value = typeof val === 'boolean' ? val.toString() : val;
            }
        });

        // 2. Wi-Fi Config 매핑 (플랫)
        if (cfg.wifi_mode !== undefined) {
            form.wifi_mode.value = cfg.wifi_mode;
            toggleWifiFields();
        }
        if (cfg.wifi_ap_ssid !== undefined) form.wifi_ap_ssid.value = cfg.wifi_ap_ssid;
        if (cfg.wifi_ap_pass !== undefined) form.wifi_ap_pass.value = cfg.wifi_ap_pass;
        if (cfg.wifi_ap_ip !== undefined) form.wifi_ap_ip.value = cfg.wifi_ap_ip;

        if (cfg.wifi_multi_ap && Array.isArray(cfg.wifi_multi_ap)) {
            cfg.wifi_multi_ap.forEach((ap, idx) => {
                if (idx < 3) {
                    form[`multi_ssid_${idx}`].value = ap.ssid || "";
                    form[`multi_pass_${idx}`].value = ap.pass || "";
                    form[`multi_static_${idx}`].value = ap.use_static_ip ? "true" : "false";
                    form[`multi_ip_${idx}`].value = ap.local_ip || "";
                    form[`multi_gw_${idx}`].value = ap.gateway || "";
                    form[`multi_sn_${idx}`].value = ap.subnet || "";
                    form[`multi_dns1_${idx}`].value = ap.dns1 || "";
                    form[`multi_dns2_${idx}`].value = ap.dns2 || "";
                    document.getElementById(`rip_${idx}`).style.display = ap.use_static_ip ? 'flex' : 'none';
                }
            });
        }
    } catch(e) { console.error("Config load fail or no file yet", e); }
}

// 점(.) 표기법 키를 중첩 객체로 구성해주는 헬퍼 함수
function setNestedValue(obj, path, value) {
    const keys = path.split('.');
    let current = obj;
    for (let i = 0; i < keys.length - 1; i++) {
        if (!current[keys[i]]) current[keys[i]] = {};
        current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
}

async function saveSettings() {
    const formData = new FormData(document.getElementById('form-config'));
    const configData = {};
    const multiApArray = [];

    formData.forEach((value, key) => {
        let parsedVal = value;
        // 타입 캐스팅
        if (value === 'true') parsedVal = true;
        else if (value === 'false') parsedVal = false;
        else if (!isNaN(value) && value.trim() !== '') parsedVal = Number(value);

        if (key.startsWith('wifi_')) {
            configData[key] = parsedVal;
        } else if (!key.startsWith('multi_')) {
            // 중첩 JSON 구조로 삽입 (예: sensor.axis -> { sensor: { axis: 0 } })
            setNestedValue(configData, key, parsedVal);
        }
    });

    for(let i=0; i<3; i++) {
        const ssid = document.querySelector(`[name="multi_ssid_${i}"]`).value;
        if (ssid && ssid.trim() !== "") {
            multiApArray.push({
                ssid: ssid,
                pass: document.querySelector(`[name="multi_pass_${i}"]`).value,
                use_static_ip: document.querySelector(`[name="multi_static_${i}"]`).value === 'true',
                local_ip: document.querySelector(`[name="multi_ip_${i}"]`).value,
                gateway: document.querySelector(`[name="multi_gw_${i}"]`).value,
                subnet: document.querySelector(`[name="multi_sn_${i}"]`).value,
                dns1: document.querySelector(`[name="multi_dns1_${i}"]`).value,
                dns2: document.querySelector(`[name="multi_dns2_${i}"]`).value
            });
        }
    }
    configData.wifi_multi_ap = multiApArray;

    showToast("Applying settings... Please wait.", false);

    try {
        const res = await fetch(`${API}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData)
        });
        if(res.ok) setTimeout(() => showToast("Settings successfully saved!"), 500);
        else showToast("Failed to apply settings", true);
    } catch (e) { showToast("Network error", true); }
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

async function fetchDiagnostics() {
    try {
        const res = await fetch(`${API}/status`);
        const d = await res.json();

        document.getElementById('val-fps').innerText = "N/A (v218)";
        document.getElementById('val-dropped').innerText = "0";
        document.getElementById('val-samples').innerText = d.hash || "0";

        document.getElementById('diag-health').innerText = JSON.stringify(d, null, 2);
        document.getElementById('diag-sensor').innerText = d.sensor_status || "Active";
    } catch(e) { document.getElementById('diag-health').innerText = "Status API Error"; }
}

window.onload = () => {
    initUI();
    initCharts();
    connectWebSocket();
    loadSettings();
    refreshFileList();
};
