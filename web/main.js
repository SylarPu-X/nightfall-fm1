import {Updater} from './assets/fm1ota.js';
import {CMD,NightfallLink,decodeLive} from './assets/nightfall-link.js?v=12';

const $=selector=>document.querySelector(selector);
const frame=$('#sim-frame');
const protocol='nightfall.bridge.v1';
const editorFrame=$('#editor-frame'),editorProtocol='nightfall.editor.v1';
let access=null,input=null,output=null,identity=null,connecting=false,events=0,liveLink=null,liveTimer=null,liveSupported=false,liveBusy=false,lastUnderruns=0,lastLive=null,lastDetail=null,editorQueue=Promise.resolve(),editorControls=0,connectionEpoch=0;
let webMuteTimer=null,webMuted=false,webAudioReadyResolve=null,webModeEpoch=0;
const sent=new Map(),noteRefs=new Map();
const editorNotes=new Map();
const noteName=n=>['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'][n%12]+(Math.floor(n/12)-1);

function post(type,data={}){frame.contentWindow?.postMessage({protocol,type,...data},location.origin)}
function editorPost(type,data={}){editorFrame.contentWindow?.postMessage({protocol:editorProtocol,type,...data},location.origin)}
function editorSession(){editorPost('session',{connected:!!liveLink,identity:identity?.text||null,live:lastLive})}
function editorMidi(bytes){if(!identity||!Array.isArray(bytes)||bytes.length!==3||bytes.some(v=>!Number.isInteger(v)||v<0||v>255))return;const kind=bytes[0]&0xf0,channel=bytes[0]&15,note=bytes[1],velocity=bytes[2];if((kind!==0x80&&kind!==0x90)||channel>3||note>127||velocity>127)return;const key=channel+':'+note;if(kind===0x90&&velocity){if(send(bytes))editorNotes.set(key,bytes)}else{send(bytes);editorNotes.delete(key)}}
function editorRequest(message){const id=message.id,cmd=message.command,payload=message.payload;
  const allowed=new Set([CMD.DETAIL,CMD.STATUS,CMD.TEMPO,CMD.FX,CMD.STEP,CMD.PLAY,CMD.SAVE,CMD.LOAD,CMD.EXPORT,CMD.IMPORT_BEGIN,CMD.IMPORT_CHUNK,CMD.IMPORT_COMMIT,CMD.IMPORT_ABORT,CMD.CONTROL]);
  if(!Number.isInteger(id)||id<0||!allowed.has(cmd)||!Array.isArray(payload)||payload.length>240||payload.some(v=>!Number.isInteger(v)||v<0||v>255)){editorPost('reply',{id,error:'编辑命令格式无效'});return}
  editorControls++;const epoch=connectionEpoch,session=liveLink;
  const job=editorQueue.then(async()=>{while(liveBusy||liveLink?.pending)await new Promise(resolve=>setTimeout(resolve,10));if(!session||epoch!==connectionEpoch||liveLink!==session)throw Error('FM‑1 已断开');return Array.from(await session.request(cmd,payload,2200))});
  editorQueue=job.catch(()=>{});
  job.then(reply=>editorPost('reply',{id,payload:reply}),error=>{const s=liveLink?.stats;log('EDITOR',`CMD ${cmd} ${error.message}`+(s?` / RX ${s.frames} invalid ${s.invalid} mismatch ${s.mismatch} len ${s.lastLength} cmd ${s.lastCommand}`:''));editorPost('reply',{id,error:error.message})}).finally(()=>{editorControls--});
}
function monitorMute(enabled){
  const session=liveLink,epoch=connectionEpoch;if(!session)return Promise.reject(Error('设备未连接'));
  editorControls++;
  const job=editorQueue.then(async()=>{while(liveBusy||session.pending)await new Promise(resolve=>setTimeout(resolve,10));if(epoch!==connectionEpoch||liveLink!==session)throw Error('FM‑1 已断开');return session.request(CMD.CONTROL,[17,0,0,enabled?1:0],1200)});
  editorQueue=job.catch(()=>{});return job.finally(()=>{editorControls--});
}
function webOnlyStatus(message){$('#web-only-status').textContent=message}
async function disableWebOnly(){
  ++webModeEpoch;
  clearInterval(webMuteTimer);webMuteTimer=null;$('#web-only').checked=false;
  post('web-only',{enabled:false,volume:Number($('#web-volume').value)});
  if(webMuted){webMuted=false;try{await monitorMute(false)}catch{}}
  webOnlyStatus(identity?'网页音源模式已关闭；实机音频恢复。':'连接 NIGHTFALL 后可启用；不会修改项目或 MASTER 旋钮。');
}
async function enableWebOnly(){
  if(!identity){$('#web-only').checked=false;return}
  const modeEpoch=++webModeEpoch;
  $('#browser-sound').checked=true;post('set-sound',{enabled:true});
  $('#route-out').checked=false;releaseSent();setMode(false);$('#route-in').checked=true;
  webOnlyStatus('正在启动网页音源…');
  const ready=await new Promise(resolve=>{
    webAudioReadyResolve=resolve;
    post('web-only',{enabled:true,volume:Number($('#web-volume').value)});
    setTimeout(()=>{if(webAudioReadyResolve===resolve){webAudioReadyResolve=null;resolve(false)}},1800);
  });
  if(modeEpoch!==webModeEpoch)return;
  if(!ready||!$('#web-only').checked){await disableWebOnly();webOnlyStatus('浏览器音频未启动。请点击模拟器中的“声音”后重试。');return}
  if(liveLink){
    try{await monitorMute(true);if(modeEpoch!==webModeEpoch){try{await monitorMute(false)}catch{}return}webMuted=true;webOnlyStatus('仅网页发声已启用；实机本地输出临时静音，断开后自动恢复。');
      webMuteTimer=setInterval(async()=>{if(modeEpoch!==webModeEpoch||!$('#web-only').checked)return;try{await monitorMute(true)}catch{webMuted=false;clearInterval(webMuteTimer);webMuteTimer=null;webOnlyStatus('自动静音已中断；请把实机 MASTER 调到 0，或关闭仅网页发声。')}},900);
    }catch{webOnlyStatus('当前实机固件不支持自动静音；请把实机 MASTER 调到 0。网页音量独立可调。')}
  }else webOnlyStatus('当前设备只提供标准 MIDI；请把实机 MASTER 调到 0。网页音量独立可调。');
}
function log(direction,description){events++;$('#activity-count').textContent=String(events).padStart(3,'0')+' EVENTS';const list=$('#activity');if(events===1)list.replaceChildren();const row=document.createElement('li'),a=document.createElement('span'),b=document.createElement('em'),time=document.createElement('small');a.textContent=direction;b.textContent=description;time.textContent=new Date().toLocaleTimeString('zh-CN',{hour12:false});row.append(a,b,time);list.prepend(row);while(list.children.length>7)list.lastChild.remove()}
function setMode(link){$('#mode-link').classList.toggle('active',link);$('#mode-link').setAttribute('aria-pressed',String(link));$('#mode-sim').classList.toggle('active',!link);$('#mode-sim').setAttribute('aria-pressed',String(!link));$('#route-indicator').textContent=link?'DEVICE ROUTE ON':'DEVICE ROUTE OFF'}
function state(name,description,kind){$('#device-state').textContent=name;$('#device-detail').textContent=description;$('#connection-mode').textContent=kind;$('#top-status').textContent=identity?kind:'SIMULATION / 离线';$('#top-dot').style.background=identity?'#48ff66':'';$('#link-led').classList.toggle('on',!!identity);$('#connect-btn').disabled=connecting;$('#disconnect-btn').disabled=!identity;$('#route-out').disabled=!identity;$('#route-in').disabled=!identity;$('#web-only').disabled=!identity}
function send(bytes){if(!output||output.state==='disconnected')return false;try{output.send(bytes);return true}catch{return false}}
function releaseSent(){for(const [key] of noteRefs){const [channel,note]=key.split(':').map(Number);send([0x80|channel,note,0])}sent.clear();noteRefs.clear();if(output)for(let channel=0;channel<16;channel++)send([0xB0|channel,123,0])}
function releaseEditorNotes(){for(const key of editorNotes.keys()){const [channel,note]=key.split(':').map(Number);send([0x80|channel,note,0])}editorNotes.clear()}
function panic(){releaseSent();releaseEditorNotes();post('panic-device');log('SAFE','ALL NOTES OFF')}
function disconnect(reason='已断开设备'){++webModeEpoch;clearInterval(webMuteTimer);webMuteTimer=null;webMuted=false;$('#web-only').checked=false;webOnlyStatus('连接 NIGHTFALL 后可启用；不会修改项目或 MASTER 旋钮。');post('web-only',{enabled:false,volume:Number($('#web-volume').value)});connectionEpoch++;releaseSent();releaseEditorNotes();clearInterval(liveTimer);liveTimer=null;liveLink?.close();liveLink=null;liveSupported=false;liveBusy=false;lastLive=lastDetail=null;if(input)input.onmidimessage=null;if(access)access.onstatechange=null;try{input?.close()}catch{}try{output?.close()}catch{}input=output=identity=null;$('#route-out').checked=false;setMode(false);state('未连接设备',reason,'仅浏览模拟');$('#editor-status').textContent='在上方识别并连接 FM‑1；连接后此处自动启用。';editorSession();post('device-live',{connected:false});post('panic-device')}
function bridgeNoteOn(message){if(!identity||!$('#route-out').checked)return;const id=String(message.id||'').slice(0,80);if(!id||sent.has(id))return;const notes=[...new Set((Array.isArray(message.notes)?message.notes:[]).filter(n=>Number.isInteger(n)&&n>=0&&n<=127))].slice(0,4);if(!notes.length)return;const channel=Number($('#midi-channel').value)||0,velocity=Math.max(1,Math.min(127,Number(message.velocity)||96));const accepted=[];for(const note of notes){const key=channel+':'+note,count=noteRefs.get(key)||0;if(count||send([0x90|channel,note,velocity])){noteRefs.set(key,count+1);accepted.push(note);log('WEB → FM‑1',noteName(note)+' '+velocity)}}if(accepted.length)sent.set(id,{notes:accepted,channel})}
function bridgeNoteOff(message){const id=String(message.id||'').slice(0,80),entry=sent.get(id);if(!entry)return;sent.delete(id);for(const note of entry.notes){const key=entry.channel+':'+note,count=noteRefs.get(key)||0;if(count<=1){noteRefs.delete(key);send([0x80|entry.channel,note,0]);log('WEB → FM‑1',noteName(note)+' OFF')}else noteRefs.set(key,count-1)}}
function onMidi(event){if(!input||event.currentTarget!==input)return;const bytes=event.data,status=bytes[0]&0xf0,channel=bytes[0]&0x0f,note=bytes[1],velocity=bytes[2]||0;if(status===0x90&&velocity>0||status===0x80||status===0x90&&velocity===0){const on=status===0x90&&velocity>0;log('FM‑1 → WEB',noteName(note)+(on?' '+velocity:' OFF'));editorPost('midi',{data:Array.from(bytes.slice(0,3))});if($('#route-in').checked&&!liveSupported)post('device-note',{note,velocity,channel,on,home:on&&$('#follow-home').checked})}else if(status===0xb0)log('FM‑1 → WEB','CC '+note+' / '+velocity)}
async function pollLive(){if(!liveLink||liveBusy||liveLink.pending||editorControls)return;liveBusy=true;try{const live=decodeLive(await liveLink.request(CMD.LIVE,[],1000));if(!liveSupported)post('panic-device');liveSupported=true;lastLive=live;editorPost('live',{live});if($('#route-in').checked)post('device-live',{connected:true,live,followHome:$('#follow-home').checked});if(live.underruns>lastUnderruns){log('AUDIO','缓冲延迟累计 '+live.underruns);lastUnderruns=live.underruns}$('#connection-mode').textContent=`实时镜像 · ${live.voices} 声部 · MASTER ${live.master}%`;}catch(e){if(!liveSupported&&String(e.message).includes('状态 2')){clearInterval(liveTimer);liveTimer=null;$('#connection-mode').textContent='标准 MIDI · 当前固件无实时快照';$('#capability').textContent='当前固件可演奏和编辑，但不支持逐声部实时快照；网页只按 MIDI 按键近似显示。更新到包含 LIVE 协议的构建后才可同步包络、伴奏与页面状态。';log('MIRROR','当前固件不支持 LIVE 快照')}else{const s=liveLink?.stats;log('MIRROR','实时快照中断：'+e.message+(s?` / RX ${s.frames} invalid ${s.invalid} mismatch ${s.mismatch} len ${s.lastLength} cmd ${s.lastCommand}`:''))}}finally{liveBusy=false}}
async function connect(autoRoute=false){
  if(connecting)return false;
  connecting=true;state('正在搜索…','请允许浏览器访问 MIDI（含 SysEx）；只查询 FM‑1 身份，不修改设备。','正在识别');
  try{
    if(!navigator.requestMIDIAccess)throw Error('当前浏览器不支持 Web MIDI；请用桌面版 Chrome 或 Edge 打开此网站。');
    access=await navigator.requestMIDIAccess({sysex:true});
    const found=await new Updater(access).find(id=>id.model==='FM-1');
    if(!found)throw Error('未找到响应身份查询的 FM‑1。请检查 USB 数据线、供电、权限以及其他占用 MIDI 的软件。');
    found.link.close();input=found.link.input;output=found.link.output;identity=found.id;
    liveLink=new NightfallLink(input,output);let nightfall=false;try{await liveLink.hello();nightfall=true}catch{liveLink.close();liveLink=null}
    input.onmidimessage=onMidi;
    access.onstatechange=()=>{if(input?.state==='disconnected'||output?.state==='disconnected')disconnect('USB 连接中断；重新连接后可继续使用浏览器模拟器。')};
    $('#capability').textContent=nightfall?'NIGHTFALL 独立固件已握手。下方实机编辑与演奏共用这一次连接；设备状态会同步到模拟面板。':'未收到 NIGHTFALL 专属握手。保留标准 MIDI 演奏；不把其他固件误识别为 NIGHTFALL。';
    $('#editor-link').textContent=nightfall?'前往同页实机编辑 ↓':'了解固件与更新 ↗';
    $('#editor-link').href=nightfall?'#edit':'/update.html';
    state(identity.text,nightfall?'实机已就绪；NIGHTFALL 参数通道已验证。':'实机已就绪；仅使用标准 MIDI。',nightfall?'NIGHTFALL / 可编辑':'FM‑1 / 标准 MIDI');
    log('LINK','识别 '+identity.text+(nightfall?' / NIGHTFALL':' / MIDI'));
    $('#editor-status').textContent=nightfall?'已连接 '+identity.text+'；编辑和演奏共享同一 USB MIDI 会话。':'该设备未通过 NIGHTFALL 握手，实机编辑保持禁用。';
    editorSession();
    if(nightfall){liveTimer=setInterval(pollLive,70);pollLive()}
    if(autoRoute){$('#route-out').checked=true;setMode(true)}return true;
  }catch(error){disconnect('连接失败：'+error.message);state('连接失败',error.message,'仅浏览器模拟');log('ERROR',error.message);return false}
  finally{connecting=false;$('#connect-btn').disabled=false}
}

window.addEventListener('message',event=>{if(event.origin!==location.origin||event.source!==frame.contentWindow||event.data?.protocol!==protocol)return;const message=event.data;if(message.type==='ready'){post('set-sound',{enabled:$('#browser-sound').checked});if(lastDetail)post('device-detail',{detail:lastDetail});if(lastLive&&$('#route-in').checked)post('device-live',{connected:true,live:lastLive})}else if(message.type==='web-audio-ready'){const resolve=webAudioReadyResolve;webAudioReadyResolve=null;resolve?.(!!message.ready)}else if(message.type==='resize'&&Number.isFinite(message.height)){frame.style.height=Math.max(570,Math.min(3500,Math.ceil(message.height)))+'px'}else if(message.type==='note-on')bridgeNoteOn(message);else if(message.type==='note-off')bridgeNoteOff(message)});
window.addEventListener('message',event=>{if(event.origin!==location.origin||event.source!==editorFrame.contentWindow||event.data?.protocol!==editorProtocol)return;const message=event.data;
  if(message.type==='ready')editorSession();
  else if(message.type==='request')editorRequest(message);
  else if(message.type==='midi-out')editorMidi(message.data);
  else if(message.type==='detail'&&Array.isArray(message.payload)&&message.payload.length===117&&message.payload.every(v=>Number.isInteger(v)&&v>=0&&v<=255)){lastDetail=message.payload;post('device-detail',{detail:lastDetail})}
  else if(message.type==='resize'&&Number.isFinite(message.height))editorFrame.style.height=Math.max(620,Math.min(2600,Math.ceil(message.height)))+'px';
  else if(message.type==='status')$('#editor-status').textContent=String(message.text||'').slice(0,180);
});
frame.addEventListener('load',()=>post('set-sound',{enabled:$('#browser-sound').checked}));
editorFrame.addEventListener('load',editorSession);
$('#connect-btn').addEventListener('click',()=>{if(identity)disconnect('正在重新识别设备。');connect(false)});
$('#disconnect-btn').addEventListener('click',async()=>{await disableWebOnly();disconnect('已手动断开；浏览器模拟器继续可用。')});
$('#mode-sim').addEventListener('click',()=>{$('#route-out').checked=false;releaseSent();setMode(false);document.querySelector('.cockpit').scrollIntoView({behavior:'smooth',block:'start'})});
$('#mode-link').addEventListener('click',async()=>{if(!identity&&!await connect(true))return;$('#route-out').checked=true;setMode(true);document.querySelector('.cockpit').scrollIntoView({behavior:'smooth',block:'start'})});
document.querySelectorAll('a[href="#edit"]').forEach(link=>link.addEventListener('click',()=>{if(!identity)connect(false)}));
$('#route-out').addEventListener('change',event=>{if(!event.target.checked)releaseSent();setMode(event.target.checked)});
$('#route-in').addEventListener('change',event=>{if(!event.target.checked){if($('#web-only').checked)disableWebOnly();post('device-live',{connected:false});post('panic-device')}else if(liveSupported)pollLive()});
$('#browser-sound').addEventListener('change',event=>{if(!event.target.checked&&$('#web-only').checked)disableWebOnly();post('set-sound',{enabled:event.target.checked})});
$('#web-only').addEventListener('change',event=>{if(event.target.checked)enableWebOnly();else disableWebOnly()});
$('#web-volume').addEventListener('input',event=>{$('#web-volume-value').textContent=event.target.value+'%';post('web-volume',{volume:Number(event.target.value)})});
$('#panic').addEventListener('click',panic);
$('#demo-signal').addEventListener('click',()=>{const notes=[53,60,65,69];notes.forEach((note,index)=>setTimeout(()=>{post('device-note',{note,velocity:78+index*12,channel:0,on:true,home:true});log('DEMO → CITY',noteName(note)+' '+(78+index*12))},index*170));setTimeout(()=>notes.forEach(note=>post('device-note',{note,velocity:0,channel:0,on:false})),1250)});
window.addEventListener('pagehide',()=>{releaseSent();releaseEditorNotes()});window.addEventListener('blur',()=>{releaseSent();releaseEditorNotes()});
document.documentElement.dataset.nightfallMainReady='1';
