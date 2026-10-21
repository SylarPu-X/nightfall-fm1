import {Updater} from './assets/fm1ota.js';
import {Package,productOf,logicalImage,sha256hex,validateStockPackage} from './assets/fm1pkg.js';
import {BackupConnection,captureBackup} from './assets/fm1backup.js';
const $=s=>document.querySelector(s);let midi=null,device=null,selected=null,busy=false,backupSaved=false;
const PREVIEW_URL=''; // Public source release intentionally excludes flashable binaries.
const PREVIEW_SHA256='';
function say(s){$('#status').textContent=s;$('#log').textContent+=`${new Date().toLocaleTimeString()} ${s}\n`}
function ready(){
  const model=device?.id?.model?.replace(/^ota-/,'');
  $('#flash').disabled=busy||!selected||!device||model!==selected.product.split('_')[0]||!$('#ack').checked;
  $('#backup').disabled=busy||!device||!/^FM-1_9\d\d$/.test(device.id.text)||/^FM-1_99[79]$/.test(device.id.text);
  $('#load-preview').disabled=true;
}
$('#ack').onchange=ready;
async function getMidi(){if(!navigator.requestMIDIAccess)throw Error('当前浏览器不支持 Web MIDI；请使用桌面 Chrome 或 Edge。');if(!midi)midi=await navigator.requestMIDIAccess({sysex:true});return midi}
$('#detect').onclick=async()=>{try{const access=await getMidi(),up=new Updater(access);const d=await up.find(id=>id.model==='FM-1'||id.model==='ota-FM-1');if(!d)throw Error('未找到 FM‑1；检查连接线、浏览器权限和其他 MIDI 软件。');d.link.close();device=d;$('#identity').textContent=`${d.id.text} · ${d.name}`;say(`识别成功：${d.id.text}`);ready()}catch(e){device=null;$('#identity').textContent='识别失败';say(e.message);ready()}};
async function choosePackage(bytes,label,expectedDigest=null){
  if(bytes.length>16*1024*1024)throw Error('文件超过 16 MiB，已拒绝。');
  const digest=await sha256hex(bytes);
  if(expectedDigest&&digest!==expectedDigest)throw Error('预览固件 SHA‑256 与构建记录不符，已拒绝刷写。');
  const product=productOf(bytes);
  let kind;
  if(product==='FM-1_015'){
    await validateStockPackage(bytes);
    kind='官方原厂 V15';
  }else if(/^FM-1_9\d\d$/.test(product)){
    const pkg=new Package(bytes);
    if(!new TextDecoder('latin1').decode(bytes).includes('FELUCCA-LOADER-1'))throw Error('缺少 Felucca 加载器标记。');
    if(pkg.product!==product)throw Error('包身份不匹配。');
    kind=expectedDigest?'NIGHTFALL 声纹地形 FM‑1_999（实机验证中的实验版）':'其他 FM‑1 开源实验固件';
  }else throw Error(`不支持的包身份：${product}`);
  selected={bytes,product,image:logicalImage(bytes),kind,digest};
  $('#file-info').textContent=`${label} · ${kind} · ${product} · ${bytes.length.toLocaleString()} 字节 · SHA‑256 ${digest}`;
  $('#postflight').hidden=true;
  say('固件包结构、身份与 SHA‑256 校验通过');
  ready();
}
$('#load-preview').onclick=async()=>{
  selected=null;ready();
  try{
    const response=await fetch(PREVIEW_URL,{cache:'no-store'});
    if(!response.ok)throw Error(`预览包下载失败（HTTP ${response.status}）。`);
    await choosePackage(new Uint8Array(await response.arrayBuffer()),'本站构建',PREVIEW_SHA256);
  }catch(e){$('#file-info').textContent=`拒绝预览包：${e.message}`;say(e.message);ready()}
};
$('#file').onchange=async()=>{
  selected=null;ready();const f=$('#file').files[0];if(!f)return;
  try{await choosePackage(new Uint8Array(await f.arrayBuffer()),f.name)}
  catch(e){$('#file-info').textContent=`拒绝文件：${e.message}`;say(e.message);ready()}
};
function archiveInfo(a){let pos=a.indexOf(0);if(pos<0)throw Error('设备 INFO 无效');const version=String.fromCharCode(...a.slice(0,pos));pos++;const engines=a[pos];pos+=5;for(let i=0;i<engines;i++){const end=a.indexOf(0,pos);if(end<0)throw Error('设备 INFO 无效');pos=end+1}const ext=a.slice(pos+1);const supported=/^FELUCCA /i.test(version)&&ext[0]===16&&ext[1]===0x55&&ext[2]===1&&ext[4]===0x4d&&ext[5]===1&&ext[8]===0x42&&ext[9]===1&&(ext[10]&1);return{version,supported}}
$('#backup').onclick=async()=>{if(busy||!device)return;busy=true;ready();let link;try{const up=new Updater(await getMidi());const d=await up.find(id=>id.model==='FM-1');if(!d)throw Error('设备未连接');const inp=d.link.input,out=d.link.output;d.link.close();link=new BackupConnection(inp,out);const info=archiveInfo(await link.request([1,[]]));if(!info.supported)throw Error('当前固件不支持完整备份，请先用原编辑器单独导出项目。');say(`正在备份 ${info.version}`);const archive=await captureBackup((r,o)=>link.request(r,o),info.version,(done,total)=>{$('#status').textContent=`备份 ${Math.round(done/Math.max(1,total)*100)}%`});const url=URL.createObjectURL(new Blob([JSON.stringify(archive)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download=`FM1-backup-${new Date().toISOString().slice(0,10)}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),60000);backupSaved=true;say('备份已触发下载；请确认文件确实保存成功，再勾选风险确认。')}catch(e){say(`备份失败：${e.message}`)}finally{link?.close();busy=false;ready()}};
window.addEventListener('beforeunload',e=>{if(busy){e.preventDefault();e.returnValue='刷写尚未完成'}});
$('#flash').onclick=async()=>{if(busy||!selected||!device||!$('#ack').checked)return;busy=true;ready();const picked=selected;let beganWrite=false;try{const access=await getMidi();const up=new Updater(access);const found=await up.find(id=>id.model==='FM-1'||id.model==='ota-FM-1');if(!found)throw Error('未找到 FM‑1');const inLoader=found.id.model==='ota-FM-1';const identity=found.id.text;found.link.close();if(inLoader&&!$('#resume-ack').checked)throw Error('设备在 OTA 模式。请确认已有备份并勾选“继续写入”。');if(!inLoader&&found.id.model!=='FM-1')throw Error('设备型号不匹配');if(picked.product.split('_')[0]!==found.id.model.replace(/^ota-/,''))throw Error('固件包与设备型号不匹配');if(!backupSaved&&/^FM-1_9\d\d$/.test(identity)&&!$('#ack').checked)throw Error('请先备份');const action=inLoader?'继续更新':'开始更新';if(!confirm(`${action}？\n设备：${identity}\n目标：${picked.product}（${picked.kind}）\nSHA‑256：${picked.digest}\n此操作会修改实机固件。请勿拔线或关闭页面。`))return;const step=(kind,n)=>{if(kind==='write')beganWrite=true;const label={start:'准备',verify:'校验',loader:'进入更新模式',write:'写入',reboot:'重启',done:'完成'}[kind]||kind;say(`${label}${n==null?'':` ${n}`}`);if(kind==='write')$('#progress').value=Math.min(95,10+n/13);if(kind==='done')$('#progress').value=100};if(inLoader){const ok=await up.resume(picked.image,step,picked.product);if(!ok)throw Error('续写未完成；设备可能仍在 OTA 模式。')}else await up.install(picked.image,picked.product,step);say(`设备报告 ${picked.product}，更新完成；请完成下方实机检查。`);$('#postflight').hidden=false;device=null;$('#identity').textContent='更新完成，请重新识别设备';}catch(e){say(`更新失败：${e.message}${beganWrite?'；写入可能已开始，请不要盲目重试其他包。':''}`)}finally{busy=false;ready()}};
