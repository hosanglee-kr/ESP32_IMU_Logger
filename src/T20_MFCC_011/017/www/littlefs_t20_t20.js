const qs=(s)=>document.querySelector(s);
async function getJson(url){ const r=await fetch(url); return await r.json(); }
async function refresh(){
  try{
    qs('#status').textContent = JSON.stringify(await getJson('/api/t20/status'), null, 2);
    qs('#config').textContent = JSON.stringify(await getJson('/api/t20/config'), null, 2);
    qs('#latest').textContent = JSON.stringify(await getJson('/api/t20/latest'), null, 2);
  }catch(e){
    qs('#status').textContent = 'error: ' + e;
  }
}
qs('#btnStart').onclick = async()=>{ await fetch('/api/t20/measurement/start',{method:'POST'}); refresh(); };
qs('#btnStop').onclick = async()=>{ await fetch('/api/t20/measurement/stop',{method:'POST'}); refresh(); };
qs('#btnRefresh').onclick = refresh;
refresh();
setInterval(refresh, 2000);
