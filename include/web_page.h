#pragma once

#include <Arduino.h>

// Single page console served from PROGMEM. Effect / palette / transition lists
// are fetched from /api/meta so the firmware stays the only source of truth.
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>WS2812 灯带控制台</title><style>
:root{--bg:#05070d;--card:#101827;--card2:#0d1420;--line:#22304a;--text:#e9f0f8;--muted:#8ba0bb;
--ac:#5eead4;--ac2:#a78bfa;--warm:#fbbf24;--red:#fb7185}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:radial-gradient(900px 500px at 8% -8%,#1b3350 0,transparent 60%),radial-gradient(700px 400px at 100% 0,#2a1b45 0,transparent 55%),var(--bg);
color:var(--text);font:14px/1.5 system-ui,-apple-system,"Microsoft YaHei",sans-serif;padding-bottom:34px}
main{max-width:960px;margin:auto;padding:16px}
h1{font-size:20px;margin:0;letter-spacing:.5px}
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:12px}
.chips{display:flex;gap:7px;flex-wrap:wrap}
.chip{padding:5px 11px;border-radius:99px;background:#16223558;border:1px solid var(--line);color:var(--muted);font-size:12px}
.chip.on{border-color:#2dd4bf66;background:#0f3a3a;color:#7ff0dd;box-shadow:0 0 16px #2dd4bf30}
.chip.warn{border-color:#fb718566;background:#3a1420;color:#ffb0bc}
.card{background:linear-gradient(180deg,#111c2ecc,#0c1420cc);border:1px solid var(--line);border-radius:16px;padding:14px;margin-bottom:12px;
box-shadow:0 10px 30px #0006;backdrop-filter:blur(6px)}
.card h2{font-size:13px;margin:0 0 12px;color:var(--muted);font-weight:600;letter-spacing:1px;text-transform:uppercase}
#strip{width:100%;height:34px;border-radius:10px;display:block;image-rendering:pixelated;background:#000;
border:1px solid #1d2a40;box-shadow:0 0 26px #0af3,inset 0 0 20px #0006}
.pv{display:flex;align-items:center;gap:10px}
.pw{flex:0 0 auto;width:64px;height:64px;border-radius:18px;border:1px solid var(--line);background:#131f31;color:var(--muted);
font-size:24px;cursor:pointer;transition:.2s}
.pw.on{background:linear-gradient(160deg,#0d9488,#7c3aed);color:#fff;border-color:#5eead480;box-shadow:0 0 22px #14b8a666}
.sl{display:grid;grid-template-columns:74px 1fr 46px;gap:10px;align-items:center;margin:9px 0}
.sl label{color:var(--muted);font-size:13px}
.sl b{text-align:right;font-variant-numeric:tabular-nums;font-weight:600}
input[type=range]{-webkit-appearance:none;appearance:none;height:22px;background:transparent;width:100%}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:6px;background:linear-gradient(90deg,#5eead4,#a78bfa)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;margin-top:-7px;border-radius:50%;
background:#fff;border:2px solid #0f172a;box-shadow:0 2px 8px #000a}
input[type=range]::-moz-range-track{height:6px;border-radius:6px;background:linear-gradient(90deg,#5eead4,#a78bfa)}
input[type=range]::-moz-range-thumb{width:18px;height:18px;border-radius:50%;background:#fff;border:2px solid #0f172a}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(96px,1fr));gap:7px}
.fx{padding:9px 6px;border-radius:10px;border:1px solid var(--line);background:#131e2f;color:#cfe0f0;font-size:13px;cursor:pointer;
text-align:center;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fx.on{background:linear-gradient(150deg,#0e7490,#6d28d9);color:#fff;border-color:#67e8f9aa;box-shadow:0 0 14px #22d3ee55}
.pal{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:7px}
.pc{border-radius:10px;border:1px solid var(--line);cursor:pointer;overflow:hidden;background:#131e2f}
.pc i{display:block;height:16px}
.pc span{display:block;padding:4px 6px;font-size:12px;color:#cfe0f0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.pc.on{border-color:#fff9;box-shadow:0 0 12px #fff3}
.row{display:flex;flex-wrap:wrap;gap:9px;align-items:center}
.f{display:grid;gap:5px;min-width:126px;flex:1}
.f span{color:var(--muted);font-size:12px}
select,input[type=number],input[type=text],input[type=password]{width:100%;padding:8px;border-radius:9px;border:1px solid #2a3a55;
background:#0b1220;color:var(--text);font-size:14px}
input[type=color]{width:100%;height:38px;padding:2px;border-radius:9px;border:1px solid #2a3a55;background:#0b1220}
button{padding:9px 13px;border-radius:10px;border:1px solid #2a3a55;background:#16233a;color:var(--text);cursor:pointer;font-size:13px}
button:active{transform:scale(.97)}
button.p{background:linear-gradient(140deg,#0d9488,#4f46e5);border-color:#5eead466}
button.w{background:#3a2a12;border-color:#fbbf2455;color:#fde68a}
.tg{display:flex;align-items:center;gap:8px;padding:8px 11px;border-radius:10px;border:1px solid var(--line);background:#131e2f;cursor:pointer;font-size:13px}
.tg.on{background:#0f3a37;border-color:#2dd4bf66;color:#8af0e0}
.tg i{width:32px;height:18px;border-radius:99px;background:#31415c;position:relative;transition:.2s;flex:0 0 auto}
.tg i::after{content:"";position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;background:#93a6bf;transition:.2s}
.tg.on i{background:#14b8a6}.tg.on i::after{left:16px;background:#fff}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(112px,1fr));gap:8px}
.kv div{background:#0c1421;border:1px solid #1c2942;border-radius:11px;padding:9px}
.kv em{display:block;font-style:normal;color:var(--muted);font-size:11px}
.kv b{font-size:16px}
.hint{color:var(--muted);font-size:12px;margin-top:8px}
.tag{font-size:11px;color:var(--muted)}
@media(max-width:520px){.sl{grid-template-columns:62px 1fr 40px}h1{font-size:17px}.pw{width:56px;height:56px}}
</style></head><body><main>

<div class="top">
  <div><h1>WS2812 · 180 珠灯带控制台</h1><div class="tag" id="tag">正在连接设备…</div></div>
  <div class="chips">
    <span class="chip" id="cPres">人体 --</span>
    <span class="chip" id="cLink">ESP-NOW --</span>
    <span class="chip" id="cRun">状态 --</span>
    <span class="chip" id="cFps">-- FPS</span>
  </div>
</div>

<div class="card"><canvas id="strip" width="60" height="1"></canvas></div>

<div class="card"><h2>主控</h2>
  <div class="pv">
    <button class="pw" id="pw" title="开关">&#9211;</button>
    <div style="flex:1">
      <div class="sl"><label>亮度</label><input type="range" id="br" min="1" max="255"><b id="brv">-</b></div>
      <div class="sl"><label>速度</label><input type="range" id="sp" min="0" max="255"><b id="spv">-</b></div>
      <div class="sl"><label>强度</label><input type="range" id="in" min="0" max="255"><b id="inv">-</b></div>
    </div>
  </div>
  <div class="hint">速度 0 会把画面定格；强度对每个效果含义不同（密度 / 拖尾 / 数量）。</div>
</div>

<div class="card"><h2>动画效果</h2>
  <div class="grid" id="fxg"></div>
  <div class="row" style="margin-top:10px">
    <button onclick="stepFx(-1)">◀ 上一个</button><button onclick="stepFx(1)">下一个 ▶</button>
    <button class="w" onclick="randFx()">🎲 随机</button>
  </div>
</div>

<div class="card"><h2>配色</h2>
  <div class="pal" id="palg"></div>
  <div class="row" style="margin-top:10px">
    <label class="f"><span>主色</span><input type="color" id="c1"></label>
    <label class="f"><span>副色</span><input type="color" id="c2"></label>
  </div>
  <div class="hint">选择“自定义双色 / 自定义单色”调色板时，主副色会直接决定整条灯带的取色。</div>
</div>

<div class="card"><h2>布局</h2>
  <div class="row">
    <div class="tg" id="rv" onclick="tog('rv')"><i></i>反向</div>
    <div class="tg" id="mi" onclick="tog('mi')"><i></i>镜像</div>
    <label class="f"><span>分段重复</span><select id="sg"></select></label>
  </div>
</div>

<div class="card"><h2>人体存在联动</h2>
  <div class="kv" style="margin-bottom:11px">
    <div><em>存在</em><b id="sPres">--</b></div>
    <div><em>雷达判定</em><b id="sRes">--</b></div>
    <div><em>目标距离</em><b id="sDist">--</b></div>
    <div><em>广播序号</em><b id="sSeq">--</b></div>
    <div><em>最后一包</em><b id="sAge">--</b></div>
    <div><em>累计 / 异常</em><b id="sPkt">--</b></div>
  </div>
  <div class="row">
    <div class="tg" id="am" onclick="tog('am')"><i></i>自动感应</div>
    <div class="tg" id="mb" onclick="tog('mb')"><i></i>动静调速</div>
    <div class="tg" id="df" onclick="tog('df')"><i></i>距离跟随光斑</div>
    <div class="tg" id="pg" onclick="tog('pg')"><i></i>到场涟漪</div>
    <div class="tg" id="ad" onclick="tog('ad')"><i></i>走近提亮</div>
    <div class="tg" id="ms" onclick="tog('ms')"><i></i>情绪同步</div>
  </div>
  <div class="row" style="margin-top:9px">
    <label class="f"><span>无人保持（秒）</span><input type="number" id="hs" min="0" max="3600"></label>
    <label class="f"><span>雷达量程（cm）</span><input type="number" id="mr" min="70" max="1000" step="10"></label>
  </div>
  <div class="row" style="margin-top:9px">
    <label class="f"><span>入场特效</span><select id="en"></select></label>
    <label class="f"><span>退场特效</span><select id="ex"></select></label>
  </div>
  <div class="row" style="margin-top:9px">
    <label class="f"><span>入场时长（ms）</span><input type="number" id="em" min="100" max="10000" step="100"></label>
    <label class="f"><span>退场时长（ms）</span><input type="number" id="xm" min="100" max="15000" step="100"></label>
  </div>
  <div class="sl"><label>无人微亮</label><input type="range" id="ib" min="0" max="120"><b id="ibv">-</b></div>
  <div class="hint">无人微亮设为 0 时退场特效会把灯带完全熄灭；大于 0 则退场后停留在这个亮度守夜。
  「情绪同步」会在运动/静止切换时自动挑选动感或安静灯效。「走近提亮」按距离提高整体亮度。
  传感器掉线超过 3 秒会显示离线，并按最后一次“有人”上报计算无人保持时间。</div>
</div>

<div class="card"><h2>场景轮播</h2>
  <div class="row">
    <div class="tg" id="ac" onclick="tog('ac')"><i></i>自动轮播灯效</div>
    <label class="f"><span>轮播间隔（秒）</span><input type="number" id="cs" min="5" max="600"></label>
  </div>
  <div class="hint">开启后会在点亮状态下自动切换动画（跳过频闪/警灯/传感器专用效果）。</div>
</div>

<div class="card"><h2>预设与系统</h2>
  <div class="row" id="presets"></div>
  <div class="row" style="margin-top:10px">
    <label class="f"><span>限流（mA，按电源能力设置）</span><input type="number" id="ma" min="300" max="20000" step="100"></label>
    <button class="p" onclick="api('/api/save').then(()=>toast('已写入 Flash'))">保存设置</button>
  </div>
  <div class="row" style="margin-top:10px">
    <label class="f"><span>热点名称</span><input type="text" id="ssid" maxlength="31"></label>
    <label class="f"><span>热点密码（≥8 位）</span><input type="text" id="pass" maxlength="63"></label>
    <button class="w" onclick="saveWifi()">保存并重启</button>
  </div>
  <div class="hint" id="sys">--</div>
</div>

<script>
let M={fx:[],pl:[],tr:[]},S={},hold={},ready=false,q={},timer=null;
const el=id=>document.getElementById(id);
const PG=['linear-gradient(90deg,red,#ff0,#0f0,#0ff,#00f,#f0f,red)',
'linear-gradient(90deg,red,red,#0f0,#0f0,#00f,#00f,#f0f,#f0f)',
'linear-gradient(90deg,#0ff,#f0f,#ff0,#0f0,#00f)',
'linear-gradient(90deg,#300,#c30,#f80,#fd0,#fff)',
'linear-gradient(90deg,#0a2b4a,#1a7fa8,#3fd0c9,#bff,#0a4)',
'linear-gradient(90deg,#053,#0a5,#5c2,#ad3,#063)',
'linear-gradient(90deg,#000,#900,#f40,#fc0,#fff)',
'linear-gradient(90deg,#123,#8ad,#fff,#acd,#57a)',
'linear-gradient(90deg,#01042a,#0050c8,#3cc8ff,#fff)',
'linear-gradient(90deg,#6e003c,#ff3c00,#ffaa14,#ff5a3c,#3c0050)',
'linear-gradient(90deg,#f0f,#80f,#0cf,#0f8,#f0a)',
'linear-gradient(90deg,#032,#0c8,#bfe,#076)',
'linear-gradient(90deg,#3c0014,#ff78aa,#ffcde1,#aa2350)',
'linear-gradient(90deg,#01100a,#00b45a,#28ffb4,#783cff,#080028)',
'linear-gradient(90deg,var(--k1),var(--k2),var(--k1))',
'linear-gradient(90deg,var(--k1),var(--k1))'];

function api(u){return fetch(u).then(r=>r.json()).catch(()=>null)}
function send(k,v){q[k]=v;if(timer)return;timer=setTimeout(flush,90)}
function flush(){timer=null;const p=new URLSearchParams(q);q={};fetch('/api/set?'+p.toString())}
function toast(t){el('tag').textContent=t;setTimeout(()=>el('tag').textContent=sysline(),1800)}
function touch(k){hold[k]=Date.now()+1200}
function held(k){return hold[k]&&hold[k]>Date.now()}

function bindSlider(id,key){const s=el(id);s.addEventListener('input',()=>{touch(key);el(id+'v').textContent=s.value;send(key,s.value)})}
function bindNum(id,key){const s=el(id),push=()=>{if(s.value==='')return;touch(key);send(key,s.value)};
  s.addEventListener('input',push);s.addEventListener('change',push)}
function tog(k){touch(k);const on=!(S[k]);S[k]=on?1:0;el(k).classList.toggle('on',on);send(k,on?1:0)}

function buildUi(){
  el('fxg').innerHTML=M.fx.map((n,i)=>`<button class="fx" data-i="${i}" onclick="pick(${i})">${n}</button>`).join('');
  el('palg').innerHTML=M.pl.map((n,i)=>`<div class="pc" data-i="${i}" onclick="pickPal(${i})"><i style="background:${PG[i]||'#345'}"></i><span>${n}</span></div>`).join('');
  const opt=M.tr.map((n,i)=>`<option value="${i}">${n}</option>`).join('');
  el('en').innerHTML=opt;el('ex').innerHTML=opt;
  el('sg').innerHTML=[1,2,3,4,5,6,7,8].map(v=>`<option value="${v}">${v} 段</option>`).join('');
  el('presets').innerHTML=[0,1,2,3].map(i=>
    `<div class="f" style="min-width:104px"><span>预设 ${i+1}</span><div class="row" style="gap:5px">
     <button style="flex:1" onclick="preset(${i},'load')">读取</button>
     <button style="flex:1" class="w" onclick="preset(${i},'save')">保存</button></div></div>`).join('');
  bindSlider('br','br');bindSlider('sp','sp');bindSlider('in','in');bindSlider('ib','ib');
  ['hs','mr','ma','em','xm','en','ex','sg','cs'].forEach(k=>bindNum(k,k));
  ['c1','c2'].forEach(k=>el(k).addEventListener('input',()=>{touch(k);send(k,el(k).value.slice(1))}));
  el('pw').onclick=()=>{touch('pw');S.pw=S.pw?0:1;el('pw').classList.toggle('on',!!S.pw);send('pw',S.pw)};
  ready=true;
}
function pick(i){touch('fx');S.fx=i;send('fx',i);paintFx()}
function pickPal(i){touch('pl');S.pl=i;send('pl',i);paintPal()}
function stepFx(d){pick((S.fx+d+M.fx.length)%M.fx.length)}
function randFx(){pick(Math.floor(Math.random()*M.fx.length))}
function preset(i,a){api(`/api/preset?slot=${i}&do=${a}`).then(()=>{toast(a=='save'?`已存入预设 ${i+1}`:`已载入预设 ${i+1}`);hold={};live()})}
function saveWifi(){const s=el('ssid').value,p=el('pass').value;
  if(!s||p.length<8){toast('名称不能为空，密码至少 8 位');return}
  fetch(`/api/wifi?ssid=${encodeURIComponent(s)}&pass=${encodeURIComponent(p)}`).then(()=>toast('已保存，正在重启…'))}
function paintFx(){document.querySelectorAll('#fxg .fx').forEach(b=>b.classList.toggle('on',+b.dataset.i===S.fx))}
function paintPal(){document.querySelectorAll('#palg .pc').forEach(b=>b.classList.toggle('on',+b.dataset.i===S.pl))}

const RES=['无人','有人运动','有人静止'];
function sysline(){return `固件 ${M.ver} · ${M.n} 珠 · GPIO${M.pin} · 信道 ${M.ch} · 热点 ${M.ssid} · 剩余内存 ${(S.hp/1024|0)} KB`}
function setIf(k,id,v){if(!held(k))el(id).value=v}

function live(){return api('/api/live').then(s=>{if(!s)return;S=s;
  if(!held('pw'))el('pw').classList.toggle('on',!!s.pw);
  [['br','brv'],['sp','spv'],['in','inv'],['ib','ibv']].forEach(([k,d])=>{if(!held(k)){el(k).value=s[k];el(d).textContent=s[k]}});
  ['hs','mr','ma','em','xm','en','ex','sg','cs'].forEach(k=>setIf(k,k,s[k]));
  if(!held('c1'))el('c1').value='#'+s.c1; if(!held('c2'))el('c2').value='#'+s.c2;
  document.documentElement.style.setProperty('--k1','#'+s.c1);
  document.documentElement.style.setProperty('--k2','#'+s.c2);
  ['rv','mi','am','mb','df','pg','ac','ad','ms'].forEach(k=>{if(!held(k))el(k).classList.toggle('on',!!s[k])});
  if(!held('fx'))paintFx(); if(!held('pl'))paintPal();
  el('cPres').textContent='人体 '+(s.pr?'在场':'无人');el('cPres').className='chip'+(s.pr?' on':'');
  el('cLink').textContent='ESP-NOW '+(s.lk?'已连接':'离线');el('cLink').className='chip'+(s.lk?' on':' warn');
  el('cRun').textContent='状态 '+['熄灭','入场中','点亮','退场中'][s.rs];el('cRun').className='chip'+(s.rs==2?' on':'');
  el('cFps').textContent=s.fp+' FPS';
  el('sPres').textContent=s.lk?(s.pr?'在场':'无人'):'--';
  el('sRes').textContent=s.lk?(RES[s.rr]||'未知'):'--';
  el('sDist').textContent=s.lk?(s.dc/100).toFixed(2)+' m':'--';
  el('sSeq').textContent=s.lk?'#'+s.sq:'--';
  el('sAge').textContent=s.lk?s.ag+' ms':'超时';
  el('sPkt').textContent=s.pk+' / '+s.bd;
  el('sys').textContent=sysline();
  if(el('tag').textContent=='正在连接设备…')el('tag').textContent=sysline();
  drawStrip(s.px);
})}

const cv=el('strip'),cx=cv.getContext('2d');
function drawStrip(hex){if(!hex)return;const n=hex.length/6;if(cv.width!=n)cv.width=n;
  const img=cx.createImageData(n,1);
  for(let i=0;i<n;i++){img.data[i*4]=parseInt(hex.substr(i*6,2),16);img.data[i*4+1]=parseInt(hex.substr(i*6+2,2),16);
    img.data[i*4+2]=parseInt(hex.substr(i*6+4,2),16);img.data[i*4+3]=255}
  cx.putImageData(img,0,0)}

function pollLive(){live().then(()=>setTimeout(pollLive,200))}
api('/api/meta').then(m=>{if(!m){el('tag').textContent='设备无响应，请重新连接热点';return}
  M=m;el('ssid').value=m.ssid;buildUi();pollLive()});
</script></main></body></html>
)HTML";
