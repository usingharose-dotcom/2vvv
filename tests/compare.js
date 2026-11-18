// Runs the original browser AudioWorklet DSP (from the HTML) in node and compares with the C++ port.
const fs=require('fs'),cp=require('child_process');
const html=fs.readFileSync(process.argv[2],'utf8');
const body=html.match(/function tuneProc\(\)\{([\s\S]*?)registerProcessor\("yolo-tune",YoloTune\)\}/)[1];
function run(sr,n,P){
  let Cls;globalThis.sampleRate=sr;
  globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage(){},onmessage:null}}};
  Cls=new Function(body+';return YoloTune;')();
  const d=new Cls();d.port.onmessage({data:Object.assign({},P,{lowlat:P.lowlat})});
  const inp=new Float32Array(n);let ph=0;
  for(let i=0;i<n;i++){const t=i/sr,f=220*Math.pow(2,(0.4*Math.sin(t*1.3)+t*0.2)/12);ph+=f/sr;inp[i]=0.4*Math.sin(2*Math.PI*ph)+0.1*Math.sin(4*Math.PI*ph)}
  const L=new Float32Array(n),R=new Float32Array(n);
  for(let s=0;s<n;s+=128){const m=Math.min(128,n-s);const o=[[new Float32Array(m),new Float32Array(m)]];d.process([[inp.subarray(s,s+m)]],o);L.set(o[0][0],s);R.set(o[0][1],s)}
  const mask=P.allowed.join('');
  const out=cp.execFileSync('./dsp_test',[sr,n,P.speed,P.amount,P.sustain,P.stab,P.dbl,P.flex,P.hum,P.inst?1:0,P.lowlat?1:0,mask],{maxBuffer:1<<28});
  const C=new Float32Array(out.buffer,out.byteOffset,2*n);
  let md=0,rms=0;for(let i=0;i<n;i++){md=Math.max(md,Math.abs(C[i]-L[i]),Math.abs(C[n+i]-R[i]));rms+=L[i]*L[i]}
  return {maxDiff:md,sigRms:Math.sqrt(rms/n)};
}
const maj=[1,0,1,0,1,1,0,1,0,1,0,1];
const cases=[
 {speed:0,amount:1,sustain:0,stab:0,dbl:0,flex:0,hum:0,inst:false,lowlat:true,allowed:maj},
 {speed:40,amount:0.8,sustain:0.4,stab:2,dbl:0.5,flex:0.3,hum:0.2,inst:false,lowlat:false,allowed:maj},
 {speed:12,amount:1,sustain:0.2,stab:1,dbl:0,flex:0,hum:0,inst:true,lowlat:true,allowed:[1,1,1,1,1,1,1,1,1,1,1,1]},
];
for(const sr of [44100,48000])for(const P of cases){const P2=Object.assign({bypass:false,ref:0,inG:1,outG:1},P);console.log(sr,JSON.stringify(run(sr,sr*4,P2)))}
