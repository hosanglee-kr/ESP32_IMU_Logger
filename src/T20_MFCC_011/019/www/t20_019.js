const qs=(s)=>document.querySelector(s);
async function getJson(url){ const r=await fetch(url); return await r.json(); }

function shortArrayText(obj, key, limit=32){
  if(!obj || !Array.isArray(obj[key])) return JSON.stringify(obj, null, 2);
  const copy = {...obj};
  copy[key] = obj[key].slice(0, limit);
  if(obj[key].length > limit) copy[key + '_truncated'] = true;
  return JSON.stringify(copy, null, 2);
}

async function refresh(){
  try{
    const status = await getJson('/api/t20/live/status');
    const config = await getJson('/api/t20/config');
    const latest = await getJson('/api/t20/live/latest');
    const wave = await getJson('/api/t20/live/wave');
    const sequence = await getJson('/api/t20/live/sequence');

    qs('#status').textContent = JSON.stringify(status, null, 2);
    qs('#configEdit').value = JSON.stringify(config, null, 2);
    qs('#latest').textContent = JSON.stringify(latest, null, 2);
    qs('#wave').textContent = shortArrayText(wave, 'samples', 64);
    qs('#sequence').textContent = shortArrayText(sequence, 'data', 96);
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
