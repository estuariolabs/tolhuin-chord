// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   dsp.h  -  Núcleo de síntesis PURO (sin hardware).
   Osciladores, envolventes, filtro, render por muestra y mezcla.
   NO incluye ESP_I2S, FreeRTOS ni Arduino: compila también en host (g++).

   El wrapper de hardware (synth.cpp) encola eventos y llama a:
     dspInit() una vez, dspNoteOn/Off/AllOff() desde la cola,
     dspRenderBlock() por bloque para llenar el buffer de salida.

   3 timbres hechos a mano (idénticos al motor MVP, sin AMY):
     BRASS   = onda cuadrada brillante (estilo HiChord)
     EPIANO  = FM 2 operadores, percusivo (Rhodes)
     STRINGS = 3 saws detuneados, cálido
   ============================================================================ */
#pragma once
#include <stdint.h>
#include "config.h"

enum Timbre {
  T_BRASS = 0, T_EPIANO, T_STRINGS,   // originales
  T_SINE, T_TRIANGLE, T_ORGAN,        // paleta extendida (T4.1)
  T_FLUTE,                            // flauta aditiva + aliento (estilo HiChord)
  T_WAVE,                             // wavetable MORPHING: 4 frames, barrido por la env de filtro
  T_USER1, T_USER2, T_USER3,          // timbres de USUARIO: familia de oscilador
  T_USER4, T_USER5, T_USER6,          // elegible (EnvCfg.family) + params libres
  T_COUNT
};

// Cantidad de patrones de batería: 0=off, 1..3 fábrica, 4..11 editables (usuario).
#define DRUM_PATTERNS 12
// Slots de arpegiador de usuario (0..ARP_SLOTS-1). El 0 es un preset "clásico".
#define ARP_SLOTS 6
// Largo de los nombres editables de presets (drums/arps), incluido el '\0'.
#define PRESET_NAME_LEN 13
// Pasos por patrón: 16 semicorcheas en un compás 4/4 (negra = 4 pasos, corchea
// = 2, silencios = bits en 0). Máscaras uint16_t, bit s = golpe en el paso s.
#define DRUM_STEPS 16

// ----------------------------------------------------------------------------
// Config por timbre (pública: la edita el web-config por USB).
//  atkMs/decMs/relMs (amplitud), sus (0..1);
//  fmDec/fmIdx/fmFloor (FM del epiano; fmDec/fmDec2 son INCREMENTOS por muestra,
//  ver msInc: inc = 1/(ms*SR/1000));
//  cutHz/res (filtro base), fEnvOct (octavas que abre la env de filtro),
//  fAtkMs/fDecMs/fSus/fRelMs (envolvente de filtro); lfoRate/vib/trem (LFO).
// ----------------------------------------------------------------------------
struct EnvCfg {
  float atkMs, decMs, sus, relMs;
  float fmDec, fmIdx, fmFloor;
  float cutHz, res, fEnvOct, fAtkMs, fDecMs, fSus, fRelMs;
  float lfoRate, vib, trem;   // LFO: rate (Hz), prof. vibrato (pitch), prof. trémolo (amp)
  bool  perc;
  // Capa "tine"/ataque (sólo EPIANO; 0 en el resto): parcial ADITIVO a
  // tineRatio·f — fmIdx2 = amplitud del parcial, fmDec2 = su decay. Los índices
  // FM (fmIdx) van en unidades de FASE de fastSine (0..1 = 2π radianes).
  float fmDec2 = 0, fmIdx2 = 0, tineRatio = 0;
  // WAVE: profundidad del morph de wavetable (0..1). La posición efectiva es
  // wtMorph * nivel de la envolvente de filtro -> el espectro BARRE los 4
  // frames (oscuro -> brillante) siguiendo la dinámica de la nota.
  float wtMorph = 0;
  // FAMILIA de oscilador (0..T_WAVE, 8 familias base). En los timbres base es su
  // propio índice; en T_USER1..6 elige QUÉ motor de oscilador usa el slot (el
  // resto de los parámetros son libres). Cambia el sizeof -> el cfgv de NVS
  // descarta blobs viejos (necesario: T_WAVE corre los índices USR).
  uint8_t family = 0;
};

// ----------------------------------------------------------------------------
// Envolvente ADSR lineal, sin clicks. Curva continua entre etapas:
//   ATK sube hasta 1 (desde el nivel actual -> retrigger suave),
//   DEC baja hasta `sustain`, SUS mantiene, REL baja hasta 0.
// `perc` = true: tras el decay va directo a 0 y termina (sin sustain, p.ej. Rhodes).
// Se expone para poder testearla de forma aislada en host (T1.2).
// ----------------------------------------------------------------------------
struct Adsr {
  // Prefijo ST_ para no chocar con macros de Arduino (DEC/HEX/OCT/BIN en Print.h).
  enum Stage { ST_IDLE = 0, ST_ATK, ST_DEC, ST_SUS, ST_REL };
  Stage stage   = ST_IDLE;
  float level   = 0.0f;   // nivel actual 0..1
  float atkInc  = 0.0f;   // incremento por muestra en ataque
  float decInc  = 0.0f;   // decremento por muestra en decay
  float relInc  = 0.0f;   // decremento por muestra en release
  float sustain = 0.0f;   // nivel de sostenido 0..1
  bool  perc    = false;  // percusivo: decae a 0 sin sostener

  // Configura tiempos en ms y nivel de sustain (sr = sample rate).
  void  config(float atkMs, float decMs, float susLevel, float relMs, bool percussive, float sr);
  void  noteOn();         // dispara ataque desde el nivel actual (sin salto)
  void  noteOff();        // pasa a release
  void  setReleaseMs(float relMs, float sr);  // recalcula sólo el release (sustain dinámico)
  float process();        // avanza una muestra y devuelve el nivel
  bool  active() const { return stage != ST_IDLE; }
};

// ----------------------------------------------------------------------------
// Filtro state-variable de 2 polos (topología TPT / Zavalishin).
// Incondicionalmente estable para cualquier cutoff < Nyquist. Salida pasa-bajos.
// Q = resonancia (0.5 = sin pico; >0.7 empieza a resonar). Se expone para tests.
// ----------------------------------------------------------------------------
struct Svf {
  float ic1eq = 0.0f, ic2eq = 0.0f;   // estados internos
  float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
  void  setCoeffs(float cutoffHz, float Q, float sr);
  float lp(float in);                 // procesa una muestra (pasa-bajos)
  void  reset() { ic1eq = ic2eq = 0.0f; }
};

// ----------------------------------------------------------------------------
// LFO sinusoidal de baja frecuencia. process(dt) avanza dt segundos y devuelve
// el valor en [-1, 1]. Se usa para vibrato (pitch) y trémolo (amplitud).
// Se expone para tests.
// ----------------------------------------------------------------------------
struct Lfo {
  float phase = 0.0f;   // 0..1
  float rate  = 5.0f;   // Hz
  void  setRate(float hz) { rate = hz; }
  void  reset()           { phase = 0.0f; }
  float process(float dt);
};

// ----------------------------------------------------------------------------
// Delay estéreo: una línea por canal con tiempos distintos (L≠R) para dar
// amplitud, realimentación (feedback) y mezcla seca/húmeda. Estable si fb<1.
// Buffer dimensionado para el retardo máximo (~300 ms a SAMPLE_RATE).
// Se expone para tests (respuesta al impulso).
// ----------------------------------------------------------------------------
struct DelayStereo {
  static const int MAXLEN = (int)(SAMPLE_RATE * 2 / 5);    // ~400 ms (para tiempos musicales)
  float bufL[MAXLEN];
  float bufR[MAXLEN];
  int   widx = 0;
  int   dL = 1, dR = 1;     // retardo por canal en muestras
  float fb = 0.0f;          // realimentación 0..<1
  float mix = 0.0f;         // mezcla húmeda 0..1
  void  setParams(float timeMs, float feedback, float mixAmt, float sr);
  void  setTime(float timeMs, float sr);                     // sólo el tiempo (dL, dR)
  void  process(float inMono, float& outL, float& outR);     // dry+eco (crossfade)
  void  processWet(float inMono, float& wetL, float& wetR);   // sólo eco*mix (send)
  void  reset();
};

// ----------------------------------------------------------------------------
// Reverb Freeverb-lite (Schroeder): 4 combs amortiguados en paralelo + 2 allpass
// en serie. Mono (la imagen estéreo la da el delay). process() devuelve la cola
// húmeda ya escalada por la mezcla. Tamaños de buffer fijos para 44100 Hz.
// Se expone para tests (respuesta al impulso / RT60).
// ----------------------------------------------------------------------------
struct Reverb {
  static const int C0 = 1116, C1 = 1188, C2 = 1277, C3 = 1356;  // combs (muestras)
  static const int A0 = 556,  A1 = 441;                          // allpass (muestras)
  float c0[C0], c1[C1], c2[C2], c3[C3];
  int   i0 = 0, i1 = 0, i2 = 0, i3 = 0;
  float s0 = 0, s1 = 0, s2 = 0, s3 = 0;   // estado del amortiguamiento (lowpass)
  float ap0[A0], ap1[A1];
  int   ai0 = 0, ai1 = 0;
  float fb = 0.84f;   // realimentación de los combs (tamaño de sala)
  float damp = 0.2f;  // amortiguamiento de agudos en la cola
  float mix = 0.0f;   // nivel de cola húmeda 0..1
  void  setParams(float roomSize, float damping, float mixAmt);
  float process(float in);   // devuelve la cola húmeda * mix
  void  reset();
};

// ----------------------------------------------------------------------------
// Chorus estéreo: una línea de retardo corta (~5..30 ms) modulada por un LFO,
// con dos tomas (L/R) en cuadratura para ensanchar. Da el "engorde" tipo HiChord.
// Se expone para tests (modulación L/R decorrelacionada).
// ----------------------------------------------------------------------------
struct Chorus {
  static const int MAXLEN = (int)(SAMPLE_RATE * 0.04f);   // 40 ms
  float buf[MAXLEN];
  int   widx = 0;
  float lfoPhase = 0.0f;
  float lfoInc = 0.0f;       // ciclos por muestra (rate/SR)
  float baseS = 0.0f;        // retardo base en muestras
  float depthS = 0.0f;       // profundidad de modulación en muestras
  float mix = 0.0f;          // nivel húmedo 0..1
  void  setParams(float rateHz, float depthMs, float mixAmt, float sr);
  void  process(float in, float& wetL, float& wetR);   // dos tomas moduladas (L≠R) * mix
  void  reset();
};

// Inicializa tabla de seno y configuración de envolventes por timbre.
void dspInit();

// GRUPO de una voz: a qué CAPA pertenece. El glide poliacorde sólo mueve voces
// VG_CHORD (antes agarraba la melodía del loop y el bajo, y los "liberaba" al
// re-conducir el acorde: capas de tomas distintas se chocaban); los note-off
// pueden apuntar a un grupo (misma altura en dos capas ya no se matan entre sí).
enum VoiceGroup : uint8_t { VG_CHORD = 0, VG_MEL = 1, VG_BASS = 2, VG_ANY = 255 };

// Gestión de voces (asignación, robo de voz por menor envolvente).
// vel: ganancia 0..1 de la voz (humanización/dinámica). delaySamples: micro-retardo
// de arranque (humanización del rasgueo natural). Ambos opcionales.
void dspNoteOn(uint8_t note, uint8_t timbre, float vel = 1.0f, int delaySamples = 0,
               uint8_t group = VG_CHORD);
void dspNoteOff(uint8_t note, uint8_t group = VG_ANY);
void dspAllOff();

// --- Glide / portamento (deslizamiento de altura entre notas) ---
// on=false: cambio de nota instantáneo. ms = tiempo aproximado por octava.
void dspSetGlide(bool on, float ms);

// --- Chorus estéreo (engrosa el sonido) ---
// on=false: bypass. rateHz/depthMs/mix configuran el LFO y la mezcla húmeda.
void dspSetChorus(bool on, float rateHz, float depthMs, float mix);

// --- Reverb editable (web-config) ---
// roomSize ~0..1 (tamaño de sala), damping 0..1 (oscurece la cola), mix 0..1.
void dspSetReverb(float roomSize, float damping, float mix);

// --- BODY: capa de "cuerpo/unísono" para acercar los timbres a un sample ---
// Suma offsets de pitch por voz (sin tocar los osciladores): detune fijo por voz
// (spread de ensamble), drift lento (inestabilidad analógica) y un transitorio de
// ataque que se asienta. amt 0..1 escala las tres. Da vida/organicidad genérica.
void dspSetBody(bool on, float amt);

// --- Generate Random Sound: randomiza los parámetros del timbre `t` ---
// Sortea ENV[t] (envolventes, filtro, LFO) con valores musicales. La familia de
// oscilador se varía eligiendo un timbre al azar antes de llamar (capa synth).
// `seed` 0 = usar una semilla fija. dspResetSound restaura el timbre original.
void dspRandomizeSound(uint8_t t, uint32_t seed);
void dspResetSound(uint8_t t);     // restaura ENV[t] a su valor por defecto (preset del usuario)

// --- Edición de timbres (web-config por USB) ---
// Tres niveles: ENV (vivo) / ENV_DEFAULT (preset del usuario, lo que restaura
// dspResetSound tras un Random o al elegir timbre) / fábrica (constante).
// dspEnvSet escribe el vivo Y el preset del usuario -> la edición sobrevive al
// cambio de timbre; se persiste en NVS desde la capa de hardware (webcfg).
EnvCfg dspEnvGet(uint8_t t);                    // parámetros vivos del timbre
void   dspEnvSet(uint8_t t, const EnvCfg& e);   // aplica (vivo + preset usuario)
EnvCfg dspEnvFactory(uint8_t t);                // valores de fábrica (para "reset")

// --- Edición de patrones de batería ---
// Cada patrón tiene su GRILLA: bars (1..2) × 4 pulsos × div pasos por pulso
// (4 binaria / 3 ternaria) -> hasta 32 pasos, máscaras uint32_t (bit s = golpe
// en el paso s). pat 1..DRUM_PATTERNS-1 (0 = off, no editable).
void dspDrumPatternSet(int pat, uint32_t kick, uint32_t snare, uint32_t hat = 0);
void dspDrumPatternGet(int pat, uint32_t* kick, uint32_t* snare, uint32_t* hat = nullptr);
// Variante por instrumento (0=kick 1=snare 2=hat), para la cola de eventos.
void dspDrumPatternSetInst(int pat, int inst, uint32_t mask);
// Config de la grilla del patrón (editable por web; recalcula el reloj en vivo).
void dspDrumPatternCfgSet(int pat, uint8_t bars, uint8_t div);
void dspDrumPatternCfgGet(int pat, uint8_t* bars, uint8_t* div);
// Grilla ACTIVA (para la UI): pasos por compás (12 o 16) y total.
void dspDrumsGridInfo(int* stepsPerBar, int* total);

// --- Nombres editables de presets (kind 0 = drums, 1 = arps) ---
void        dspPresetNameSet(int kind, int slot, const char* name);
const char* dspPresetNameGet(int kind, int slot);

// --- Tremolo global sincronizable (modula la amplitud de la salida) ---
// on=false: sin tremolo. rateHz: velocidad (normalmente derivada del BPM).
// depth 0..1: profundidad de la modulación. Se aplica al final de la cadena.
void dspSetTremolo(bool on, float rateHz, float depth);

// --- Sustain conmutable (release largo al soltar) ---
// on=false: cada voz usa el release de su timbre (corto). on=true: las voces que
// se suelten usan `relMs` (largo) -> el acorde resuena y se apaga solo, estilo
// "sostenutos" del HiChord. Afecta voces ya sonando al soltarlas (release en vivo).
void dspSetSustain(bool on, float relMs);

// --- Modo monofónico (last-note priority + legato + glide) ---
// Toca UNA sola voz: si ya hay nota sonando, cambia la altura sin re-atacar
// (legato; con glide, desliza). dspMonoOff() la suelta.
void dspMonoNote(uint8_t note, uint8_t timbre);
void dspMonoOff();

// --- Glide polifónico entre acordes ---
// Reasigna las voces que ya suenan a las notas del acorde nuevo por mínima
// distancia y RETARGUETEA su altura (deslizan, legato, sin re-atacar). Las notas
// extra arrancan voces nuevas; las voces sobrantes se liberan. Da portamento de
// acorde a acorde cuando el glide está activo.
void dspGlideChord(const uint8_t* notes, uint8_t n, uint8_t timbre);

// ----------------------------------------------------------------------------
// STRUM (rasgueo estilo Omnichord/HiChord): dispara el acorde nota por nota,
// de grave a agudo, con un retardo `strumMs` entre notas.
// ----------------------------------------------------------------------------
// Plan de rasgueo: notas ordenadas ascendente y la muestra en que dispara cada una.
struct StrumPlan {
  uint8_t note[MAX_CHORD_NOTES];
  int     atSample[MAX_CHORD_NOTES];   // i * (strumMs en muestras)
  uint8_t count;
};
// Construye el plan (puro, testeable): ordena ascendente y espacia por el retardo.
StrumPlan dspMakeStrum(const uint8_t* notes, uint8_t n, float strumMs, float sr);

// Programa un acorde rasgueado: las notas se disparan en su momento durante el
// render (los note-on se cuelan en blockSetup según el plan).
void dspStrumChord(const uint8_t* notes, uint8_t n, uint8_t timbre, float strumMs, float sr);

// ----------------------------------------------------------------------------
// ARPEGIADOR: reloj de tempo; toca las notas del acorde una por vez en las
// subdivisiones del BPM, con patrón up/down/updown.
// ----------------------------------------------------------------------------
enum ArpPattern {
  ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_DOWNUP,
  ARP_CONVERGE, ARP_DIVERGE, ARP_RANDOM, ARP_CHORD,
  ARP_SEQ,           // secuenciador de pasos (303-style): patrón por slot, ver abajo
  ARP_COUNT
};
#define ARP_MAX_STEPS 32   // secuencia máxima (acorde × octavas, o escala × octavas)

// --- SECUENCIADOR DE PASOS (estilo ARP_SEQ) ---
// Cada slot de arp tiene un patrón de hasta ARP_SEQ_LEN pasos. Cada paso es:
//   0..ARP_MAX_STEPS-1  -> índice de nota en la secuencia del acorde (se toma % gArpN)
//   ARP_STEP_REST       -> silencio (corta lo que sonaba)
//   ARP_STEP_TIE        -> ligadura (sostiene la nota del paso anterior, legato)
// Transponible: el mismo patrón suena distinto según el acorde apretado. La
// división/tempo salen de la config del arp (rate) + el reloj maestro.
#define ARP_SEQ_LEN   16
#define ARP_STEP_REST 254
#define ARP_STEP_TIE  255

// Índice de nota (en la secuencia ascendente) para el paso `step`. Puro/testeable.
// RANDOM y CHORD se resuelven en el trigger (no acá): devuelven step%n de fallback.
int  dspArpStepIndex(int step, int n, ArpPattern pat);

// Arranca/para el arpegiador. `notes` es la secuencia YA armada (harmonyArpSequence:
// octavas + scale-run resueltos). stepsPerBeat = subdivisiones por pulso. gate =
// largo de nota 0..1 del paso (1 = legato). El orden lo decide el estilo, NO se
// reordena la secuencia (respeta lo que entregó la armonía).
void dspArpStart(const uint8_t* notes, uint8_t n, uint8_t timbre,
                 float bpm, int stepsPerBeat, ArpPattern pat, float gate, float sr);
void dspArpStop();

// --- Config de arpegiador por slot (editable/persistente; la resuelve el .ino
// para armar la secuencia y los tiempos). rate = índice de subdivisión. ---
struct ArpCfg {
  uint8_t style;     // ArpPattern 0..ARP_COUNT-1
  uint8_t octaves;   // 1..4
  uint8_t rate;      // 0..ARP_RATES-1 (ver dspArpRateSteps/Name)
  uint8_t gate;      // 5..100 (% del paso)
  uint8_t scaleRun;  // 0/1 (recorre la escala diatónica en vez del acorde)
};
#define ARP_RATES 6                        // 1/4,1/8,1/8T,1/16,1/16T,1/32
int         dspArpRateSteps(uint8_t rate); // subdivisiones por pulso (1,2,3,4,6,8)
const char* dspArpRateName(uint8_t rate);
ArpCfg      dspArpCfgGet(int slot);
void        dspArpCfgSet(int slot, const ArpCfg& c);
// Secuencia de pasos por slot (estilo ARP_SEQ). Valores por paso: índice / REST / TIE.
void        dspArpSeqSet(int slot, const uint8_t* seq, uint8_t len);
uint8_t     dspArpSeqGet(int slot, uint8_t* seq);   // copia hasta ARP_SEQ_LEN; devuelve el largo
// Carga la secuencia ACTIVA (la que reproduce el arp). El .ino la empuja con el
// patrón del slot vigente antes de armar el arp en modo ARP_SEQ.
void        dspArpSetActiveSeq(const uint8_t* seq, uint8_t len);
int  dspArpLastNote();   // última nota disparada por el arp (-1 si ninguna); para tests

// --- Sync externo del arp (MIDI clock entrante) ---
// dspArpSetExtSync(true): el arp NO usa su reloj interno; avanza sólo cuando el
// integrador llama dspArpAdvance() en cada subdivisión del clock externo.
void dspArpAdvance();
void dspArpSetExtSync(bool on);

// Renderiza `n` muestras MONO secas en [-1, 1] (master gain + saturación tanh).
// Sin efectos: lo usan los tests para analizar el motor "limpio".
void dspRenderBlock(float* out, int n);

// Renderiza `n` muestras ESTÉREO (L≠R) con los efectos aplicados (reverb + delay).
// Es el camino que usa el firmware.
void dspRenderStereo(float* outL, float* outR, int n);

// ----------------------------------------------------------------------------
// LOOPER de 4 capas (audio). Graba la mezcla seca (post-voces, pre-efectos) en
// buffers int16 de PSRAM y suma las capas a la salida. La 1ra capa grabada fija
// el largo del loop (maestro); las demás cuantizan a ese largo. Los buffers los
// asigna el wrapper de hardware (synth.cpp) y se pasan por looperInit.
// Estados de capa: 0 EMPTY, 1 REC, 2 PLAY, 3 OVERDUB, 4 MUTED.
// ----------------------------------------------------------------------------
void  looperInit(int16_t* const bufs[LOOPER_LAYERS], uint32_t maxLen);
void  looperRecordToggle(int layer);   // EMPTY->REC->PLAY (define/loop) ; PLAY<->OVERDUB
void  looperToggleMute(int layer);     // PLAY<->MUTED
void  looperClear(int layer);          // capa -> EMPTY
void  looperClearAll();
void  looperStopAll();                 // STOP del transporte (no suena; conserva el contenido)
// Cuantización del looper al compás (opt-in, OFF por defecto). Con ella, la capa
// maestra arranca y termina en el "1" del reloj maestro -> largo = compases
// enteros y el tope del loop cae en fase con drums/arp/trémolo.
void  dspLooperQuantize(bool on);
bool  dspLooperQuantizeOn();

// ----------------------------------------------------------------------------
// PERCUSIÓN sintetizada (one-shot): bombo (kick) y caja (snare). Se mezclan en
// la salida seca (los captura el looper). dspDrumTick() avanza una muestra.
// ----------------------------------------------------------------------------
void  dspTriggerKick();
void  dspTriggerSnare();
void  dspTriggerHat();                 // hi-hat cerrado (ruido pasa-altos, ~45 ms)
float dspDrumTick();                   // 1 muestra de la percusión (mono)
// Patrón de batería sincronizado al BPM. pattern 0=off, 1..3 = patrón.
void  dspDrumsSet(int pattern, float bpm);
// Paso sonando de la grilla (0..15) o -1 si está apagada (indicador del OLED).
int   dspDrumsStepNow();
// Golpe EN VIVO de un cuerpo (0=bombo 1=caja 2=hi-hat). record=true con el
// patrón sonando: lo escribe cuantizado al paso más cercano (overdub TR).
void  dspDrumLiveHit(int inst, bool record);
// METRÓNOMO: click sync BPM (blip de seno, acento en el 1). Independiente del
// patrón de batería (guía para grabar loops sin drums). Suena por el bus de
// batería (escala con dspDrumsSetGain).
void  dspMetroSet(bool on, float bpm);
bool  dspMetro();
// Volumen general de la batería (0..1); escala todos los golpes en la mezcla.
void  dspDrumsSetGain(float g);
float dspDrumsGain();
// Volúmenes independientes: voces del synth (acorde+melodía) y sub-bajo (0..2).
void  dspSetSynthGain(float g);
float dspSynthGain();
void  dspSetBassGain(float g);
float dspBassGain();
// Motor de la batería: true = SAMPLES (drum_samples.h, PCM en flash, por
// defecto), false = sintetizada (osciladores/ruido). Cambiar corta lo que suena.
void  dspDrumsSetEngine(bool samples);
bool  dspDrumsEngine();
float looperTick(float src);           // 1 muestra: graba `src`, devuelve la suma de capas (soft-clip)
int   looperLayerState(int layer);     // estado actual (para UI/tests)
uint32_t looperMasterLen();            // largo del loop maestro en muestras (0 = sin loop)
uint32_t looperPos();                  // posición global actual del loop (sync visual en la UI)

// --- Delay editable + sincronizable ---
// Parámetros directos (timeMs limitado por el buffer ~400 ms).
void  dspDelaySet(float timeMs, float feedback, float mix);
// Sincroniza el tiempo a un tempo: timeMs = (60000/bpm) * beatFraction
// (1/4=1.0, 1/8=0.5, 1/8.=0.75, 1/16=0.25). Conserva feedback y mezcla.
void  dspDelaySyncBpm(float bpm, float beatFraction);
float dspDelayTimeMs();   // tiempo actual en ms (para UI/tests)

// --- Reloj maestro (transporte) ---
// Fase única de compás (4/4) de la que DERIVAN su tiempo la batería, el arp y el
// trémolo -> todos comparten el "1" (downbeat) y caen en fase. dspClockSet lo
// llama el .ino en cada cambio de BPM (fuente única de tempo, aunque no haya
// batería/arp activos). dspClockReset vuelve al "1".
void     dspClockSet(float bpm);
void     dspClockReset();
float    dspClockBpm();
uint32_t dspClockPos();          // muestras desde el "1" (para UI/tests)
uint32_t dspClockBarSamples();   // muestras por compás (0 = sin fijar)
// Disciplina la fase del reloj maestro a un clock MIDI externo (96 ticks/compás
// 4/4). El .ino lo llama por pulso mientras esclava; tick 0 = el "1".
void     dspClockSyncTicks(uint32_t tickInBar);

// Render estéreo SECO (sin efectos): expone el ancho estéreo de las cuerdas
// para los tests (decorrelación L/R).
void dspRenderStereoDry(float* outL, float* outR, int n);

// Utilidades (para tests y UI).
const char* dspTimbreName(uint8_t t);
int         dspActiveVoices();

// --- Osciladores expuestos para el test de anti-aliasing (T1.1) ---
// ph = fase 0..1 ; dt = incremento de fase por muestra (freq / SAMPLE_RATE).
// Las versiones *Blep aplican PolyBLEP para limpiar el alias en agudos.
float dspOscSawNaive   (float ph);
float dspOscSawBlep    (float ph, float dt);
float dspOscSquareNaive(float ph);
float dspOscSquareBlep (float ph, float dt);
// Pulso de ancho `width` (0..1) band-limited, centrado (sin DC). width=0.5 = cuadrada.
float dspOscPulseBlep  (float ph, float dt, float width);
