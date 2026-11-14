import assert from 'node:assert/strict';import {BleMidiDecoder} from '../web/midi-transport.js';
let p=new BleMidiDecoder();assert.deepEqual([...p.feed(new Uint8Array([128,128,144,60,100,129,64,100,130,67,100]))],[144,60,100,144,64,100,144,67,100]);
assert.deepEqual([...p.feed(new Uint8Array([128,131,144,60,0,132,248]))],[144,60,0,248]);
p=new BleMidiDecoder();assert.deepEqual([...p.feed(new Uint8Array([128,128,240,125,2,123,34]))],[240,125,2,123,34]);assert.deepEqual([...p.feed(new Uint8Array([128,129,97,34,58,49,125,130,247]))],[97,34,58,49,125,247]);
p=new BleMidiDecoder();assert.deepEqual([...p.feed(new Uint8Array([128,128,144,60,248,100]))],[248,144,60,100]);
console.log('PASS BLE MIDI: acordes running status, note-off velocity 0, realtime y SysEx fragmentado.');
