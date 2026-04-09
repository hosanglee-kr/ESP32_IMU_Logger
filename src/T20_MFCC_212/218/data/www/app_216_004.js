/* app_216_004.js */

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
    // 3초 후 페이드아웃
    setTimeout(() => { toast.className = toast.className.replace("show", ""); }, 3000);
}

// ==========================================
// 2. Chart.js 초기화
// ==========================================
function initCharts() {
    const opt = { 
        animation: false, 
        responsive: true, 
        maintainAspectRatio: false, 
        plugins: { legend: { display: false } },
        elements: { point: { radius: 0 } }
    };
    
    charts.wave = new Chart(document.getElementById('chart-waveform'), { 
        type: 'line', 
        data: { labels: Array(256).fill(''), datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1 }] }, 
        options: { ...opt, scales: { y: { min: -2.0, max: 2.0 } } }
    });
    
    charts.spec = new Chart(document.getElementById('chart-spectrum'), { 
        type: 'line', 
        data: { labels: Array(129).fill(''), datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)' }] }, 
        options: opt 
    });
    
    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { 
        type: 'bar', 
        data: { labels: Array(39).fill(''), datasets: [{ data: [], backgroundColor: '#2196f3' }] }, 
        options: opt 
    });
}

// ==========================================
// 폭포수(Waterfall) 렌더링 클래스
// ==========================================
function getHeatmapColor(value, min, max) {
    let norm = (value - min) / (max - min);
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    
    const r = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.5))))));
    const g = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * (norm - 0.25))))));
    const b = Math.max(0, Math.min(255, Math.floor(255 * (1.5 - Math.abs(1 - 4 * norm)))));
    
    return `rgb(${r},${g},${b})`;
}

class WaterfallChart {
    constructor(canvasId, dataLen, minVal, maxVal) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas ? this.canvas.getContext('2d', { alpha: false, willReadFrequently: true }) : null;
        this.dataLen = dataLen;
        this.minVal = minVal;
        this.maxVal = maxVal;
        
        if (this.canvas) {
            this.canvas.width = 600;
            this.canvas.height = dataLen;
        }
    }

    draw(dataArray) {
        if (!this.ctx) return;
        const w = this.canvas.width;
        const h = this.canvas.height;

        this.ctx.drawImage(this.canvas, 1, 0, w - 1, h, 0, 0, w - 1, h);

        for (let y = 0; y < h; y++) {
            const val = dataArray[h - 1 - y];
            this.ctx.fillStyle = getHeatmapColor(val, this.minVal, this.maxVal);
            this.ctx.fillRect(w - 1, y, 1, 1);
        }
    }
}

const waterfallSpec = new WaterfallChart('chart-waterfall-spec', 129, 0.0, 15.0); 
const waterfallMfcc = new WaterfallChart('chart-waterfall-mfcc', 39, -20.0, 20.0);


// ==========================================
// 3. WebSocket 바이너리 스트리밍
// ==========================================
function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    socket = new WebSocket(`${protocol}//${window.location.host}${API}/ws`);
    socket.binaryType = "arraybuffer";

    socket.onopen = () => {
        document.getElementById('status-tag').className = "tag online";
        document.getElementById('status-tag').innerText = "WS CONNECTED";
    };

    socket.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
            const floats = new Float32Array(event.data);
            
            // [버그 수정] 배열을 명시적으로 분리하여 참조
            const waveData = floats.slice(0, 256);
            const specData = floats.slice(256, 385);
            const mfccData = floats.slice(385, 424);
            
            charts.wave.data.datasets[0].data = waveData;
            charts.spec.data.datasets[0].data = specData;
            charts.mfcc.data.datasets[0].data = mfccData;
            
            charts.wave.update('none');
            charts.spec.update('none');
            charts.mfcc.update('none');
            
            // 폭포수 차트 업데이트
            waterfallSpec.draw(specData);
            waterfallMfcc.draw(mfccData);
        }
    };

    socket.onclose = () => {
        document.getElementById('status-tag').className = "tag offline";
        document.getElementById('status-tag').innerText = "WS DISCONNECTED";
        setTimeout(connectWebSocket, 2000);
    };
}


// ==========================================
// 4. API 제어 및 동적 설정
// ==========================================
document.getElementById('btn-start').onclick = () => fetch(`${API}/recorder_begin`, {method: 'POST'}).then(() => showToast("Measurement Started"));
document.getElementById('btn-stop').onclick = () => fetch(`${API}/recorder_end`, {method: 'POST'}).then(() => showToast("Measurement Stopped"));
document.getElementById('btn-learn').onclick = () => {
    isLearning = !isLearning;
    fetch(`${API}/noise_learn?active=${isLearning}`, {method: 'POST'}).then(() => showToast(isLearning ? "Noise Learning ON" : "Noise Learning OFF"));
};

// [추가] 센서 캘리브레이션 요청
document.getElementById('btn-calibrate').onclick = async () => {
    showToast("Calibrating... Please keep the device perfectly flat.", false);
    document.getElementById('btn-calibrate').disabled = true; // 중복 방지

    try {
        const res = await fetch(`${API}/calibrate_sensor`, {method: 'POST'});
        const data = await res.json();
        if (data.ok) {
            showToast("Calibration successful and saved!");
        } else {
            showToast("Calibration failed!", true);
        }
    } catch(e) {
        showToast("Network error during calibration", true);
    } finally {
        document.getElementById('btn-calibrate').disabled = false;
    }
};

async function loadSettings() {
    try {
        const res = await fetch(`${API}/runtime_config`);
        const cfg = await res.json();
        const form = document.getElementById('form-config');
        
        // [수정] 센서 축 및 범위 파라미터 매핑
        if (cfg.sensor_axis !== undefined) form.sensor_axis.value = cfg.sensor_axis;
        if (cfg.accel_range !== undefined) form.accel_range.value = cfg.accel_range;
        if (cfg.gyro_range !== undefined) form.gyro_range.value = cfg.gyro_range;

        if (cfg.hop_size !== undefined) form.hop_size.value = cfg.hop_size;
        if (cfg.mfcc_coeffs !== undefined) form.mfcc_coeffs.value = cfg.mfcc_coeffs;
        if (cfg.pre_alpha !== undefined) form.pre_alpha.value = cfg.pre_alpha;
        if (cfg.filter_cutoff !== undefined) form.filter_cutoff.value = cfg.filter_cutoff;
        if (cfg.noise_mode !== undefined) form.noise_mode.value = cfg.noise_mode;
        if (cfg.sub_strength !== undefined) form.sub_strength.value = cfg.sub_strength;
    } catch(e) { console.error("Config load fail", e); }
}

async function saveSettings() {
    const formData = new FormData(document.getElementById('form-config'));
    const configData = {};
    
    formData.forEach((value, key) => {
        configData[key] = isNaN(value) || value === '' ? value : Number(value);
    });

    // UX: 센서 측정 범위가 바뀌면 하드웨어 재부팅(Re-init)으로 인해 약 150ms 멈추므로 토스트를 먼저 띄움
    showToast("Applying settings... Please wait.");

    try {
        const res = await fetch(`${API}/runtime_config`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(configData)
        });
        if(res.ok) {
            setTimeout(() => showToast("Settings successfully applied & saved!"), 500);
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
        list.innerHTML = data.items.map(f => `
            <tr>
                <td>${f.path.split('/').pop()}</td>
                <td>${f.size_bytes}</td>
                <td>${f.record_count}</td>
                <td><button class="btn primary" onclick="window.location.href='${API}/recorder/download?path=${f.path}'">Download</button></td>
            </tr>
        `).join('');
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
        const h_res = await fetch(`${API}/live_debug`);
        document.getElementById('diag-health').innerText = JSON.stringify(await h_res.json(), null, 2);
        
        const s_res = await fetch(`${API}/bmi270_bridge_state`);
        document.getElementById('diag-sensor').innerText = JSON.stringify(await s_res.json(), null, 2);
    } catch(e) {}
}

window.onload = () => {
    initCharts();
    connectWebSocket();
    loadSettings();
    refreshFileList();
};

