// StatusPage.h
// The single HTML page served at "/". It polls /api/status and /api/log
// every two seconds; all formatting happens in the browser.

#pragma once

#include <Arduino.h>

static const char kStatusPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Garage Door Controller</title>
<style>
:root{--bg:#f4f4f2;--card:#fff;--text:#1d1d1b;--muted:#6b6b66;--line:#ddddd8;
--ok:#1f7a4d;--okbg:#e3f3ea;--warn:#9a5b00;--warnbg:#fcefd9;--bad:#a32d2d;--badbg:#fbe5e5}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#22221f;--text:#ecece8;--muted:#a3a39c;
--line:#3a3a36;--ok:#7fd3a6;--okbg:#173a28;--warn:#f2c06b;--warnbg:#3d2c10;--bad:#f09595;--badbg:#3d1717}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 -apple-system,system-ui,sans-serif}
main{max-width:860px;margin:0 auto;padding:16px}
h1{font-size:20px;font-weight:600;margin:4px 0 12px}
.door{display:flex;flex-wrap:wrap;align-items:center;gap:14px;background:var(--card);border:1px solid var(--line);
border-radius:12px;padding:16px;margin-bottom:12px}
.statuses{display:flex;flex-wrap:wrap;align-items:flex-start;gap:14px}
.badge{display:inline-block;font-size:22px;font-weight:600;padding:6px 16px;border-radius:8px}
.closed{color:var(--ok);background:var(--okbg)}.open{color:var(--warn);background:var(--warnbg)}
.status-red{color:#fff;background:#a32d2d}.status-blue{color:#fff;background:#1458a0}
.status-green{color:#fff;background:#1f7a4d}.status-transit{color:#fff;background:#865000}
.unk{color:var(--muted);background:var(--line)}
.door-details{flex-basis:100%}
.door-details-row{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.door-details-row #version{text-align:right}
.door-controls{margin-left:auto;align-self:flex-start;display:flex;flex-direction:column;align-items:flex-end}
.door-controls::before{content:'\00a0';font-size:13px}
button:disabled{opacity:.5;cursor:not-allowed}
.sub{color:var(--muted);font-size:13px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:8px;margin-bottom:12px}
.cell{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.cell .k{color:var(--muted);font-size:12px}.cell .v{font-weight:600;word-break:break-all}
.cell.wide{grid-column:span 2}
.bar{display:flex;align-items:center;gap:8px;margin:6px 0}
.bar h2{font-size:16px;font-weight:600;margin:0;flex:1}
button{font:inherit;font-size:13px;color:var(--text);background:var(--card);border:1px solid var(--line);
border-radius:8px;padding:5px 10px;cursor:pointer}
#press{font-size:22px;font-weight:600;color:#fff;background:#2878b5;border:0;padding:6px 16px}
#press:disabled{background:#777;color:#fff;opacity:1}
#control-toggle{color:#fff;border:0}
#control-toggle.control-disabled{background:#2e8b57}
#control-toggle.control-enabled{background:#2878b5}
#log{background:var(--card);border:1px solid var(--line);border-radius:10px;height:50vh;overflow:auto;
padding:8px 10px;font:12.5px/1.55 ui-monospace,Menlo,monospace;white-space:pre-wrap}
#log .t{color:var(--muted)}
#conn{font-size:13px;padding:2px 8px;border-radius:6px}
.on{color:var(--ok);background:var(--okbg)}.off{color:var(--bad);background:var(--badbg)}
</style></head><body><main>
<div class="bar"><h1 style="flex:1;margin:0">Garage Door Controller</h1><span id="conn" class="off">connecting</span></div>
<div class="door"><div class="statuses">
<div><div class="sub">Reed sensor</div><span id="door" class="badge unk">-</span></div>
<div><div class="sub">Control status</div><span id="control-state" class="badge unk">Unknown</span></div>
<div><div class="sub">Motion status</div><span id="motion" class="badge unk" role="status">Unknown</span></div>
</div>
<div class="door-controls"><button id="press" disabled title="Presses the opener's wall-button input; the action is an estimate">Open / Stop / Close</button>
<button id="control-toggle" disabled>Control: unknown</button>
<div id="control-error" class="sub" role="alert"></div></div>
<div class="door-details"><div class="door-details-row"><div id="since"></div><div id="version" class="sub"></div></div><div class="sub" id="changes"></div>
<div id="obstruction" class="sub"></div></div></div>
<div class="grid" id="grid"></div>
<div class="bar"><h2>Event log</h2>
<button id="rst">Reset</button><button id="pause">Pause</button><button id="dl">Download</button></div>
<div id="log"></div>
</main><script>
const $=id=>document.getElementById(id);
let lastSeq=0,paused=false,lines=[],online=null,busy=false,pressing=false,controlPending=true,
controlEnabled=false,controlChanging=false;
function updateControls(){
 $('press').disabled=online!==true||pressing||controlChanging||controlPending||!controlEnabled;
 $('control-toggle').disabled=online!==true||controlChanging;
 $('control-toggle').textContent=controlEnabled?'Control enabled: Disable':'Control disabled: Enable';
 $('control-toggle').className=controlEnabled?'control-enabled':'control-disabled';
}
function tmo(url,opt){const c=new AbortController(),t=setTimeout(()=>c.abort(),4000);
 return fetch(url,{...opt,cache:'no-store',signal:c.signal}).finally(()=>clearTimeout(t))}
function addLine(text){const ts=new Date().toLocaleString([], {day:'2-digit',month:'short',hour:'2-digit',minute:'2-digit',second:'2-digit'});
 lines.push(`${ts}  ${text}`);const el=$('log'),atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
 const d=document.createElement('div');d.innerHTML=`<span class="t">${ts}</span>  ${esc(text)}`;el.appendChild(d);
 if(atEnd)el.scrollTop=el.scrollHeight}
function setOnline(ok){if(ok===online)return;
 if(online!==null)addLine(ok?'Connection restored':'Connection lost: device not responding');
 online=ok;$('conn').textContent=ok?'live':'not responding';$('conn').className=ok?'on':'off';
 updateControls()}
function dur(s){s=Math.floor(s);const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),
m=Math.floor(s%3600/60);return d?`${d}d ${h}h ${m}m`:h?`${h}h ${m}m`:m?`${m}m ${s%60}s`:`${s}s`}
function stamp(epoch,now,t){if(epoch>0){const d=new Date((epoch-(now-t)/1000)*1000);
return d.toLocaleString([], {day:'2-digit',month:'short',hour:'2-digit',minute:'2-digit',second:'2-digit'})}
return '+'+dur(t/1000)}
function cell(k,v,c){return `<div class="cell${c?' '+c:''}"><div class="k">${k}</div><div class="v">${v}</div></div>`}
function esc(s){return s.replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
async function status(){
 const r=await tmo('/api/status');if(!r.ok)throw Error('Status unavailable');const s=await r.json();
 const b=$('door');b.textContent=s.doorClosed?'Closed':'Open';b.className='badge '+(s.doorClosed?'closed':'open');
 const control=$('control-state');control.textContent=s.doorState;
 control.className='badge '+({Stopped:'status-red',Open:'status-blue',Closed:'status-green',
 Opening:'status-transit',Closing:'status-transit',Moving:'status-transit'}[s.doorState]||'unk');
 const motion=$('motion');motion.textContent=s.doorMoving?'Moving':'Not moving';
 motion.className='badge '+(s.doorMoving?'status-red':'status-green');
 $('press').textContent=s.doorMoving?'Stop':s.doorClosed?'Open':'Close';
 $('obstruction').textContent=s.obstruction?'Possible obstruction or reversal':'';
 controlPending=s.controlPending;controlEnabled=s.controlEnabled===true;updateControls();
 $('since').textContent=s.lastChangeAgoS>=0?`for ${dur(s.lastChangeAgoS)}`:'no change since boot';
 $('version').textContent=`Version: ${s.firmware}`;
 $('changes').textContent=`${s.doorChanges} change${s.doorChanges==1?'':'s'} since boot`;
 const rssi=s.rssi?`${s.rssi} dBm (${s.rssi>-60?'strong':s.rssi>-70?'good':s.rssi>-80?'weak':'very weak'})`:'-';
 $('grid').innerHTML=cell('Uptime',dur(s.uptimeMs/1000))+cell('Time',s.epoch?new Date(s.epoch*1000).toLocaleString():'not synced')
 +cell('Wi-Fi',s.wifi?esc(s.ssid||'connected'):'disconnected')+cell('Signal',rssi)
 +cell('Device name',esc(s.deviceName||'-')+`<div class="sub">${esc(s.ip||'-')}</div>`)
 +cell('HomeKit',s.commissioned?'Paired':'Not paired')+cell('Boot count',s.bootCount)
 +cell('Last reset',esc(s.resetReason))+cell('HomeKit setup code',esc(s.homekitSetupCode))
 +cell('Free memory',`${(s.freeHeap/1024).toFixed(0)} KB (min ${(s.minFreeHeap/1024).toFixed(0)} KB)`)
 +cell('Firmware',esc(s.firmware),'wide');}
async function log(){
 const r=await tmo('/api/log?since='+lastSeq);const j=await r.json();
 if(j.latest<lastSeq){lastSeq=0;lines=[];$('log').innerHTML='';return}
 const el=$('log'),atEnd=el.scrollTop+el.clientHeight>=el.scrollHeight-20;
 for(const e of j.entries){const ts=stamp(j.epoch,j.now,e.t);lines.push(`${ts}  ${e.m}`);
  const d=document.createElement('div');d.innerHTML=`<span class="t">${ts}</span>  ${esc(e.m)}`;el.appendChild(d);lastSeq=e.s}
 while(el.childNodes.length>500)el.removeChild(el.firstChild);
 if(atEnd)el.scrollTop=el.scrollHeight;}
async function tick(){if(busy)return;busy=true;
 try{await status();if(!paused)await log();setOnline(true)}
 catch(e){setOnline(false)}
 finally{busy=false}}
$('rst').onclick=async()=>{if(!confirm('Reset the boot count to 0?'))return;
 try{await fetch('/api/reset-boot-count',{method:'POST'});await status()}catch(e){}};
$('press').onclick=async()=>{
 if(pressing)return;pressing=true;$('press').disabled=true;$('control-error').textContent='';
 try{const r=await tmo('/api/door/press',{method:'POST',headers:{'Content-Type':'application/json','X-Garage-Control':'press'},body:'press'});
  if(!r.ok)throw Error(await r.text());await status()}
 catch(e){$('control-error').textContent=`Command may not have completed: ${e.message}. Check the door before retrying.`;
  addLine(`Opener command error: ${e.message}`)}
 finally{pressing=false;updateControls()}};
$('control-toggle').onclick=async()=>{
 if(controlChanging)return;controlChanging=true;updateControls();$('control-error').textContent='';
 const value=controlEnabled?'disable':'enable';
 try{const r=await tmo('/api/door/control',{method:'POST',
  headers:{'Content-Type':'application/json','X-Garage-Control':'control'},body:value});
  if(!r.ok)throw Error(await r.text());await status()}
 catch(e){controlEnabled=false;$('control-error').textContent=`Control setting error: ${e.message}. Check status before retrying.`;
  addLine(`Control setting error: ${e.message}`)}
 finally{controlChanging=false;updateControls()}};
$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Resume':'Pause'};
$('dl').onclick=()=>{const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob([lines.join('\n')+'\n'],{type:'text/plain'}));
 a.download='garage-door-log.txt';a.click()};
tick();setInterval(tick,2000);
</script></body></html>)HTML";
