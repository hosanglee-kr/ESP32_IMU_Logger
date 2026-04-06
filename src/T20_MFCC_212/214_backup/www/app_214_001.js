const API_BASE = "/api/t20";
let lastHash = 0;
let charts = {};

// 1. 차트 초기화
function initCharts() {
    const ctxW = document.getElementById('chart-waveform').getContext('2d');
    charts.waveform = new Chart(ctxW, {
        type: 'line',
        data: { labels: Array(256).fill(0), datasets: [{ label: 'Amp', data: [], borderColor: '#4caf50', borderWidth: 1, pointRadius: 0 }] },
        options: { animation: false, scales: { y: { min: -1, max: 1 } } }
    });

    const ctxS = document.getElementById('chart-spectrum').getContext('2d');
    charts.spectrum = new Chart(ctxS, {
        type: 'line',
        data: { labels: Array(129).fill(0).map((_, i) => i), datasets: [{ label: 'dB', data: [], backgroundColor: 'rgba(76, 175, 80, 0.2)', borderColor: '#4caf50', fill: true, pointRadius: 0 }] },
        options: { animation: false }
    });

    const ctxM = document.getElementById('chart-mfcc').getContext('2d');
    charts.mfcc = new Chart(ctxM, {
        type: 'bar',
        data: { labels: Array(39).fill(0).map((_, i) => i), datasets: [{ label: 'Feature', data: [], backgroundColor: '#ff9800' }] },
        options: { animation: false }
    });
}

// 2. 상태 폴링 (해시 기반)
async function pollStatus() {
    try {
        const res = await fetch(`${API_BASE}/status`);
        const status = await res.json();

        // 해시가 다를 때만 무거운 데이터 요청
        if (status.hash !== lastHash) {
            updateDebugInfo();
            updateCharts();
            lastHash = status.hash;
        }
        document.getElementById('status-badge').innerText = "ONLINE";
        document.getElementById('status-badge').classList.add('online');
    } catch (e) {
        document.getElementById('status-badge').innerText = "OFFLINE";
        document.getElementById('status-badge').classList.remove('online');
    }
}

// 3. 차트 데이터 갱신
async function updateCharts() {
    const res = await fetch(`${API_BASE}/viewer_data`); // 백엔드 T20_buildViewerDataJsonText와 매칭
    const data = await res.json();

    // Waveform & Spectrum 갱신
    charts.waveform.data.datasets[0].data = data.waveform;
    charts.spectrum.data.datasets[0].data = data.spectrum;
    
    // 39차 벡터 통합 (MFCC+D+DD)
    charts.mfcc.data.datasets[0].data = [...data.mfcc, ...data.delta, ...data.delta2];
    
    Object.values(charts).forEach(c => c.update());
}

async function updateDebugInfo() {
    const res = await fetch(`${API_BASE}/live_debug`);
    const d = await res.json();
    document.getElementById('val-fps').innerText = d.process_hz.toFixed(1);
    document.getElementById('val-dropped').innerText = d.integrity.dropped_frames;
    document.getElementById('val-samples').innerText = d.sample_count;
}

// 4. 컨트롤 이벤트
document.getElementById('btn-start').onclick = () => fetch(`${API_BASE}/recorder_begin`, {method: 'POST'});
document.getElementById('btn-stop').onclick = () => fetch(`${API_BASE}/recorder_end`, {method: 'POST'});
document.getElementById('btn-learn').onclick = () => {
    const active = true; // 토글 로직 추가 가능
    fetch(`${API_BASE}/noise_learn?active=${active}`, {method: 'POST'});
};

// 초기화 실행
window.onload = () => {
    initCharts();
    setInterval(pollStatus, 200); // 200ms 주기로 상태 감시
};

