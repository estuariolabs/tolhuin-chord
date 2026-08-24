/* ============================================================================
   midi.h  -  Entrada MIDI por USB (TinyUSB) y Bluetooth LE (NimBLE).
   Recibe:
     - MIDI clock (0xF8): calcula el BPM -> sincroniza delay/arp.
     - note on/off: toca el synth en el timbre actual.
     - start/stop: (reservado) reinicio de fase.
   Capa de hardware (depende de Arduino/ESP32). El motor (dsp) no la conoce.
   ============================================================================ */
#pragma once
#include <stdint.h>

void  midiInit();          // inicializa USB MIDI + BLE MIDI
void  midiUpdate();        // procesa mensajes entrantes (llamar en cada loop)

bool  midiClockChanged();  // true una vez cuando hay un BPM nuevo estable
float midiClockBpm();      // último BPM calculado del MIDI clock externo
bool  midiBleConnected();  // estado de la conexión BLE (para UI/serial)
uint32_t midiClockTickCount(); // ticks 0xF8 acumulados (se reinicia en Start); sync del arp
bool     midiClockLive();      // true si llegó un tick hace < 500 ms (hay clock externo)

// --- MIDI clock OUT (TOLHUIN como master de tempo) ---
// Set(true): manda Start (0xFA) y empieza a emitir clock (0xF8) a 24 PPQN.
// Set(false): manda Stop (0xFC). Update(bpm): se llama en cada loop y emite los
// ticks vencidos según el tempo (sigue cambios de BPM en vivo).
void  midiClockOutSet(bool on);
void  midiClockOutUpdate(uint16_t bpm);
bool  midiClockOut();      // estado actual (para la UI)

// --- SysEx del web-config (transporte MIDI del editor, como el HiChord) ---
// Manda `text` (ASCII) envuelto en F0 7D 02 ... F7 por USB, en paquetes USB-MIDI.
// La recepción (F0 7D 01 <comando> F7) la maneja midiUpdate -> webcfgFeedMidi.
void  midiSendSysExText(const char* text, int len);

// --- MIDI OUT: manda lo que tocás por USB + BLE (para grabar en un DAW/GarageBand) ---
void  midiSendNoteOn(uint8_t note, uint8_t vel);
void  midiSendNoteOff(uint8_t note);
// Manda un acorde entero: note-offs + note-ons en UN solo paquete BLE (evita que
// el stack descarte notificaciones en ráfaga). vel = velocity de las note-on.
void  midiSendChord(const uint8_t* off, uint8_t nOff,
                    const uint8_t* on,  uint8_t nOn, uint8_t vel);
