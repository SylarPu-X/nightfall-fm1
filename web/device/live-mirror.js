/* A 240 × 240 optical city driven by firmware LIVE events. Coordinates and
 * event lifetime mirror src/city.c; an older firmware is labelled MIDI-only. */
export function startMirror(canvas,readLive,readDetail){
  const ctx=canvas.getContext('2d'),legacy=[];
  const ink={bg:'#0c0805',grid:'#49331f',dim:'#8a694b',amber:'#d89c53',bright:'#ffdfaa'};
  canvas.width=canvas.height=240;
  function note(note,velocity,on){
    const now=performance.now();
    if(on)legacy.push({note,track:0,source:'midi',velocity,envelope:velocity/127,received:now,active:true});
    else for(let i=legacy.length-1;i>=0;i--)if(legacy[i].note===note&&legacy[i].active){legacy[i].active=false;legacy[i].endedAt=now;break}
    while(legacy.length>24)legacy.shift();
  }
  function line(x0,y0,x1,y1,color=ink.amber,alpha=1,width=.7){
    ctx.strokeStyle=color;ctx.globalAlpha=alpha;ctx.lineWidth=width;ctx.beginPath();
    ctx.moveTo(x0,y0);ctx.lineTo(x1,y1);ctx.stroke();ctx.globalAlpha=1;
  }
  function project(x,z,h){const f=25600/(100+z*160/256);return[120+(x-120)*f/256+z*6/256,182-z*62/256-h*90*f/65536]}
  function world(x0,z0,h0,x1,z1,h1,color,alpha=1,width=.7){const a=project(x0,z0,h0),b=project(x1,z1,h1);line(a[0],a[1],b[0],b[1],color,alpha,width)}
  function label(value,x,y,color=ink.amber,size=7){ctx.fillStyle=color;ctx.font=`${size}px ui-monospace,monospace`;ctx.fillText(value,x,y)}
  function draw(){
    const t=performance.now(),live=readLive(),detail=readDetail(),exact=!!live,delta=live?(t-live.receivedAt)/1000:0;
    ctx.fillStyle=ink.bg;ctx.fillRect(0,0,240,240);
    const events=exact?live.events:legacy.filter(e=>e.active||t-e.endedAt<3000).map(e=>({...e,onsetAge:(t-e.received)/1000,endedAge:e.active?null:(t-e.endedAt)/1000}));
    label('NIGHTFALL / CITY SCAN',8,14,ink.amber,8);label(exact?(live.recording?'REC':live.playing?'RUN':'READY'):'MIDI',195,14,ink.bright,8);
    line(8,22,232,22,ink.grid);
    label(exact?['HOME','FX','SAVE','ENV','LFO','EDIT','GLO','SEQ','ARP','SCALE'][live.page]||'HOME':'OFFLINE',8,39,ink.bright,15);
    label(exact?`TRACK ${live.track+1} / ${live.tempo} BPM / STEP ${live.step+1}`:'AWAITING DEVICE',8,56,ink.dim,8);
    ctx.fillStyle='#0b0906';ctx.fillRect(5,73,230,111);
    for(let i=0;i<=6;i++){const z=i*256/6;world(15,z,0,225,z,0,ink.grid,.65)}
    for(let i=0;i<=5;i++){const x=21+i*198/5;world(x,0,0,x,256,0,ink.grid,.65);world(x,0,0,x,0,12,ink.grid,.65)}
    for(const e of events){const age=e.onsetAge+delta;if(age<0||age>=.84)continue;const z=age*256/.84,alpha=(1-age/.84)*.5;
      for(let h=75;h<315;h+=60)world(15,z,h,225,z,h,ink.amber,alpha*.12);
      world(15,z,0,225,z,0,ink.bright,alpha);world(15,z,0,15,z,315,ink.amber,alpha*.7);world(225,z,0,225,z,315,ink.amber,alpha*.7)}
    const ordered=events.slice().sort((a,b)=>Number(a.active)-Number(b.active));
    for(const e of ordered){
      const age=e.active?0:(e.endedAge||0)+delta;if(age>=3)continue;
      const z=e.active?(e.source==='key'?5:e.source==='seq'?59:12):12+age*244/3;
      const x=21+(Math.max(36,Math.min(96,e.note))-36)*198/60,w=e.source==='key'?6:e.source==='seq'?4:3;
      const h=Math.max(20,Math.min(310,26+(e.note-24)*4)),back=Math.min(256,z+9);
      const color=e.active?(e.source==='key'?ink.bright:ink.amber):ink.amber,alpha=e.active?1:.5*(1-age/3);
      const audible=!detail||!detail[24+e.track]||e.source!=='seq';
      const env=e.active&&audible?e.envelope*(detail?.[20+e.track]??100)/127:0;
      world(x-w,z,0,x-w,z,h,color,alpha);world(x-w,z,h,x+w,z,h,color,alpha);world(x+w,z,h,x+w,z,0,color,alpha);
      world(x-w,z,h,x-w,back,h,color,alpha);world(x-w,back,h,x+w,back,h,color,alpha);world(x+w,back,h,x+w,z,h,color,alpha);
      world(x-w,back,h,x-w,back,0,ink.dim,alpha*.7);world(x+w,back,h,x+w,back,0,ink.dim,alpha*.7);
      world(x-w,z,0,x-w,back,0,ink.grid,alpha);world(x+w,z,0,x+w,back,0,ink.grid,alpha);
      if(e.active&&h>110)for(let floor=h/3;floor<h;floor+=h/3)world(x-w,z,floor,x+w,z,floor,color,alpha*.3);
      if(env>.01){const level=h*env;world(x-w,z,level,x+w,z,level,ink.bright,1,1.3);world(x-w+1,z,2,x-w+1,z,level,ink.amber,env);world(x+w-1,z,2,x+w-1,z,level,ink.amber,env)}
    }
    ctx.fillStyle='#0c08051c';for(let y=74;y<184;y+=3)ctx.fillRect(5,y,230,1);
    label(exact?`REAL ENVELOPE / ${live.voices} VOICES`:'MIDI EVENTS / APPROXIMATE',8,195,ink.amber,8);
    label(exact?`MASTER ${live.master}% / ${live.underruns} LATE`:'CONNECT FOR REAL-TIME MIRROR',8,213,ink.dim,8);
    line(8,224,232,224,ink.grid);label('NIGHTFALL / FM-1',8,235,ink.dim,7);
  }
  function frame(){draw();requestAnimationFrame(frame)}requestAnimationFrame(frame);
  return{note,draw};
}
