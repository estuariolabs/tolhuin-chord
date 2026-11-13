import createTolhuin from './tolhuin.js';
// El firmware completo corre en el hilo de audio. Una sola instancia es dueña
// del estado y del DSP: no hay copias JS de la lógica musical.
class TolhuinProcessor extends AudioWorkletProcessor {
 constructor(options){
  super();this.ready=false;this.pos=256;this.frames=0;
  const mod=options.processorOptions.module;
  createTolhuin({instantiateWasm(imports,done){const instance=new WebAssembly.Instance(mod,imports);done(instance);return instance.exports;},print(){},printErr(s){console.error(s);}}).then(m=>{
   this.m=m;this.command=m.cwrap('th_command',null,['string']);this.restore=m.cwrap('th_restore',null,['string','string']);
   for(const [k,v] of Object.entries(options.processorOptions.nvs||{}))this.restore(k,v);
   m._th_init();this.ready=true;this.port.postMessage({type:'ready'});
  }).catch(e=>this.port.postMessage({type:'error',message:String(e)}));
  this.port.onmessage=e=>{if(!this.ready)return;const d=e.data,m=this.m;
   if(d.type==='pin')m._th_pin(d.pin,d.down);
   if(d.type==='joy')m._th_joy(d.x,d.y);
   if(d.type==='command'){this.command(d.text);this.port.postMessage({type:'nvs',data:JSON.parse(m.UTF8ToString(m._th_nvs()))});}
   if(d.type==='midi'){for(const p of d.packets)m._th_midi(p[0],p[1]||0,p[2]||0,p.length);}
   if(d.type==='panic'){m._th_panic();for(let n=0;n<128;n++)m._th_midi(128,n,0,3);}
  };
 }
 process(inputs,outputs){
  const out=outputs[0];if(!this.ready)return true;
  for(let i=0;i<out[0].length;i++){
   if(this.pos===256){this.ptr=this.m._th_audio()>>>2;this.pos=0;}
   out[0][i]=this.m.HEAPF32[this.ptr+this.pos];out[1][i]=this.m.HEAPF32[this.ptr+256+this.pos];this.pos++;
  }
  // MIDI sale cada quantum; las imágenes y el estado sólo a 15 fps.
  const packets=[];for(let v;(v=this.m._th_midi_out())!==-1;){const cin=v&15;if(cin>=4&&cin<=7)continue;const n=cin===5||cin===15?1:cin===6||cin===12||cin===13?2:3;packets.push([v>>>8&255,v>>>16&255,v>>>24&255].slice(0,n));}
  if(packets.length)this.port.postMessage({type:'midiout',packets});
  if((this.frames+=out[0].length)>=2940){this.frames=0;const m=this.m;
   const oled=m.HEAPU8.slice(m._th_oled(),m._th_oled()+8192);const sp=m._th_scope()>>>2;const scope=m.HEAPF32.slice(sp,sp+128);
   const screens=[0,1,2].map(t=>{const p=m._th_screen(t)>>>1;return m.HEAPU16.slice(p,p+57600);});
   this.port.postMessage({type:'frame',screens,state:JSON.parse(m.UTF8ToString(m._th_state())),oled,scope,log:m.UTF8ToString(m._th_log()),nvs:JSON.parse(m.UTF8ToString(m._th_nvs()))},[oled.buffer,scope.buffer,...screens.map(s=>s.buffer)]);
  }
  return true;
 }
}
registerProcessor('tolhuin-firmware',TolhuinProcessor);
