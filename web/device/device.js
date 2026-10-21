import {Updater} from '../assets/fm1ota.js';
import {CMD,NightfallLink,decodeLive} from '../assets/nightfall-link.js?v=12';
import {startMirror} from './live-mirror.js?v=12';
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
const embedded=window.parent!==window&&new URLSearchParams(location.search).get('embedded')==='1',editorProtocol='nightfall.editor.v1';
let access,link,input,output,detail=null,queue=Promise.resolve(),keyboardNotes=new Map(),pollTimer,liveTimer,live=null,liveAt=0,liveBusy=false,controlsPending=0,liveSupported=false;
let brokerSequence=0;
const fxNames=['FILTER','DRIVE','CHORUS','PHASER','DELAY','REVERB'];
const macroNames=['BRIGHT','DENSITY','SPACE','MOTION'];
const trackNames=['DRUM','BASS','CHORD','LEAD'];
const defaults=[36,41,48,60];
function toParent(type,data={}){if(embedded)window.parent.postMessage({protocol:editorProtocol,type,...data},location.origin)}
class BrokerLink{
  constructor(){this.waiting=new Map()}
  get pending(){return this.waiting.size>0}
  request(command,payload=[]){const id=brokerSequence++;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.waiting.delete(id);reject(Error('设备通信超时'))},2800);this.waiting.set(id,{resolve,reject,timer});toParent('request',{id,command,payload:Array.from(payload)})})}
  receive(reply){const pending=this.waiting.get(reply.id);if(!pending)return;this.waiting.delete(reply.id);clearTimeout(pending.timer);if(reply.error)pending.reject(Error(String(reply.error)));else pending.resolve(Uint8Array.from(reply.payload||[]))}
  close(){for(const pending of this.waiting.values()){clearTimeout(pending.timer);pending.reject(Error('连接已断开'))}this.waiting.clear()}
}
function message(text){$('#message').textContent=text;toParent('status',{text})}
function editingControl(){const active=document.activeElement;return !!(active?.closest('.pane')&&active.matches('input,select'))}
function connected(yes){$('#identity').textContent=yes?'NIGHTFALL / LIVE':'OFFLINE · SIMULATION';$('#connect').disabled=yes;$('#disconnect').disabled=!yes;$$('.pane input,.pane select,.pane button').forEach(el=>{el.disabled=!yes});$('#bank').disabled=false;$$('.rail button').forEach(el=>el.disabled=false)}
function run(fn){controlsPending++;const job=queue.then(async()=>{if(!link)throw Error('请先连接 NIGHTFALL');while(liveBusy)await new Promise(resolve=>setTimeout(resolve,10));return fn()});queue=job.catch(e=>message(e.message));return job.finally(()=>{controlsPending--})}
async function command(cmd,payload=[]){return run(()=>link.request(cmd,payload))}
function control(group,index,parameter,value){return command(CMD.CONTROL,[group,index,parameter,value])}
async function refresh(){const p=await command(CMD.DETAIL);if(p.length!==117)throw Error('固件协议版本与网页不匹配');detail=p;if(!editingControl())render();toParent('detail',{payload:Array.from(p)});return p}
function render(){if(!detail)return;const p=detail,track=p[6],fx=p[111];
  $('#tempo').value=p[1]|p[2]<<8;$('#play').textContent=p[3]?'■ 停止':'▶ 播放';$('#preset').value=p[7];$('#track').value=track;$('#scale').value=p[11];$('#length').value=p[115];$('#sound-track').value=track;$('#seq-track').value=track;$('#fx-module').value=fx;$('#fx-enabled').checked=!!p[28+fx];
  $('#macros').replaceChildren(...macroNames.map((name,i)=>slider(name,p[16+i],100,v=>control(3,i,0,v))));
  $('#fx-sliders').replaceChildren(...Array.from({length:4},(_,i)=>slider(['PARAM A','PARAM B','PARAM C','MIX'][i],p[34+fx*4+i],127,v=>command(CMD.FX,[fx,i,v]))));
  $('#mix-sliders').replaceChildren(...trackNames.map((name,i)=>slider(name,p[20+i],127,v=>control(4,i,0,v))));
  renderSteps(p);
}
function slider(name,value,max,change){const label=document.createElement('label'),title=document.createElement('span'),out=document.createElement('output'),range=document.createElement('input');title.textContent=name;out.textContent=value;range.type='range';range.min=0;range.max=max;range.value=value;range.disabled=!link;range.addEventListener('change',async()=>{try{await change(Number(range.value));out.textContent=range.value;message(name+' 已同步')}catch(e){message(e.message);range.value=value;out.textContent=value}});label.append(title,out,range);return label}
function renderSteps(p){const bank=Number($('#bank').value),box=$('#steps');box.replaceChildren();for(let i=0;i<16;i++){const step=bank*16+i,on=!!(p[98+(step>>3)]&(1<<(step&7))),button=document.createElement('button');button.textContent=String(step+1).padStart(2,'0');button.classList.toggle('on',on);button.disabled=!link;button.setAttribute('aria-label',`第 ${step+1} 步：${on?'开启':'关闭'}`);button.onclick=async()=>{const track=Number($('#seq-track').value);try{await command(CMD.STEP,[track,step,defaults[track],on?0:100,1,100]);await refresh();message(`轨道 ${trackNames[track]} · 第 ${step+1} 步 ${on?'关闭':'开启'}`)}catch(e){message(e.message)}};box.append(button)}}
async function pollLive(){if(!link||liveBusy||controlsPending||link.pending)return;liveBusy=true;try{live=decodeLive(await link.request(CMD.LIVE,[],1100));liveAt=performance.now();liveSupported=true;$('#mirror-mode').textContent=`实机快照 · ${live.voices} 声部 · MASTER ${live.master}%`;$('#note').textContent=live.underruns?`AUDIO LATE ${live.underruns}`:`STEP ${live.step+1} · ${live.events.length} EVENTS`;draw();}catch(e){if(!liveSupported&&String(e.message).includes('状态 2')){clearInterval(liveTimer);liveTimer=null;live=null;$('#mirror-mode').textContent='旧固件 / MIDI 近似';message('当前固件没有 LIVE 实时快照；需更新后才能同步真实包络、伴奏和页面状态。')}else message('实时快照暂时中断：'+e.message)}finally{liveBusy=false}}
function acceptSharedLive(snapshot){if(!snapshot||!Array.isArray(snapshot.events))return;live=snapshot;liveAt=performance.now();liveSupported=true;$('#mirror-mode').textContent=`实机快照 · ${live.voices} 声部 · MASTER ${live.master}%`;$('#note').textContent=live.underruns?`AUDIO LATE ${live.underruns}`:`STEP ${live.step+1} · ${live.events.length} EVENTS`;draw()}
function attachShared(identity,snapshot){if(link)return;const broker=new BrokerLink();link=broker;output={send:bytes=>toParent('midi-out',{data:Array.from(bytes)})};message('正在从 '+identity+' 同步当前状态…');refresh().then(()=>{if(link!==broker)return;connected(true);message('已连接 '+identity+' · 演奏和编辑共用一次 USB MIDI 会话');pollTimer=setInterval(()=>{if(link&&!link.pending&&!editingControl())refresh().catch(e=>message(e.message))},1800);acceptSharedLive(snapshot)}).catch(e=>{if(link===broker){disconnect();message('状态同步失败：'+e.message)}})}
async function connect(){try{message('等待浏览器 MIDI / SysEx 权限…');if(!navigator.requestMIDIAccess)throw Error('请用桌面 Chrome / Edge 打开');access=await navigator.requestMIDIAccess({sysex:true});message('已获得权限，正在查找 FM‑1…');const found=await new Updater(access).find(id=>id.model==='FM-1');if(!found)throw Error('没有找到已连接的 FM‑1');found.link.close();input=found.link.input;output=found.link.output;link=new NightfallLink(input,output);message('已识别 FM‑1，正在验证 NIGHTFALL 协议…');await link.hello();input.addEventListener('midimessage',noteMessage);access.onstatechange=()=>{if(input?.state==='disconnected'||output?.state==='disconnected')disconnect()};connected(true);await refresh();message('已连接 '+found.id.text+' · NIGHTFALL 协议握手成功');pollTimer=setInterval(()=>{if(link&&!link.pending&&!document.querySelector('.pane input[type=range]:active'))refresh().catch(()=>{})},1800);liveTimer=setInterval(pollLive,125);pollLive()}catch(e){disconnect();message('连接失败：'+e.message)}}
function disconnect(){clearInterval(pollTimer);clearInterval(liveTimer);liveTimer=null;live=null;liveSupported=false;for(const voice of keyboardNotes.values()){try{output?.send([0x80|voice.channel,voice.note,0])}catch{}}keyboardNotes.clear();input?.removeEventListener('midimessage',noteMessage);link?.close();link=null;detail=null;connected(false);$('#mirror-mode').textContent='离线 / 演示';message(embedded?'请在上方连接 FM‑1；此处不会再次请求 MIDI 权限。':'已断开；没有向设备发送后续命令')}
function noteMessage(e){const d=e.data;if(d[0]===0xf0||d.length<3)return;const type=d[0]&0xf0;if(type!==0x90&&type!==0x80)return;const active=type===0x90&&d[2]>0;legacyEvent(d[1],d[2],active);if(!liveSupported)$('#note').textContent=active?`NOTE ${d[1]} / VEL ${d[2]}`:'NOTE RELEASE'}
if(embedded){window.addEventListener('message',event=>{if(event.source!==window.parent||event.origin!==location.origin||event.data?.protocol!==editorProtocol)return;const m=event.data;if(m.type==='session'){if(m.connected)attachShared(m.identity||'FM‑1',m.live);else disconnect()}else if(m.type==='reply'&&link instanceof BrokerLink)link.receive(m);else if(m.type==='live')acceptSharedLive(m.live);else if(m.type==='midi'&&Array.isArray(m.data))noteMessage({data:Uint8Array.from(m.data)})});const report=()=>toParent('resize',{height:Math.ceil(document.documentElement.scrollHeight)});new ResizeObserver(report).observe(document.body);window.addEventListener('load',report)}
$('#connect').onclick=connect;$('#disconnect').onclick=disconnect;$('#refresh').onclick=()=>refresh().catch(e=>message(e.message));
$$('.rail button').forEach(button=>button.onclick=()=>{$$('.rail button,.pane').forEach(el=>el.classList.remove('active'));button.classList.add('active');$('#'+button.dataset.page).classList.add('active')});
$('#play').onclick=async()=>{try{await command(CMD.PLAY,[detail?.[3]?0:1]);await refresh()}catch(e){message(e.message)}};
$('#tempo').onchange=async e=>{const value=Number(e.target.value);if(!Number.isInteger(value)||value<40||value>240){message('BPM 范围为 40–240');return}try{await command(CMD.TEMPO,[value&255,value>>8]);await refresh()}catch(err){message(err.message)}};
$('#preset').onchange=async e=>{try{if(detail?.[3])throw Error('请先停止播放再换预设');await control(2,0,0,Number(e.target.value));await refresh()}catch(err){message(err.message)}};
$('#track').onchange=$('#sound-track').onchange=async e=>{try{await control(1,0,0,Number(e.target.value));await refresh()}catch(err){message(err.message)}};
$('#scale').onchange=e=>control(10,0,0,Number(e.target.value)).catch(err=>message(err.message));
$('#fx-module').onchange=async e=>{try{await control(13,0,0,Number(e.target.value));await refresh()}catch(err){message(err.message)}};
$('#fx-enabled').onchange=async e=>{try{await command(CMD.FX,[Number($('#fx-module').value),4,e.target.checked?1:0]);await refresh()}catch(err){message(err.message)}};
$('#seq-track').onchange=async e=>{try{await control(1,0,0,Number(e.target.value));await refresh()}catch(err){message(err.message)}};
$('#length').onchange=async e=>{try{await control(16,0,0,Number(e.target.value));await refresh()}catch(err){message(err.message)}};
$('#bank').onchange=()=>detail&&renderSteps(detail);
$('#save').onclick=async()=>{try{if(detail?.[3])throw Error('先停止播放再保存');await command(CMD.SAVE,[Number($('#slot').value)]);message('已保存至设备 RAM 槽位；断电会丢失')}catch(e){message(e.message)}};
$('#load').onclick=async()=>{try{if(detail?.[3])throw Error('先停止播放');await command(CMD.LOAD,[Number($('#slot').value)]);await refresh();message('项目已从 RAM 槽位载入')}catch(e){message(e.message)}};
$('#export').onclick=async()=>{try{const slot=Number($('#slot').value),data=new Uint8Array(1260);for(let part=0;part<7;part++){const p=await command(CMD.EXPORT,[slot,part]);if(p.length!==182||p[1]!==part)throw Error('导出分块有误');data.set(p.slice(2),part*180)}const url=URL.createObjectURL(new Blob([data],{type:'application/octet-stream'})),a=document.createElement('a');a.href=url;a.download=`NIGHTFALL-project-${slot+1}.nfpr`;a.click();setTimeout(()=>URL.revokeObjectURL(url),30000);message('项目文件已触发下载，请确认浏览器保存成功')}catch(e){message(e.message)}};
$('#import').onchange=async e=>{const file=e.target.files[0];if(!file)return;const data=new Uint8Array(await file.arrayBuffer()),slot=Number($('#slot').value);try{if(data.length!==1260||String.fromCharCode(...data.slice(0,4))!=='NFPR'||data[4]!==2)throw Error('不是有效的 NIGHTFALL v2 项目文件');if(detail?.[3])throw Error('先停止播放');if(!confirm(`导入会覆盖设备 RAM 槽位 ${slot+1}，继续吗？`))return;await command(CMD.IMPORT_BEGIN,[slot]);try{for(let part=0;part<7;part++)await command(CMD.IMPORT_CHUNK,[slot,part,...data.slice(part*180,(part+1)*180)]);await command(CMD.IMPORT_COMMIT,[slot])}catch(err){await command(CMD.IMPORT_ABORT,[slot]).catch(()=>{});throw err}await refresh();message('项目已导入 RAM 槽位；请手动载入使用')}catch(err){message(err.message)}finally{e.target.value=''}};
const keyNotes=[60,61,62,63,64,65,66,67,68,69,70,71,72];
for(const note of keyNotes){
  const b=document.createElement('button');b.textContent=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B','C'][note-60];
  b.classList.toggle('black',[1,3,6,8,10].includes(note%12));b.disabled=true;
  b.onpointerdown=e=>{if(!link||keyboardNotes.has(note))return;e.preventDefault();b.setPointerCapture(e.pointerId);
    const channel=Number($('#track').value),actual=channel===0?[36,38,42,46][(note-60)%4]:note;
    try{output.send([0x90|channel,actual,105]);keyboardNotes.set(note,{channel,note:actual})}catch(err){message(err.message)}};
  const off=()=>{const voice=keyboardNotes.get(note);if(!voice)return;keyboardNotes.delete(note);try{output.send([0x80|voice.channel,voice.note,0])}catch(err){message(err.message)}};
  b.onpointerup=b.onpointercancel=b.onlostpointercapture=off;$('#keyboard').append(b);
}
const mirror=startMirror($('#city'),()=>live&&performance.now()-liveAt<700?{...live,receivedAt:liveAt}:null,()=>detail);
function legacyEvent(note,velocity,on){mirror.note(note,velocity,on)}
function draw(){mirror.draw()}
connected(false);$$('.rail button').forEach(b=>b.disabled=false);
if(embedded){message('请在上方连接 FM‑1；此处无需第二次连接。');toParent('ready')}
