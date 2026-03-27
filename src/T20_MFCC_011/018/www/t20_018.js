const qs=(s)=>document.querySelector(s);
async function getJson(url){ const r=await fetch(url); return await r.json(); }
async function refresh(){
  try{
    const status = await getJson('/api/t20/live/status');
    const config = await getJson('/api/t20/config');
    const latest = await getJson('/api/t20/live/latest');
    qs('#status').textContent = JSON.stringify(status, null, 2);
    qs('#configEdit').value = JSON.stringify(config, null, 2);
    qs('#latest').textContent = JSON.stringify(latest, null, 2);
  }catch(e){
    qs('#status').textContent = 'error: ' + e;
  }
}
qs('#btnStart').onclick = async()=>{ await fetch('/api/t20/measurement/start',{method:'POST'}); refresh(); };
qs('#btnStop').onclick = async()=>{ await fetch('/api/t20/measurement/stop',{method:'POST'}); refresh(); };
qs('#btnRefresh').onclick = refresh;
qs('#btnSaveConfig').onclick = async()=>{
  const body = qs('#configEdit').value;
  const r = await fetch('/api/t20/config', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body
  });
  const t = await r.text();
  alert(t);
  refresh();
};
refresh();
setInterval(refresh, 1000);
