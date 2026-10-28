// StatusPage.h
// The single HTML page served at "/". It polls /api/status and /api/log
// every two seconds; all formatting happens in the browser.

#pragma once

#include <Arduino.h>

static const char kStatusPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garage Door Sensor</title>
<style>
:root{--bg:#f4f4f2;--card:#fff;--text:#1d1d1b;--muted:#6b6b66;--line:#ddddd8;
--ok:#1f7a4d;--okbg:#e3f3ea;--warn:#9a5b00;--warnbg:#fcefd9;--bad:#a32d2d;--badbg:#fbe5e5}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#22221f;--text:#ecece8;--muted:#a3a39c;
--line:#3a3a36;--ok:#7fd3a6;--okbg:#173a28;--warn:#f2c06b;--warnbg:#3d2c10;--bad:#f09595;--badbg:#3d1717}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 -apple-system,system-ui,sans-serif}
main{max-width:860px;margin:0 auto;padding:16px}
h1{font-size:20px;font-weight:600;margin:4px 0 12px}
.door{display:flex;align-items:center;gap:14px;background:var(--card);border:1px solid var(--line);
border-radius:12px;padding:16px;margin-bottom:12px}
.badge{font-size:22px;font-weight:600;padding:6px 16px;border-radius:8px}
.closed{color:var(--ok);background:var(--okbg)}.open{color:var(--warn);background:var(--warnbg)}
.unk{color:var(--muted);background:var(--line)}
.sub{color:var(--muted);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:8px;margin-bottom:12px}
.cell{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.cell .k{color:var(--muted);font-size:12px}.cell .v{font-weight:600;word-break:break-all}
.cell.wide{grid-column:span 2}
.bar{display:flex;align-items:center;gap:8px;margin:6px 0}
.bar h2{font-size:16px;font-weight:600;margin:0;flex:1}
button{font:inherit;font-size:13px;color:var(--text);background:var(--card);border:1px solid var(--line);
border-radius:8px;padding:5px 10px;cursor:pointer}
#log{background:var(--card);border:1px solid var(--line);border-radius:10px;height:50vh;overflow:auto;
padding:8px 10px;font:12.5px/1.55 ui-monospace,Menlo,monospace;white-space:pre-wrap}
#log .t{color:var(--muted)}
#conn{font-size:13px;padding:2px 8px;border-radius:6px}
.on{color:var(--ok);background:var(--okbg)}.off{color:var(--bad);background:var(--badbg)}
</style></head><body><main>
<div class="bar"><h1 style="flex:1;margin:0">Garage Door Sensor</h1><span id="conn" class="off">connecting</span></div>
<div class="door"><span id="door" class="badge unk">-</span>
<div><div id="since"></div><div class="sub" id="changes"></div></div></div>
<div class="grid" id="grid"></div>
<div class="bar"><h2>Event log</h2>
<button id="rst">Reset</button><button id="pause">Pause</button><button id="dl">Download</button></div>
<div id="log"></div>
</main><script>
const $=id=>document.getElementById(id);
let lastSeq=0,paused=false,lines=[],online=null,busy=false;
function tmo(url,opt){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);
 return fetch(url,{...opt,cache:'no-store',signal:c.signal}).finally(()=>clearTimeout(t))}
function addLine(text){const ts=new Date().toLocaleString([], {day:'2-digit',month:'short',hour:'2-digit',minute:'2-digit',second:'2-digit'});
 lines.push(`${ts}  ${text}`);const el=$('log'),atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
 const d=document.createElement('div');d.innerHTML=`<span class="t">${ts}</span>  ${esc(text)}`;el.appendChild(d);
 if(atEnd)el.scrollTop=el.scrollHeight}
function setOnline(ok){if(ok===online)return;
 if(online!==null)addLine(ok?'Connection restored':'Connection lost: device not responding');
 online=ok;$('conn').textContent=ok?'live':'not responding';$('conn').className=ok?'on':'off'}
function dur(s){s=Math.floor(s);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),
m=Math.floor(s%3600/60);return d?`${d}d ${h}h ${m}m`:h?`${h}h ${m}m`:m?`${m}m ${s%60}s`:`${s}s`}
function stamp(epoch,now,t){if(epoch>0){const d=new Date((epoch-(now-t)/1000)*1000);
return d.toLocaleString([], {day:'2-digit',month:'short',hour:'2-digit',minute:'2-digit',second:'2-digit'})}
return '+'+dur(t/1000)}
function cell(k,v,c){return `<div class="cell${c?' '+c:''}"><div class="k">${k}</div><div class="v">${v}</div></div>`}
function esc(s){return s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
async function status(){
 const r=await tmo('/api/status');const s=await r.json();
 const b=$('door');b.textContent=s.doorClosed?'Closed':'Open';b.className='badge '+(s.doorClosed?'closed':'open');
 $('since').textContent=s.lastChangeAgoS>=0?`for ${dur(s.lastChangeAgoS)}`:'no change since boot';
 $('changes').textContent=`${s.doorChanges} change${s.doorChanges==1?'':'s'} since boot`;
 const rssi=s.rssi?`${s.rssi} dBm (${s.rssi>-60?'strong':s.rssi>-70?'good':s.rssi>-80?'weak':'very weak'})`:'-';
 $('grid').innerHTML=cell('Uptime',dur(s.uptimeMs/1000))+cell('Time',s.epoch?new Date(s.epoch*1000).toLocaleString():'not synced')
 +cell('Wi-Fi',s.wifi?esc(s.ssid||'connected'):'disconnected')+cell('Signal',rssi)+cell('IP address',s.ip||'-')
 +cell('Matter',s.commissioned?'Paired':'Not paired')+cell('Boot count',s.bootCount)
 +cell('Last reset',esc(s.resetReason))+cell('Free memory',`${(s.freeHeap/1024).toFixed(0)} KB (min ${(s.minFreeHeap/1024).toFixed(0)} KB)`)
 +cell('Firmware',esc(s.firmware),'wide');}
async function log(){
 const r=await tmo('/api/log?since='+lastSeq);const j=await r.json();
 if(j.latest<lastSeq){lastSeq=0;lines=[];$('log').innerHTML='';return}
 const el=$('log'),atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
 for(const e of j.entries){const ts=stamp(j.epoch,j.now,e.t);lines.push(`${ts}  ${e.m}`);
  const d=document.createElement('div');d.innerHTML=`<span class="t">${ts}</span>  ${esc(e.m)}`;el.appendChild(d);lastSeq=e.s}
 while(el.childNodes.length>500)el.removeChild(el.firstChild);
 if(atEnd)el.scrollTop=el.scrollHeight;}
async function tick(){if(paused||busy)return;busy=true;
 try{await status();await log();setOnline(true)}
 catch(e){setOnline(false)}
 finally{busy=false}}
$('rst').onclick=async()=>{if(!confirm('Reset the boot count to 0?'))return;
 try{await fetch('/api/reset-boot-count',{method:'POST'});await status()}catch(e){}};
$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Resume':'Pause'};
$('dl').onclick=()=>{const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob([lines.join('\n')+'\n'],{type:'text/plain'}));
 a.download='garage-door-log.txt';a.click()};
tick();setInterval(tick,2000);
</script></body></html>)HTML";
