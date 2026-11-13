// BLE MIDI (servicio 03B80E5A): timestamps se retiran en la capa de transporte.
// Conserva running status y SysEx entre notificaciones; realtime no los altera.
export class BleMidiDecoder {
 constructor(){this.running=0;this.pending=[];this.sysex=false;}
 feed(data){
  if(data.length<2||!(data[0]&128))return new Uint8Array();
  let timestamp=true,out=[];
  for(let i=1;i<data.length;i++){
   const b=data[i];
   if(timestamp){if(b&128){timestamp=false;continue;}timestamp=false;}
   if(this.sysex){if(b===247){out.push(b);this.sysex=false;timestamp=true;}else if(b>=248)out.push(b);else if(b<128)out.push(b);continue;}
   if(b===240){out.push(b);this.sysex=true;this.pending=[];this.running=0;continue;}
   if(b>=248){out.push(b);timestamp=true;continue;}
   if(b&128){this.running=b;this.pending=[b];}else if(this.running)this.pending.push(b);else continue;
   const hi=this.running&240,n=hi===192||hi===208?2:3;
   if(this.pending.length===n){out.push(...this.pending);this.pending=[this.running];timestamp=true;}
  }
  return new Uint8Array(out);
 }
}
