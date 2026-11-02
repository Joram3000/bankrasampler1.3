// transfer_page.h — the web page served in WiFi transfer mode (flash only).
//
// Files that are not already 44.1 kHz / 16-bit / stereo PCM WAV — the only
// format the player handles — are decoded and converted in the browser
// before upload, so any WAV/MP3/AIFF/M4A/FLAC the browser can read works.
// .txt files (settings, pin config, sample map) are uploaded unchanged.
#pragma once

#include <Arduino.h>

const char kTransferPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="nl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bankra Sampler</title>
<style>
:root{--bg:#f6f5f2;--card:#fff;--fg:#1c1b19;--mute:#76736c;--line:#e4e1da;--acc:#d9480f;--ok:#2b8a3e;--err:#c92a2a}
@media (prefers-color-scheme:dark){:root{--bg:#141413;--card:#1e1d1b;--fg:#ecebe8;--mute:#9a978f;--line:#33312d;--acc:#ff7a3d;--ok:#69db7c;--err:#ff8787}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 -apple-system,system-ui,sans-serif}
main{max-width:640px;margin:0 auto;padding:24px 16px 48px}
h1{font-size:20px;margin:0}
.sub{color:var(--mute);font-size:13px;margin:2px 0 20px}
#drop{display:block;border:2px dashed var(--line);border-radius:12px;padding:28px 16px;text-align:center;cursor:pointer;background:var(--card)}
#drop.over{border-color:var(--acc)}
#drop b{color:var(--acc)}
#drop small{display:block;color:var(--mute);margin-top:6px;font-size:12px}
#pick{display:none}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;margin-top:16px;overflow:hidden}
.card h2{display:flex;justify-content:space-between;font-size:13px;font-weight:600;color:var(--mute);margin:0;padding:10px 12px;border-bottom:1px solid var(--line)}
ul{list-style:none;margin:0;padding:0}
li{display:flex;align-items:center;gap:10px;padding:8px 12px;border-bottom:1px solid var(--line)}
li:last-child{border-bottom:0}
.name{flex:1;min-width:0}
.name span{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{color:var(--mute);font-size:12px;font-variant-numeric:tabular-nums;white-space:nowrap}
.bar{height:3px;background:var(--line);border-radius:2px;margin-top:4px;overflow:hidden}
.bar i{display:block;height:100%;width:0;background:var(--acc)}
.ok{color:var(--ok)}.err{color:var(--err)}
.icon{border:0;background:none;color:var(--mute);font-size:15px;padding:4px 6px;cursor:pointer;text-decoration:none;border-radius:6px}
.icon:hover{color:var(--fg);background:var(--line)}
#done{width:100%;margin-top:20px;padding:12px;border:0;border-radius:10px;background:var(--fg);color:var(--bg);font-weight:600;font-size:15px;cursor:pointer}
</style>
</head>
<body>
<main>
<h1>Bankra Sampler</h1>
<p class="sub" id="info">Laden…</p>
<label id="drop"><input type="file" id="pick" multiple accept="audio/*,.wav,.mp3,.m4a,.aif,.aiff,.flac,.ogg,.txt">
<b>Sleep samples hierheen</b> of klik om te kiezen
<small>Alles wordt opgeslagen als WAV (44,1 kHz · 16-bit · stereo); andere formaten worden eerst omgezet.<br>Houd namen kort: het scherm toont ongeveer 15 tekens.</small></label>
<div class="card" id="qcard" hidden><h2>Uploads</h2><ul id="queue"></ul></div>
<div class="card"><h2><span>Op de SD-kaart</span><span id="count"></span></h2><ul id="files"></ul></div>
<button id="done">Klaar — sampler herstarten</button>
</main>
<script>
const $=s=>document.querySelector(s);
let maxName=47,existing=new Set(),busy=Promise.resolve();
const size=b=>b<1048576?Math.ceil(b/1024)+' KB':(b/1048576).toFixed(1).replace('.',',')+' MB';
const esc=s=>s.replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));

async function refresh(){
  const d=await (await fetch('/list')).json();
  maxName=d.maxName;
  d.files.sort((a,b)=>a.name.localeCompare(b.name,undefined,{numeric:true}));
  existing=new Set(d.files.map(f=>f.name.toLowerCase()));
  const wavs=d.files.filter(f=>/\.wav$/i.test(f.name)&&f.name[0]!='.').length;
  $('#count').textContent=wavs+' / '+d.maxSamples+' samples';
  $('#info').textContent=size(d.freeKb*1024)+' vrij van '+size(d.totalKb*1024);
  $('#files').innerHTML=d.files.map(f=>'<li><span class="name"><span>'+esc(f.name)+'</span></span><span class="meta">'+size(f.size)+
    '</span><a class="icon" title="Downloaden" href="/download?name='+encodeURIComponent(f.name)+'">↓</a>'+
    '<button class="icon del" title="Verwijderen" data-n="'+esc(f.name)+'">✕</button></li>').join('')||'<li class="meta">Nog geen bestanden</li>';
}

// ASCII only (the display font has nothing else), nothing FAT rejects, and
// short enough for the sampler's sample list.
function cleanName(name,ext){
  const m=name.match(/\.[^.]*$/);ext=ext||(m?m[0]:'');
  let base=(m?name.slice(0,-m[0].length):name).normalize('NFD').replace(/[̀-ͯ]/g,'')
    .replace(/[^A-Za-z0-9 _().-]/gu,'_').replace(/^[ .]+|[ .]+$/g,'')||'sample';
  return base.slice(0,maxName-ext.length)+ext;
}

function isPlayableWav(buf){
  if(buf.byteLength<44)return false;
  const v=new DataView(buf),tag=o=>String.fromCharCode(...new Uint8Array(buf,o,4));
  if(tag(0)!='RIFF'||tag(8)!='WAVE')return false;
  for(let o=12;o+24<=buf.byteLength;){
    const len=v.getUint32(o+4,true);
    if(tag(o)=='fmt ')return v.getUint16(o+8,true)==1&&v.getUint16(o+10,true)==2&&v.getUint32(o+12,true)==44100&&v.getUint16(o+22,true)==16;
    o+=8+len+(len&1);
  }
  return false;
}

// Decode with the browser (resampled to 44.1 kHz by the context) and write
// a 16-bit stereo WAV; mono is copied to both channels.
async function toWav(buf){
  const Ctx=window.OfflineAudioContext||window.webkitOfflineAudioContext;
  const a=await new Promise((res,rej)=>new Ctx(2,1,44100).decodeAudioData(buf,res,rej))
    .catch(()=>{throw Error('Geen bruikbaar audiobestand')});
  const n=a.length,L=a.getChannelData(0),R=a.numberOfChannels>1?a.getChannelData(1):L;
  const v=new DataView(new ArrayBuffer(44+n*4));
  const str=(o,s)=>{for(let i=0;i<4;i++)v.setUint8(o+i,s.charCodeAt(i))};
  str(0,'RIFF');v.setUint32(4,36+n*4,true);str(8,'WAVE');str(12,'fmt ');
  v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,2,true);v.setUint32(24,44100,true);
  v.setUint32(28,176400,true);v.setUint16(32,4,true);v.setUint16(34,16,true);str(36,'data');v.setUint32(40,n*4,true);
  const s16=x=>Math.max(-32768,Math.min(32767,Math.round(x*32767)));
  for(let i=0,o=44;i<n;i++,o+=4){v.setInt16(o,s16(L[i]),true);v.setInt16(o+2,s16(R[i]),true)}
  return new Blob([v],{type:'audio/wav'});
}

function send(blob,name,bar){
  return new Promise((res,rej)=>{
    const x=new XMLHttpRequest();
    x.open('POST','/upload');
    x.upload.onprogress=e=>{if(e.lengthComputable)bar.style.width=100*e.loaded/e.total+'%'};
    x.onload=()=>x.status==200?res():rej(Error(x.responseText||'HTTP '+x.status));
    x.onerror=()=>rej(Error('Verbinding verbroken'));
    const f=new FormData();f.append('file',blob,name);x.send(f);
  });
}

async function handle(file,li){
  const label=li.querySelector('.name span'),meta=li.querySelector('.meta'),bar=li.querySelector('i');
  try{
    const buf=await file.arrayBuffer();
    let blob=file,name;
    if(/\.txt$/i.test(file.name)||isPlayableWav(buf))name=cleanName(file.name);
    else{meta.textContent='omzetten…';blob=await toWav(buf);name=cleanName(file.name,'.wav')}
    if(existing.has(name.toLowerCase())&&!confirm(name+' staat al op de kaart. Overschrijven?')){meta.textContent='overgeslagen';return}
    label.textContent=name;meta.textContent=size(blob.size);
    await send(blob,name,bar);
    meta.textContent='✓ '+size(blob.size);meta.className='meta ok';
  }catch(e){meta.textContent=e.message||'Mislukt';meta.className='meta err'}
  await refresh().catch(()=>{});
}

function addFiles(list){
  for(const file of Array.from(list)){
    $('#qcard').hidden=false;
    const li=document.createElement('li');
    li.innerHTML='<div class="name"><span></span><div class="bar"><i></i></div></div><span class="meta">wacht…</span>';
    li.querySelector('.name span').textContent=file.name;
    $('#queue').append(li);
    busy=busy.then(()=>handle(file,li));
  }
}

const drop=$('#drop');
addEventListener('dragover',e=>{e.preventDefault();drop.classList.add('over')});
addEventListener('dragleave',e=>{if(!e.relatedTarget)drop.classList.remove('over')});
addEventListener('drop',e=>{e.preventDefault();drop.classList.remove('over');addFiles(e.dataTransfer.files)});
$('#pick').onchange=e=>{addFiles(e.target.files);e.target.value=''};

$('#files').onclick=async e=>{
  const b=e.target.closest('.del');if(!b)return;
  if(!confirm(b.dataset.n+' verwijderen?'))return;
  const r=await fetch('/delete?name='+encodeURIComponent(b.dataset.n),{method:'POST'});
  if(!r.ok)alert(await r.text());
  refresh();
};

$('#done').onclick=async()=>{
  if(!confirm('Sampler herstarten in de normale modus?'))return;
  try{await fetch('/reboot',{method:'POST'})}catch(e){}
  document.querySelector('main').innerHTML='<h1>Klaar</h1><p class="sub">De sampler herstart en laadt de nieuwe samples. Je kunt nu weer met je eigen WiFi verbinden.</p>';
};

refresh().catch(()=>{$('#info').textContent='Kan de bestandslijst niet laden'});
</script>
</body>
</html>
)HTML";
