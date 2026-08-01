// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   synth.h  -  Wrapper de hardware del motor de audio.
   Une el núcleo DSP puro (dsp.h) con el I2S del ESP32 y una tarea de render
   en el núcleo 0 (FreeRTOS). El loop principal sólo encola eventos -> sin
   carreras entre núcleos. NADIE más incluye ESP_I2S.
   ============================================================================ */
#pragma once
#include <stdint.h>

// Inicializa DSP + I2S + tarea de audio (núcleo 0). false si el I2S no arranca.
bool        synthInit();

// Do mayor por cada timbre (verificación humana de la cadena de audio).
void        synthAutotest();

// Timbre activo (lo que sonará en el próximo acorde disparado).
void        synthSetTimbre(uint8_t t);
void        synthNextTimbre();
uint8_t     synthTimbre();
const char* synthTimbreName(uint8_t t);

// Modo STRUM (rasgueo): si está activo, synthPlaySet dispara el acorde nota por
// nota de grave a agudo, con `ms` de retardo entre notas.
void        synthSetStrum(bool on, float ms);
bool        synthStrum();

// Modo ARPEGIADOR: si está activo, el acorde se arpegia (una nota por vez al BPM).
// pattern = estilo (ArpPattern). gate 0..1 = largo de nota del paso (<=0 = no toca).
void        synthSetArp(bool on, float bpm, int stepsPerBeat, int pattern, float gate = 0.0f);
bool        synthArp();
// Manda la SECUENCIA ya armada (octavas + scale-run resueltos por harmonyArpSequence).
void        synthArpPlay(const uint8_t* seq, uint8_t n);

// Config de arp por slot + nombres de presets (thread-safe: get directo, set vía cola).
struct ArpCfg;   // definido en dsp.h
ArpCfg      synthArpCfgGet(int slot);
void        synthArpCfgSet(int slot, const ArpCfg& c);
// Secuenciador de pasos (ARP_SEQ): patrón por slot (índice/silencio/ligadura) +
// la secuencia ACTIVA que reproduce el arp (la empuja el .ino al armar).
uint8_t     synthArpSeqGet(int slot, uint8_t* seq);   // copia hasta ARP_SEQ_LEN; devuelve largo
void        synthArpSeqSet(int slot, const uint8_t* seq, uint8_t len);
void        synthArpSetActiveSeq(const uint8_t* seq, uint8_t len);
const char* synthPresetName(int kind, int slot);            // kind 0=drum 1=arp
void        synthPresetNameSet(int kind, int slot, const char* name);

// Sync del arp a un MIDI clock entrante: ExtSync(true) desconecta el reloj interno;
// Advance() dispara un paso (lo llama el .ino en cada subdivisión de los ticks).
void        synthArpExtSync(bool on);
void        synthArpAdvance();

// Modo MONOFÓNICO: una sola nota (lead/bajo) con legato. El .ino usa synthMonoNote
// en vez de synthPlaySet cuando está activo.
void        synthSetMono(bool on);
bool        synthMono();
void        synthMonoNote(uint8_t note);   // toca/retarguetea la nota mono

// GLIDE / portamento: desliza la altura entre notas (sobre todo en mono/legato).
void        synthSetGlide(bool on, float ms);
bool        synthGlide();

// SUSTAIN conmutable: al soltar, las notas resuenan con un release largo (`ms`)
// en vez de cortarse. Pensado para un sinte de acordes (cola que se apaga sola).
void        synthSetSustain(bool on, float ms);
bool        synthSustain();

// TREMOLO global sincronizable: modula la amplitud de la salida a `rateHz`
// (normalmente derivada del BPM) con profundidad `depth` (0..1).
void        synthSetTremolo(bool on, float rateHz, float depth);
bool        synthTremolo();

// CHORUS estéreo: engorda y ensancha el sonido (LFO de retardo corto).
void        synthSetChorus(bool on, float rateHz, float depthMs, float mix);
bool        synthChorus();

// REVERB editable (web-config): room 0..1, damp 0..1, mix 0..1. Vía cola.
void        synthSetReverb(float room, float damp, float mix);

// Generate Random Sound: elige un timbre al azar y randomiza sus parámetros
// (osciladores/envolventes/filtro). Cambiar de timbre con TAP lo restaura.
void        synthRandomizeSound();

// --- Edición de timbres y patrones de batería (web-config por USB) ---
// Apply es thread-safe (vía cola, lo consume la tarea de audio); Get lee directo
// (los floats se leen enteros; una lectura durante un random es benigna).
struct EnvCfg;   // definido en dsp.h
void        synthEnvApply(uint8_t t, const EnvCfg& e);
void        synthEnvGet(uint8_t t, EnvCfg* out);
void        synthEnvFactory(uint8_t t, EnvCfg* out);
// Patrones de 16 pasos (semicorcheas): máscaras uint16, bit s = golpe en paso s.
void        synthDrumPatternSet(int pat, uint32_t kick, uint32_t snare, uint32_t hat = 0);  // vía cola
void        synthDrumPatternGet(int pat, uint32_t* kick, uint32_t* snare, uint32_t* hat = nullptr);
// Grilla del patrón: compases (1..2) y subdivisión (4 binaria / 3 ternaria).
void        synthDrumPatternCfgSet(int pat, uint8_t bars, uint8_t div);
void        synthDrumPatternCfgGet(int pat, uint8_t* bars, uint8_t* div);
// Grilla ACTIVA para la UI: pasos por compás (12/16) y total del patrón.
void        synthDrumsGridInfo(int* stepsPerBar, int* total);

// BODY: capa de "cuerpo/unísono" (detune por voz + drift + transitorio) que da
// organicidad genérica a todos los timbres (los acerca a un sample). amt 0..1.
void        synthSetBody(bool on, float amt);
bool        synthBody();

// LOOPER de 4 capas (audio). Control thread-safe (cola). layer 0..3.
// Record: EMPTY->graba->PLAY (define/loop) ; PLAY<->OVERDUB. Mute: PLAY<->MUTED.
void        synthLooperRecord(int layer);
void        synthLooperMute(int layer);
void        synthLooperClear(int layer);
void        synthLooperClearAll();
void        synthLooperStop();             // STOP del transporte (deja de sonar, conserva el loop)
int         synthLooperState(int layer);   // 0 EMPTY,1 REC,2 PLAY,3 OVERDUB,4 MUTED (para la UI)
int         synthLooperProgress();         // posición del loop 0..127 (barra del OLED; 0 sin loop)

// PERCUSIÓN: dispara un golpe de bombo, caja o hi-hat (one-shot, en la salida).
void        synthKick();
void        synthSnare();
void        synthHat();
// Reloj maestro del transporte: fija el tempo del que derivan su fase la batería,
// el arp y el trémolo (comparten el "1"). Llamar en cada cambio de BPM.
void        synthClockSet(float bpm);
// Disciplina la fase del reloj maestro a un clock MIDI externo (96 ticks/compás
// 4/4). Llamar una vez por pulso mientras se esclava a un clock entrante.
void        synthClockSyncTicks(uint32_t tickInBar);
// Patrón de batería sincronizado al BPM. pattern 0=off, 1..3 = patrón.
void        synthDrumsSet(int pattern, float bpm);
bool        synthDrums();
// Volumen general de la batería (0..1), editable por web y persistido en NVS.
void        synthDrumsSetGain(float g);
float       synthDrumsGain();
// Volúmenes independientes de synth (acorde+melodía) y sub-bajo (0..2, 1=100%).
void        synthSetSynthGain(float g);
float       synthSynthGain();
void        synthSetBassGain(float g);
float       synthBassGain();
// Motor de la batería: true = samples PCM (flash), false = sintetizada.
void        synthDrumsSetEngine(bool samples);
bool        synthDrumsEngine();
// Paso sonando de la grilla de batería (0..15) o -1 apagada (indicador OLED).
int         synthDrumsStepNow();
// Metrónomo: click sync BPM con acento en el 1 (guía para grabar loops).
void        synthMetroSet(bool on, float bpm);
// Golpe de batería en vivo (0=bombo 1=caja 2=hat); record=grabar cuantizado.
void        synthDrumHit(int inst, bool record);

// DELAY editable: parámetros directos, o sincronizado a un tempo (beatFraction:
// 1/4=1.0, 1/8=0.5, 1/8.=0.75, 1/16=0.25). Thread-safe (vía cola).
void        synthDelaySet(float timeMs, float feedback, float mix);
void        synthDelaySyncBpm(float bpm, float beatFraction);

// Hace sonar EXACTAMENTE este set de notas. Las notas comunes con el acorde
// anterior NO se re-disparan -> conducción de voces suave (salvo en STRUM).
// timbre 255 = el activo; el looper pasa el timbre con el que se grabó el loop.
void        synthPlaySet(const uint8_t* notes, uint8_t count, uint8_t timbre = 255);

// Libera todas las notas sostenidas (release). Anti notas-colgadas.
void        synthReleaseAll();
// Libera SOLO acorde + sub-bajo (conserva la voz mono): lo usan los eventos OFF
// del looper para no cortar la melodía en vivo. synthMonoOff suelta sólo la mono.
void        synthReleaseChord();
void        synthMonoOff();

// SUB-BAJO dedicado (raíz -1 octava con timbre propio; el "bass role" del
// HiChord). note < 0 apaga. Sólo re-dispara si la nota/timbre cambian.
void        synthSetBass(int note, uint8_t timbre);

// Nota MIDI directa (entrada externa USB/BLE): suena en el timbre actual.
void        synthNoteOn(uint8_t note, uint8_t vel);   // vel 0..127
void        synthNoteOff(uint8_t note);
// Con timbre explícito (melodía del looper: suena con el timbre grabado).
void        synthNoteOnT(uint8_t note, uint8_t vel, uint8_t timbre);

// Nivel de salida (pico del último bloque, 0..1) para la pantalla.
float       synthGetLevel();

// Diagnóstico de CPU del audio: peor bloque en µs (con reset opcional) y cuántos
// bloques superaron el presupuesto (~5.8 ms) desde el arranque (acumulativo).
// Un "late" > 0 significa riesgo real de underrun (el ruido al soltar acordes).
void        synthPerfStats(uint32_t* maxUs, uint32_t* lateBlocks, bool reset);

// Reloj musical: muestras rendereadas desde el arranque (mismo reloj que la
// batería). El .ino lo convierte a pasos de semicorchea para el looper de eventos.
uint32_t    synthSamplePos();

// Forma de onda reciente para el osciloscopio de la pantalla: copia n muestras
// mono (-1..1, n<=128) de más vieja a más nueva.
void        synthGetScope(float* dst, int n);
