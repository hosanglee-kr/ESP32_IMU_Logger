const API = "/api/t20";
let charts = {};
let lastHash = 0;
let isMeasuring = false;

// 탭 전환
function showTab(tabId) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    event.target.classList.add('active');
}

// 설정 불러오기
async function loadSettings() {
    const res = await fetch(`${API}/runtime_config`);
    const cfg = await res.json();
    const form = document.getElementById('form-config');
    form.hop_size.value = cfg.hop_size;
    form.mfcc_coeffs.value = cfg.mfcc_coeffs;
    form.noise_mode.value = cfg.noise_mode || 2;
    form.subtract_strength.value = cfg.subtract_strength || 1.0;
}

// 개별 설정 저장
async function saveSettings() {
    const form = document.getElementById('form-config');
    const data = {
        hop_size: parseInt(form.hop_size.value),
        mfcc_coeffs: parseInt(form.mfcc_coeffs.value),
        noise_mode: parseInt(form.noise_mode.value),
        subtract_strength: parseFloat(form.subtract_strength.value)
    };
    const res = await fetch(`${API}/runtime_config`, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
    });
    if(res.ok) alert("Settings applied!");
}

// 실시간 차트 갱신
async function updateCharts() {
    const res = await fetch(`${API}/viewer_data`);
    const d = await res.json();
    charts.wave.data.datasets[0].data = d.waveform;
    charts.spec.data.datasets[0].data = d.spectrum;
    charts.mfcc.data.datasets[0].data = [...d.mfcc, ...d.delta, ...d.delta2];
    Object.values(charts).forEach(c => c.update('none'));
}

// 파일 목록 갱신
async function refreshFileList() {
    const res = await fetch(`${API}/recorder_index`);
    const data = await res.json();
    const list = document.getElementById('file-list');
    list.innerHTML = data.items.map(f => `
        <tr>
            <td>${f.path.split('/').pop()}</td>
            <td>${(f.size_bytes/1024).toFixed(1)}</td>
            <td><button class="btn" onclick="location.href='${API}/recorder/download?path=${f.path}'">Down</button></td>
        </tr>
    `).join('');
}

// 메인 폴링 루프
async function poll() {
    try {
        const res = await fetch(`${API}/status`);
        const s = await res.json();
        if (s.hash !== lastHash) {
            updateCharts();
            lastHash = s.hash;
        }
        document.getElementById('status-tag').className = "tag online";
        document.getElementById('status-tag').innerText = "ONLINE";
    } catch {
        document.getElementById('status-tag').className = "tag offline";
    }
}

// 차트 초기화 (애니메이션 제거로 성능 확보)
function init() {
    const opt = { animation: false, responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } } };
    charts.wave = new Chart(document.getElementById('chart-waveform'), { type: 'line', data: { labels: Array(256).fill(''), datasets: [{ data: [], borderColor: '#4caf50', borderWidth: 1, pointRadius:0 }] }, options: opt });
    charts.spec = new Chart(document.getElementById('chart-spectrum'), { type: 'line', data: { labels: Array(129).fill(''), datasets: [{ data: [], borderColor: '#ff9800', borderWidth: 1, fill: true, backgroundColor: 'rgba(255,152,0,0.1)', pointRadius:0 }] }, options: opt });
    charts.mfcc = new Chart(document.getElementById('chart-mfcc'), { type: 'bar', data: { labels: Array(39).fill(''), datasets: [{ data: [], backgroundColor: '#2196f3' }] }, options: opt });
    
    setInterval(poll, 200);
    loadSettings();
}

window.onload = init;
