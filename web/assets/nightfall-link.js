// NIGHTFALL v1 application packets over non-commercial USB-MIDI SysEx.
export const CMD={HELLO:1,STATUS:2,NOTE_ON:3,NOTE_OFF:4,TEMPO:5,FX:6,STEP:7,PLAY:8,SAVE:9,LOAD:10,EXPORT:11,IMPORT_BEGIN:12,IMPORT_CHUNK:13,IMPORT_COMMIT:14,IMPORT_ABORT:15,CONTROL:16,DETAIL:17,LIVE:18};
export function decodeLive(p){
  if(!p||p.length<54||p[0]!==0||p[53]>23||
      (p.length!==54+p[53]*8&&p.length!==75+p[53]*8))throw Error('实机实时快照格式不正确');
  const now=(p[1]|p[2]<<8|p[3]<<16|p[4]<<24)>>>0;
  const events=[];
  for(let i=0;i<p[53];i++){
    const o=54+i*8,source=['key','midi','seq','web'][p[o+3]];
    if(p[o+2]>3||!source||p[o+1]>127||p[o+4]>127)throw Error('实机音符事件无效');
    events.push({serial:p[o],note:p[o+1],track:p[o+2],source,velocity:p[o+4],envelope:p[o+5]/255,
      onsetAge:p[o+6]/100,endedAge:p[o+7]===255?null:p[o+7]/100,active:p[o+7]===255});
  }
  const terrainAt=54+p[53]*8,hasTerrain=p.length===terrainAt+21;
  return{now,page:p[5],track:p[6],playing:!!p[7],recording:!!p[8],step:p[9],bar:p[10],master:p[11],underruns:p[12]|p[13]<<8,voices:p[14],tempo:p[15]|p[16]<<8,scene:p[17],queuedScene:p[18],homeView:p[19],selectedFx:p[20],scope:Array.from(p.slice(21,53)),events,
    terrainSerial:hasTerrain?p[terrainAt]:null,terrain:hasTerrain?Array.from(p.slice(terrainAt+1,terrainAt+21)):null};
}
function crc16(bytes){let crc=0xffff;for(const byte of bytes){crc^=byte<<8;for(let i=0;i<8;i++)crc=((crc<<1)^((crc&0x8000)?0x1021:0))&0xffff}return crc}
export function packet(command,sequence,payload=[]){if(payload.length>240)throw Error('负载过长');const out=Uint8Array.from([0x46,0x4e,1,command,sequence,payload.length,0,...payload,0,0]);const c=crc16(out.subarray(0,out.length-2));out[out.length-2]=c&255;out[out.length-1]=c>>8;return out}
export function unpack(frame){const data=Uint8Array.from(frame);if(data.length<9||data[0]!==0x46||data[1]!==0x4e||data[2]!==1||data.length!==9+(data[5]|data[6]<<8))throw Error('协议帧无效');const c=crc16(data.subarray(0,data.length-2));if(data.at(-2)!==(c&255)||data.at(-1)!==(c>>8))throw Error('协议校验失败');return {command:data[3],sequence:data[4],payload:data.slice(7,-2)}}
export function pack7(bytes){let acc=0,bits=0,out=[];for(const byte of bytes){acc|=byte<<bits;bits+=8;while(bits>=7){out.push(acc&127);acc>>>=7;bits-=7}}if(bits)out.push(acc&127);return out}
export function unpack7(bytes){let acc=0,bits=0,out=[];for(const byte of bytes){acc|=(byte&127)<<bits;bits+=7;if(bits>=8){out.push(acc&255);acc>>>=8;bits-=8}}return out}
export function sysex(bytes){return Uint8Array.from([0xf0,0x7d,0x4e,0x46,1,...pack7(bytes),0xf7])}
function readSysex(data){if(data.length<8||data[0]!==0xf0||data.at(-1)!==0xf7||data[1]!==0x7d||data[2]!==0x4e||data[3]!==0x46||data[4]!==1)return null;return unpack(unpack7(data.slice(5,-1)))}
export class NightfallLink{
  constructor(input,output){this.input=input;this.output=output;this.sequence=0;this.pending=null;this.stats={frames:0,invalid:0,mismatch:0,lastLength:0,lastCommand:0};this.listener=e=>this.receive(e);input.addEventListener('midimessage',this.listener)}
  close(){this.input.removeEventListener('midimessage',this.listener);if(this.pending){clearTimeout(this.pending.timer);this.pending.reject(Error('连接已断开'));this.pending=null}}
  receive(event){let reply;this.stats.frames++;this.stats.lastLength=event.data.length;try{reply=readSysex(event.data)}catch{this.stats.invalid++;return}if(!reply)return;this.stats.lastCommand=reply.command;if(!this.pending||reply.sequence!==this.pending.sequence||reply.command!==(this.pending.command|0x80)){this.stats.mismatch++;return}const waiting=this.pending;this.pending=null;clearTimeout(waiting.timer);if(reply.payload[0])waiting.reject(Error('设备拒绝命令 '+waiting.command+'，状态 '+reply.payload[0]));else waiting.resolve(reply.payload)}
  request(command,payload=[],timeout=1600){if(this.pending)return Promise.reject(Error('设备正在处理上一条命令'));const sequence=this.sequence++&255;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{this.pending=null;reject(Error('设备通信超时'))},timeout);this.pending={sequence,command,timer,resolve,reject};try{this.output.send(sysex(packet(command,sequence,payload)))}catch(e){clearTimeout(timer);this.pending=null;reject(e)}})}
  async hello(){const p=await this.request(CMD.HELLO);if(new TextDecoder().decode(p.slice(1,10))!=='NIGHTFALL'||p[10]!==1)throw Error('固件不是兼容的 NIGHTFALL v1');return p}
}
export async function probeNightfall(input,output){const link=new NightfallLink(input,output);try{await link.hello();return true}catch{return false}finally{link.close()}}
