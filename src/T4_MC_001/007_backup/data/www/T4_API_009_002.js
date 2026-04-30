// ==========================================
// T4_API_009_002.js (Network & API Engine)
// ==========================================
const API_BASE = "/api";

const SMEA_API = {
	postCommand: async (path) => {
		return await fetch(`${API_BASE}${path}`, { method: 'POST' });
	},
	
	fetchDiagnostics: async () => {
		const res = await fetch(`${API_BASE}/status`);
		if (!res.ok) throw new Error("API Offline");
		return await res.json();
	},
	
	loadRuntimeConfig: async () => {
		const res = await fetch(`${API_BASE}/runtime_config`);
		if (!res.ok) throw new Error("Config not found");
		return await res.json();
	},
	
	saveRuntimeConfig: async (configObj) => {
		const res = await fetch(`${API_BASE}/runtime_config`, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify(configObj)
		});
		return res.ok;
	},
	
	downloadBackup: async () => {
		const res = await fetch(`${API_BASE}/runtime_config`);
		const cfg = await res.json();
		const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(cfg, null, 2));
		const a = document.createElement('a');
		a.href = dataStr;
		a.download = "smea100_backup.json";
		a.click();
	},
	
	uploadOTA: (file, onProgress, onSuccess, onFail) => {
		const formData = new FormData();
		formData.append("update", file);
		const xhr = new XMLHttpRequest();
		xhr.open("POST", `${API_BASE}/ota`, true);
		xhr.upload.onprogress = onProgress;
		xhr.onload = () => {
			if (xhr.status === 200) onSuccess();
			else onFail();
		};
		xhr.onerror = onFail;
		xhr.send(formData);
	}
};