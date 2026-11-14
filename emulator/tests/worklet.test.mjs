// Prueba del AudioWorklet real en un contexto sin DOM, Node ni WorkerGlobalScope.
import vm from 'node:vm';import fs from 'node:fs';import assert from 'node:assert/strict';
let Processor;const messages=[];const context=vm.createContext({console,WebAssembly,TextDecoder,TextEncoder,AudioWorkletProcessor:class{constructor(){this.port={postMessage:m=>messages.push(m)};}},registerProcessor(name,cls){assert.equal(name,'tolhuin-firmware');Processor=cls;}});
const core=new vm.SourceTextModule(fs.readFileSync('dist/tolhuin.js','utf8'),{context,identifier:'tolhuin.js',initializeImportMeta(m){m.url='https://tolhuin.invalid/tolhuin.js';}});
const worklet=new vm.SourceTextModule(fs.readFileSync('dist/audio-worklet.js','utf8'),{context,identifier:'audio-worklet.js'});
await core.link(()=>{throw new Error('Import de Node inesperado');});await worklet.link(spec=>{assert.equal(spec,'./tolhuin.js');return core;});await worklet.evaluate();
const module=await WebAssembly.compile(fs.readFileSync('dist/tolhuin.wasm'));const p=new Processor({processorOptions:{module,nvs:{}}});
for(let i=0;i<20&&!p.ready;i++)await new Promise(r=>setImmediate(r));assert(p.ready,JSON.stringify(messages));
p.port.onmessage({data:{type:'pin',pin:8,down:1}});let peak=0;for(let i=0;i<80;i++){const left=new Float32Array(128),right=new Float32Array(128);assert(p.process([],[[left,right]]));for(const x of left){assert(Number.isFinite(x));peak=Math.max(peak,Math.abs(x));}}
assert(peak>.01);const frames=messages.filter(m=>m.type==='frame');assert(frames.length>0);assert.equal(frames.at(-1).state.active,1);assert.equal(frames.at(-1).screens.length,3);assert.equal(frames.at(-1).screens[0].length,57600);assert(messages.some(m=>m.type==='midiout'));
p.port.onmessage({data:{type:'pin',pin:8,down:0}});for(let i=0;i<80;i++)p.process([],[[new Float32Array(128),new Float32Array(128)]]);assert.equal(messages.filter(m=>m.type==='frame').at(-1).state.active,0);
console.log('PASS AudioWorklet: inicialización sin DOM, PCM estéreo, controles, MIDI y 3 framebuffers.');
