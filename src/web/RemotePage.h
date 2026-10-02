#pragma once

// The web remote's page, served as is from flash. One file, no external resources: it must
// work with no Internet. Channel names come from the SD and are only ever set as text.

constexpr char REMOTE_PAGE[] = R"PAGE(<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#1d1c1a">
<title>RETROTV · Mando</title>
<meta name="apple-mobile-web-app-title" content="RETROTV">
<style>
:root{--bg:#ece6da;--body:#1d1c1a;--body2:#2c2a26;--key:#34312d;--key-hi:#48443e;--ink:#ece6da;--dim:#b5ada1;
--lcd:#b3c6a0;--lcd-ink:#1b2715;--red:#d9452b;--amber:#f0a92e}
@media (prefers-color-scheme:dark){:root{--bg:#121110}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0}
body{min-height:100vh;background:radial-gradient(120% 70% at 50% 0%,#fff3 0%,transparent 60%),var(--bg);
font:15px/1.3 system-ui,-apple-system,"Segoe UI",sans-serif;display:flex;justify-content:center;
padding:24px 16px calc(24px + env(safe-area-inset-bottom))}
.remote{width:100%;min-width:0;max-width:340px;align-self:flex-start;color:var(--ink);padding:22px 20px 24px;
border-radius:46px 46px 36px 36px;background:linear-gradient(165deg,var(--body2),var(--body) 45%);
box-shadow:0 32px 60px -22px #000a,inset 0 2px 0 #ffffff12,inset 0 -3px 0 #0007}
.top{display:flex;justify-content:space-between;align-items:center;margin:0 8px 14px}
.brand{font-weight:800;letter-spacing:.34em;font-size:12px;color:var(--dim)}
.led{width:8px;height:8px;border-radius:50%;background:#4a1a12;box-shadow:inset 0 1px 1px #0009}
.pw{width:34px;height:34px;border-radius:50%;font-size:17px;line-height:34px;color:var(--red);
background:linear-gradient(180deg,var(--key-hi),var(--key));box-shadow:0 2px 0 #0008}
.pw:active{transform:translateY(2px);box-shadow:none}
.led.on{background:var(--red);box-shadow:0 0 10px var(--red)}
.lcd{position:relative;min-height:76px;padding:10px 14px;border-radius:10px;color:var(--lcd-ink);
font-family:ui-monospace,"SF Mono",Menlo,Consolas,monospace;background:linear-gradient(180deg,#c6d6b4,var(--lcd));
box-shadow:inset 0 2px 7px #0006}
.lcd.err{background:linear-gradient(180deg,#e0c2b0,#cfa892);color:#3a150c}
.ch{font-size:28px;font-weight:700;letter-spacing:.04em}
.nm{font-size:13px;text-transform:uppercase;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;padding-right:64px}
.vol{position:absolute;right:12px;top:12px;font-size:12px;font-weight:700}
.bat{position:absolute;right:12px;bottom:10px;font-size:12px;font-weight:700}
.bat.low{color:var(--red)}
.pad{display:grid;grid-template-columns:1fr 1fr;gap:14px;margin-top:20px}
.rocker{display:flex;flex-direction:column;gap:6px;padding:6px;border-radius:32px;background:var(--body);
box-shadow:inset 0 2px 5px #0009}
.rocker span{font-size:10px;letter-spacing:.22em;color:var(--dim);text-align:center}
button{font:inherit;color:inherit;border:0;cursor:pointer;touch-action:manipulation}
.k{height:60px;border-radius:26px;font-size:24px;background:linear-gradient(180deg,var(--key-hi),var(--key));
box-shadow:0 3px 0 #0008,inset 0 1px 0 #ffffff14;transition:transform .07s,box-shadow .07s}
.k:active{transform:translateY(2px);box-shadow:0 1px 0 #0008,inset 0 1px 0 #ffffff0d}
.k:focus-visible,.c:focus-visible{outline:3px solid var(--amber);outline-offset:2px}
.row{display:flex;gap:14px;margin-top:14px}
.row .k{flex:1;height:48px;font-size:12px;letter-spacing:.16em;font-weight:700}
.k.mute{background:linear-gradient(180deg,#e3593d,#b5321c);color:#fff}
.k.mute[aria-pressed=true]{background:linear-gradient(180deg,#f3b347,#c47f10);color:#2a1a05}
h2{margin:22px 8px 10px;font-size:11px;letter-spacing:.26em;font-weight:600;color:var(--dim)}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.c{display:flex;flex-direction:column;gap:2px;min-height:58px;padding:8px 9px;border-radius:12px;text-align:left;
background:var(--key);box-shadow:0 2px 0 #0007}
.c b{font-family:ui-monospace,Menlo,monospace;font-size:15px}
.c small{font-size:11px;line-height:1.2;color:var(--dim);overflow:hidden;display:-webkit-box;-webkit-line-clamp:2;
-webkit-box-orient:vertical}
.c.lg{padding:5px 5px 6px;gap:4px}
.c img{display:block;width:100%;height:auto;aspect-ratio:9/4;border-radius:8px}
.c.lg small{display:none}
.c.lg b{font-size:12px;padding:0 3px}
.c.live b::after{content:" \25CF";color:var(--red);font-size:9px;vertical-align:middle}
.c[aria-current=true]{background:var(--amber);color:#2a1a05}
.c[aria-current=true] small{color:#5a3a08}
.hint{margin:18px 0 0;font-size:11px;color:var(--dim);text-align:center}
.cfgbtn{margin-top:14px;width:100%;height:40px;border-radius:20px;font-size:11px;letter-spacing:.22em;font-weight:700;
color:var(--dim);background:transparent;box-shadow:inset 0 0 0 1px #ffffff26}
.cfgbtn:focus-visible,.x:focus-visible,.btn:focus-visible,.mini:focus-visible,.pick:focus-visible{outline:3px solid var(--amber);outline-offset:2px}
.cfg header{display:flex;justify-content:space-between;align-items:center;margin:0 4px 4px}
.cfg header b{letter-spacing:.32em;font-size:12px;color:var(--dim)}
.x{width:40px;height:40px;border-radius:20px;background:var(--key);font-size:20px}
.sec{margin-top:16px;padding:14px;border-radius:18px;background:#0000002e;box-shadow:inset 0 1px 3px #0008}
.sec h3{margin:0 0 10px;font-size:11px;letter-spacing:.26em;color:var(--amber);font-weight:700}
.sec p{margin:0 0 10px;font-size:13px;color:var(--dim)}
.fld{display:flex;flex-direction:column;gap:5px;margin:10px 0;font-size:12px;color:var(--dim)}
.fld input:not([type=range]){font:inherit;font-size:16px;color:var(--ink);background:#141312;border:1px solid #ffffff1f;
border-radius:10px;padding:10px 12px;width:100%}
.fld input:focus-visible{outline:3px solid var(--amber);outline-offset:1px}
.fld input[type=range]{width:100%;accent-color:var(--amber);height:28px}
.codebox{font-family:ui-monospace,Menlo,monospace!important;font-size:28px!important;letter-spacing:.5em;text-align:center}
.btn{height:42px;padding:0 18px;border-radius:21px;background:linear-gradient(180deg,var(--key-hi),var(--key));
font-size:12px;letter-spacing:.14em;font-weight:700;box-shadow:0 2px 0 #0008}
.btn.go{background:linear-gradient(180deg,#f3b347,#c47f10);color:#2a1a05}
.btn.warn{background:linear-gradient(180deg,#e3593d,#b5321c);color:#fff;width:100%;margin-top:12px}
.btns{display:flex;gap:10px;flex-wrap:wrap}
.list{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:6px}
.list li{display:flex;align-items:center;gap:8px;min-height:44px;padding:6px 10px;border-radius:10px;background:var(--key);font-size:13px}
.list li>span,.pick{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-align:left}
.pick{background:none;padding:6px 0}
.tag{font-size:10px;letter-spacing:.1em;color:var(--dim);white-space:nowrap}
.tag.on{color:var(--lcd)}
.mini{height:32px;padding:0 12px;border-radius:16px;background:#0000004d;font-size:11px;letter-spacing:.1em}
.msg{min-height:1.3em;margin:8px 2px 0;font-size:12px;color:var(--lcd)}
.msg.bad{color:#f4a08f}
.sw{appearance:none;-webkit-appearance:none;width:44px;height:26px;border-radius:13px;background:#0008;position:relative;
flex:none;box-shadow:inset 0 0 0 1px #ffffff26;cursor:pointer;margin:0}
.sw::after{content:"";position:absolute;top:4px;left:4px;width:18px;height:18px;border-radius:50%;background:var(--dim);transition:transform .15s}
.sw:checked{background:var(--amber)}
.sw:checked::after{transform:translateX(18px);background:#2a1a05}
.sw:focus-visible{outline:3px solid var(--amber);outline-offset:2px}
.chl{display:flex;align-items:center;gap:10px;flex:1;min-width:0}
.chl span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin:0;font-size:12px}
dt{color:var(--dim)}
dd{margin:0;text-align:right;font-family:ui-monospace,Menlo,monospace;overflow-wrap:anywhere}
.duo{display:flex;gap:10px}.duo .cfgbtn{flex:1}
.guide{list-style:none;margin:12px 0 0;padding:0;display:flex;flex-direction:column;gap:10px;min-width:0}
.guide li{min-width:0}
.gc{display:block;width:100%;overflow:hidden;text-align:left;padding:10px 12px 12px;border-radius:16px;background:var(--key);
box-shadow:0 2px 0 #0007}
.gc[aria-current=true]{box-shadow:0 0 0 2px var(--amber),0 2px 0 #0007}
.gc:focus-visible{outline:3px solid var(--amber);outline-offset:2px}
.gh{display:flex;align-items:center;gap:10px;margin-bottom:8px;min-width:0}
.gh img{width:90px;height:40px;border-radius:6px;flex:none}
.gh b{font-family:ui-monospace,Menlo,monospace;font-size:15px}
.gh span{font-size:12px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.now{display:flex;justify-content:space-between;gap:10px;font-family:ui-monospace,Menlo,monospace;font-size:13px;color:var(--lcd)}
.now span:first-child{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.now span:last-child{flex:none;color:var(--dim)}
.bar{height:5px;margin:6px 0 8px;border-radius:3px;background:#0008;overflow:hidden}
.bar i{display:block;height:100%;background:var(--amber)}
.nx{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:3px;font-size:12px;color:var(--dim)}
.nx li{display:flex;gap:10px;min-width:0}
.nx li span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.nx time{flex:none;font-family:ui-monospace,Menlo,monospace;color:var(--ink)}
.note{font-size:12px;letter-spacing:.08em;color:var(--dim)}.note.live{color:var(--red)}
[hidden]{display:none!important}
@media (prefers-reduced-motion:reduce){.k,.sw::after{transition:none}}
</style></head><body>
<main class="remote" id="rem" aria-label="Mando de RETROTV">
<div class="top"><span class="brand">RETROTV</span><span class="led" id="led" aria-hidden="true"></span>
<button class="pw" id="pw" aria-label="Apagar la tele">&#x23FB;</button></div>
<div class="lcd" id="lcd" role="status" aria-live="polite">
<div class="ch" id="ch">--</div><div class="nm" id="nm">Buscando la tele&hellip;</div><div class="vol" id="vol"></div><div class="bat" id="bat"></div></div>
<div class="pad">
<div class="rocker"><button class="k" data-k="next" aria-label="Canal siguiente">&#9650;</button><span>CANAL</span>
<button class="k" data-k="prev" aria-label="Canal anterior">&#9660;</button></div>
<div class="rocker"><button class="k" data-k="volup" aria-label="Subir volumen">+</button><span>VOLUMEN</span>
<button class="k" data-k="voldown" aria-label="Bajar volumen">&minus;</button></div>
</div>
<div class="row"><button class="k mute" id="mute" data-k="mute" aria-pressed="false">SILENCIO</button>
<button class="k" data-k="info" aria-label="Mostrar informaci&oacute;n en pantalla">INFO</button></div>
<h2 id="lh">CANALES</h2>
<div class="grid" id="grid" role="group" aria-labelledby="lh"></div>
<p class="hint">Teclado: &uarr;&darr; canal &middot; &larr;&rarr; volumen &middot; M silencio</p>
<div class="duo"><button class="cfgbtn" id="gopen">GU&Iacute;A</button><button class="cfgbtn" id="open">AJUSTES</button></div>
</main>
<section class="remote cfg" id="gd" hidden aria-labelledby="gdt">
<header><b id="gdt">GU&Iacute;A</b><button class="x" id="gclose" aria-label="Cerrar la gu&iacute;a">&times;</button></header>
<p class="msg" id="gmsg" role="status"></p><ul class="guide" id="glist"></ul></section>
<section class="remote cfg" id="cfg" hidden aria-labelledby="cfgt">
<header><b id="cfgt">AJUSTES</b><button class="x" id="close" aria-label="Cerrar ajustes">&times;</button></header>
<div class="sec" id="pair"><h3>EMPAREJAR ESTE M&Oacute;VIL</h3>
<p>Para tocar los ajustes, la tele ense&ntilde;a un c&oacute;digo en pantalla. Escr&iacute;belo aqu&iacute;.</p>
<button class="btn" id="showcode">MOSTRAR C&Oacute;DIGO</button>
<label class="fld">C&oacute;digo de la pantalla<input class="codebox" id="code" inputmode="numeric" pattern="[0-9]*" maxlength="4" autocomplete="one-time-code"></label>
<button class="btn go" id="enter">ENTRAR</button><p class="msg" id="pmsg" role="status"></p></div>
<div id="main" hidden>
<div class="sec"><h3>WI-FI</h3><ul class="list" id="nets"></ul>
<div class="btns" style="margin-top:10px"><button class="btn" id="scan">BUSCAR REDES</button></div>
<ul class="list" id="found" style="margin-top:8px"></ul>
<label class="fld">Nombre de la red (2,4&nbsp;GHz)<input id="ssid" autocomplete="off" autocapitalize="none" spellcheck="false" maxlength="32"></label>
<label class="fld">Contrase&ntilde;a<input type="password" id="pass" autocomplete="new-password" maxlength="63"></label>
<button class="btn go" id="add">A&Ntilde;ADIR RED</button><p class="msg" id="wmsg" role="status"></p></div>
<div class="sec"><h3>PANTALLA Y SONIDO</h3>
<label class="fld">Brillo <output id="bo"></output><input type="range" id="bri" min="10" max="100" step="10"></label>
<label class="fld">Volumen <output id="vo"></output><input type="range" id="vlm" min="0" max="100" step="5"></label>
<p class="msg" id="dmsg" role="status"></p></div>
<div class="sec"><h3>CANALES</h3><ul class="list" id="chs"></ul><p class="msg" id="cmsg" role="status"></p></div>
<div class="sec"><h3>INFORMACI&Oacute;N</h3><dl id="info"></dl>
<button class="btn warn" id="reboot">REINICIAR LA TELE</button><p class="msg" id="rmsg" role="status"></p></div>
</div></section>
<script>
const $=i=>document.getElementById(i);let fails=0,listVer=null;
const SCREENS={starting:'ARRANCANDO',switching:'CAMBIANDO',menu:'MENÚ',error:'ERROR'};
function blink(){const l=$('led');l.classList.add('on');setTimeout(()=>l.classList.remove('on'),160)}
async function get(p){const r=await fetch(p,{cache:'no-store'});if(!r.ok)throw r.status;return r.json()}
async function send(p){blink();if(navigator.vibrate)navigator.vibrate(8);
try{const r=await fetch(p,{method:'POST',headers:{'X-RETROTV':'1'}});if(!r.ok)throw r.status;setTimeout(poll,300)}
catch(e){down()}}
function down(){$('lcd').classList.add('err');$('ch').textContent='SIN SEÑAL';
$('nm').textContent='La tele no responde';$('vol').textContent='';$('bat').textContent=''}
function show(s){$('lcd').classList.remove('err');
if(listVer!==null&&s.list!==listVer)channels();listVer=s.list;
$('ch').textContent=s.channel!=null?'CH '+String(s.channel).padStart(2,'0'):'--';
const scr=SCREENS[s.screen];$('nm').textContent=scr?(scr+' '+s.name).trim():s.name;
$('vol').textContent=s.muted?'MUTE':'VOL '+s.volume;$('mute').setAttribute('aria-pressed',s.muted);
const b=$('bat');b.textContent=s.battery!=null?(s.charging?'CARGANDO ':s.battery_low?'BATERÍA BAJA ':'BAT ')+s.battery+'%'+(s.charging?' \u26A1':''):'';b.classList.toggle('low',!!s.battery_low&&!s.charging);
document.querySelectorAll('.c').forEach(b=>b.setAttribute('aria-current',+b.dataset.n===s.channel))}
async function poll(){try{show(await get('/api/state'));fails=0}catch(e){if(++fails>1)down()}}
async function channels(){try{const [d,st]=await Promise.all([get('/api/channels'),get('/api/state')]),g=$('grid');
g.textContent='';listVer=st.list;const lv='&v='+st.list;
if(!d.channels.length){g.textContent='Sin canales';return}
for(const c of d.channels){const b=document.createElement('button'),n=document.createElement('b'),t=document.createElement('small');
b.className='c'+(c.type==='remote'?' live':'');b.dataset.n=c.n;n.textContent=String(c.n).padStart(2,'0');
t.textContent=c.name;
if(c.logo){const i=document.createElement('img');i.src='/api/logo?n='+c.n+lv;i.alt='';i.width=180;i.height=80;
i.onerror=()=>{if(!i.dataset.r){i.dataset.r=1;setTimeout(()=>{i.src='/api/logo?n='+c.n+lv+'&r=1'},1000)}
else{i.remove();b.classList.remove('lg')}};b.classList.add('lg');b.append(i)}
b.append(n,t);b.setAttribute('aria-label','Canal '+c.n+', '+c.name);
b.onclick=()=>send('/api/channel?n='+c.n);g.append(b)}
poll()}catch(e){down();setTimeout(channels,3000)}}
document.querySelectorAll('[data-k]').forEach(b=>b.onclick=()=>send('/api/key?k='+b.dataset.k));
$('pw').onclick=()=>{if(confirm('¿Apagar la tele? Se vuelve a encender con cualquier tecla de la tele.'))send('/api/key?k=power')};
addEventListener('keydown',e=>{if(!$('cfg').hidden||e.target.tagName==='INPUT')return;const k={ArrowUp:'next',ArrowDown:'prev',ArrowRight:'volup',ArrowLeft:'voldown',
m:'mute',M:'mute',i:'info'}[e.key];if(k&&!e.repeat){e.preventDefault();send('/api/key?k='+k)}});
channels();setInterval(()=>{if(!document.hidden)poll()},2000);
let tok=null;try{tok=localStorage.getItem('pautv-cfg')}catch(e){}
function setTok(t){tok=t;try{t?localStorage.setItem('pautv-cfg',t):localStorage.removeItem('pautv-cfg')}catch(e){}}
function el(tag,text,cls){const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e}
function msg(id,t,bad){const m=$(id);m.textContent=t||'';m.classList.toggle('bad',!!bad)}
const ERR={'wrong or expired code':'C\u00f3digo incorrecto o caducado','bad code':'Escribe las 4 cifras',
'compiled into the firmware (secrets.h)':'Esta red va dentro del firmware: no se puede borrar desde aqu\u00ed',
'no SD card':'La tele no tiene SD','the TV did not answer':'La tele no ha respondido'};
async function cfg(p,body){const h={'X-RETROTV':'1'};if(tok)h['X-RETROTV-Token']=tok;
const o=body===undefined?{headers:h,cache:'no-store'}:{method:'POST',headers:h,body:JSON.stringify(body)};
let r;try{r=await fetch('/api/config/'+p,o)}catch(e){throw new Error('La tele no responde')}
let d={};try{d=await r.json()}catch(e){}
if(r.status===401){setTok(null);openPair();throw new Error('Vuelve a emparejar el m\u00f3vil')}
if(!r.ok)throw new Error(ERR[d.error]||d.error||('Error '+r.status));return d}
function openPair(){$('pair').hidden=false;$('main').hidden=true}
$('open').onclick=()=>{$('rem').hidden=true;$('cfg').hidden=false;scrollTo(0,0);tok?loadAll():openPair()};
$('close').onclick=()=>{$('cfg').hidden=true;$('rem').hidden=false};
$('showcode').onclick=async()=>{try{await cfg('pair/start',{});msg('pmsg','Mira la pantalla de la tele');$('code').focus()}
catch(e){msg('pmsg',e.message,1)}};
$('enter').onclick=async()=>{try{const d=await cfg('pair',{code:$('code').value.trim()});setTok(d.token);$('code').value='';
msg('pmsg','');loadAll()}catch(e){msg('pmsg',e.message,1)}};
async function loadAll(){$('pair').hidden=true;$('main').hidden=false;
try{const [i,w,d,c]=await Promise.all([cfg('info'),cfg('wifi'),cfg('display'),cfg('channels')]);
renderInfo(i);renderNets(w.networks);renderDisplay(d);renderChs(c.channels)}catch(e){if(tok)msg('wmsg',e.message,1)}}
function renderNets(list){const u=$('nets');u.textContent='';if(!list.length)u.append(el('li','Ninguna red guardada'));
for(const n of list){const l=el('li');l.append(el('span',n.ssid),el('small',n.connected?'CONECTADA':(n.removable?'SD':'FIRMWARE'),'tag'+(n.connected?' on':'')));
if(n.removable){const b=el('button','BORRAR','mini');b.setAttribute('aria-label','Borrar la red '+n.ssid);
b.onclick=async()=>{if(!confirm('\u00bfBorrar la red '+n.ssid+'?'))return;try{await cfg('wifi/remove',{ssid:n.ssid});
msg('wmsg','Red borrada');renderNets((await cfg('wifi')).networks)}catch(e){msg('wmsg',e.message,1)}};l.append(b)}
u.append(l)}}
function bars(r){return r>-55?'\u2582\u2584\u2586\u2588':r>-67?'\u2582\u2584\u2586':r>-78?'\u2582\u2584':'\u2582'}
$('scan').onclick=async()=>{msg('wmsg','Buscando redes\u2026');$('found').textContent='';try{await cfg('wifi/scan',{});
for(let i=0;i<15;i++){await new Promise(r=>setTimeout(r,1000));const d=await cfg('wifi/scan');if(d.done){renderFound(d.networks);
msg('wmsg',d.networks.length?'Toca una red para elegirla':'No se ve ninguna red de 2,4 GHz');return}}
msg('wmsg','La b\u00fasqueda no termin\u00f3',1)}catch(e){msg('wmsg',e.message,1)}};
function renderFound(list){const u=$('found');u.textContent='';for(const n of list){const l=el('li'),b=el('button',n.ssid,'pick');
b.onclick=()=>{$('ssid').value=n.ssid;$('pass').value='';$('pass').focus()};
l.append(b,el('small',(n.secure?'\ud83d\udd12 ':'')+bars(n.rssi)+(n.known?' \u00b7 GUARDADA':''),'tag'));u.append(l)}}
$('add').onclick=async()=>{const ssid=$('ssid').value.trim(),password=$('pass').value;
if(!ssid){msg('wmsg','Escribe el nombre de la red',1);return}
if(password&&password.length<8){msg('wmsg','La contrase\u00f1a necesita al menos 8 caracteres',1);return}
try{await cfg('wifi/add',{ssid,password});$('pass').value='';msg('wmsg','Guardada. La tele la usar\u00e1 cuando la tenga al alcance');
renderNets((await cfg('wifi')).networks)}catch(e){msg('wmsg',e.message,1)}};
function renderDisplay(d){$('bri').value=d.brightness;$('vlm').value=d.volume;$('bo').textContent=d.brightness+' %';$('vo').textContent=d.volume}
for(const [id,key,out,suf] of [['bri','brightness','bo',' %'],['vlm','volume','vo','']]){
$(id).oninput=()=>{$(out).textContent=$(id).value+suf};
$(id).onchange=async()=>{try{renderDisplay(await cfg('display',{[key]:+$(id).value}));msg('dmsg','Guardado')}catch(e){msg('dmsg',e.message,1)}}}
function renderChs(list){const u=$('chs');u.textContent='';for(const c of list){const l=el('li'),lab=el('label','','chl'),sw=el('input');
sw.type='checkbox';sw.className='sw';sw.checked=c.enabled;lab.append(sw,el('span',String(c.n).padStart(2,'0')+'  '+c.name));
sw.onchange=async()=>{sw.disabled=true;try{await cfg('channels',{n:c.n,enabled:sw.checked});
msg('cmsg',(sw.checked?'Activado: ':'Desactivado: ')+c.name);channels()}catch(e){sw.checked=!sw.checked;msg('cmsg',e.message,1)}sw.disabled=false};
l.append(lab);u.append(l)}}
function renderInfo(i){const d=$('info');d.textContent='';const h=Math.floor(i.uptime_s/3600),m=Math.floor(i.uptime_s%3600/60);
const rows=[['Versi\u00f3n',i.version],['Wi-Fi',i.ssid?i.ssid+' ('+i.rssi+' dBm)':i.wifi],['IP',i.ip||'\u2014'],['Mando',i.remote],
['Encendida',h+' h '+m+' min'],['SD',i.sd?(i.sd_used_mb/1024).toFixed(1)+' de '+(i.sd_total_mb/1024).toFixed(1)+' GB':'sin tarjeta'],
['Memoria',i.heap_kb+' KB libres (m\u00edn. '+i.heap_min_kb+')'],['PSRAM',i.psram_kb+' KB libres'],['Canales',i.channels]];
for(const [k,v] of rows)d.append(el('dt',k),el('dd',String(v)))}
if(location.hash==='#ajustes')$('open').click();
let gTimer=null,gData=null,gAt=0;
function hm(ms){return new Date(ms).toLocaleTimeString('es-ES',{hour:'2-digit',minute:'2-digit'})}
function ep(t){return /^\d+$/.test(t)?'Cap\u00edtulo '+t:t}
async function guide(){try{const d=await get('/api/guide');if(d.pending){msg('gmsg','Preparando la gu\u00eda\u2026');setTimeout(guide,1500);return}
gData=d;gAt=Date.now();msg('gmsg','');renderGuide();
if(d.channels.some(c=>c.note==='BUSCANDO...'))setTimeout(guide,3000)}catch(e){msg('gmsg','La tele no responde',1)}}
function renderGuide(){if(!gData)return;const off=gAt-gData.now*1000,now=Date.now(),u=$('glist'),cur=$('ch').textContent;
u.textContent='';for(const c of gData.channels){const li=el('li'),b=el('button','','gc'),h=el('div','','gh');
b.setAttribute('aria-current',cur==='CH '+String(c.n).padStart(2,'0'));b.setAttribute('aria-label','Canal '+c.n+', '+c.name);
b.onclick=()=>send('/api/channel?n='+c.n);
if(c.logo){const i=el('img');i.src='/api/logo?n='+c.n+'&v='+listVer;i.alt='';i.width=180;i.height=80;i.onerror=()=>i.remove();h.append(i)}
h.append(el('b',String(c.n).padStart(2,'0')),el('span',c.name));b.append(h);
if(c.items&&c.items.length){const a=c.items[0],s=a.s*1000+off,e=a.e*1000+off,row=el('div','','now'),bar=el('div','','bar'),f=el('i');
row.append(el('span',ep(a.t)),el('span','quedan '+Math.max(0,Math.round((e-now)/60000))+"'"));
f.style.width=Math.min(100,Math.max(0,(now-s)/(e-s)*100))+'%';bar.append(f);b.append(row,bar);
const nx=el('ul','','nx');for(const x of c.items.some(x=>x.t!==a.t)?c.items.slice(1):[]){const l=el('li');l.append(el('time',hm(x.s*1000+off)),el('span',ep(x.t)));nx.append(l)}
b.append(nx)}else b.append(el('div',c.note||'','note'+(c.type==='remote'?' live':'')));
li.append(b);u.append(li)}}
$('gopen').onclick=()=>{$('rem').hidden=true;$('gd').hidden=false;scrollTo(0,0);guide();clearInterval(gTimer);
gTimer=setInterval(()=>{if(document.hidden)return;poll();Date.now()-gAt>30000?guide():renderGuide()},10000)};
$('gclose').onclick=()=>{clearInterval(gTimer);$('gd').hidden=true;$('rem').hidden=false};
if(location.hash==='#guia')$('gopen').click();
$('reboot').onclick=async()=>{if(!confirm('\u00bfReiniciar la tele?'))return;try{await cfg('reboot',{});
msg('rmsg','Reiniciando\u2026 vuelve en unos segundos');setTimeout(()=>location.reload(),15000)}catch(e){msg('rmsg',e.message,1)}};
</script></body></html>)PAGE";
