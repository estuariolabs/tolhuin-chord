// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   synth.cpp  -  Wrapper de hardware: I2S (ESP_I2S) + tarea FreeRTOS (núcleo 0)
   + cola de eventos. Toda la síntesis vive en dsp.cpp (puro). Acá sólo:
     - convertimos el buffer float del DSP a PCM 16-bit estéreo,
     - lo escribimos al I2S (que pacea el render al DMA),
     - encolamos note-on/off de forma thread-safe (loop en núcleo 1).
   ============================================================================ */
#include <Arduino.h>
#include <ESP_I2S.h>
#include "config.h"
#include "synth.h"
#include "dsp.h"

static I2SClass i2s;
static bool     ready = false;

// type: 0=off 1=on 2=alloff 3=strum 4=arp 5=arp-stop. vel/delay sólo en note-on.
// aux: GRUPO de voz (VoiceGroup) en on/off — a qué capa pertenece la nota.
struct Evt { uint8_t type; uint8_t note; uint8_t timbre; uint8_t vel; uint16_t delay; uint8_t aux; };
static QueueHandle_t evtQ;

static volatile uint8_t curTimbre = T_BRASS;

// Estado de STRUM (rasgueo).
static volatile bool  strumOn = false;
static volatile float strumMs = 35.0f;
static uint8_t        gStrumSet[MAX_VOICES];   // set a rasguear/arpegiar (escrito antes de encolar)
static uint8_t        gStrumN = 0;

// Estado del ARPEGIADOR.
static volatile bool  arpOn = false;
static volatile float arpBpm = 120.0f;
static volatile int   arpSteps = 2;
static volatile int   arpPat = 0;
static volatile float arpGate = 1.0f;
static uint8_t        gArpSeq[ARP_MAX_STEPS];   // secuencia armada (octavas/scale-run)
static volatile uint8_t gArpSeqN = 0;

// Estado MONO / GLIDE.
static volatile bool  monoOn = false;
static volatile bool  glideOn = false;
static volatile bool  sustainOn = false;
static volatile bool  tremOn = false;
static volatile float gTrmRate = 0.0f, gTrmDepth = 0.0f;
static volatile bool  chorusOn = false;
static volatile float gChoRate = 0.6f, gChoDepth = 6.0f, gChoMix = 0.4f;
static volatile bool  bodyOn = false;
static volatile float gBodyAmt = 0.6f;
static volatile int   drumPat = 0;
static volatile float gDrumBpm = 120.0f;
static volatile float gDrumGainStage = 1.0f;   // staging del volumen de batería (evento 34)
static volatile float gClkBpmStage   = 120.0f; // staging del BPM del reloj maestro (evento 39)
static volatile float gSynthGainStage = 1.0f;  // staging del volumen de synth  (evento 41)
static volatile float gBassGainStage  = 1.0f;  // staging del volumen de sub-bajo (evento 42)

// Parámetros de DELAY (escritos antes de encolar el evento que los aplica).
static volatile float gDlyTime = 250.0f, gDlyFb = 0.35f, gDlyMix = 0.30f;
static volatile float gDlySyncBpm = 120.0f, gDlySyncFrac = 0.5f;

// Staging del web-config: un EnvCfg POR TIMBRE (el evento 30 lleva el índice).
// Con un único buffer compartido, la carga de NVS al boot (13 applies en un
// loop) pisaba el staging antes de que la tarea de audio drenara: sólo el
// último timbre recibía sus valores guardados -> "los presets se borraban".
static EnvCfg gEnvStage[T_COUNT];

// Parámetros del reverb editable (mismo patrón de staging que el delay).
static volatile float gRevRoom = 0.42f, gRevDamp = 0.55f, gRevMix = 0.14f;

// Set de notas sostenidas (para el diff de conducción de voces).
static uint8_t held[MAX_VOICES];
static uint8_t heldCount = 0;

// SUB-BAJO: voz de bajo dedicada (raíz -1 octava) con su PROPIO timbre, rastreada
// aparte del acorde. Es la clave del "bass role" del HiChord (medido: da el sub
// que la síntesis pura no tenía). Un sine acá domina el registro grave.
static uint8_t gBassNote = 0, gBassTimbre = 0;
static bool    gBassOn = false;

static float   bufL[AUDIO_BLOCK];             // buffer L del DSP
static float   bufR[AUDIO_BLOCK];             // buffer R del DSP
static int16_t pcmBuf[AUDIO_BLOCK * 2];       // estéreo intercalado L,R,...

static volatile float gOutPeak = 0.0f;        // pico del último bloque (para el VU)

// Forma de onda para el osciloscopio (anillo de 128 muestras mono, decimado 1/4).
// 256 muestras (~23 ms decimado 1/4): margen para que el OLED busque un cruce
// por cero (trigger) y dibuje 128 px con la onda QUIETA en vez de corriendo.
static volatile float    gScope[256];
static volatile uint16_t gScopeW = 0;         // índice de escritura (= muestra más vieja)

static inline uint32_t hrand();               // (definida más abajo; la usa drainEvents)
static float gMetBpm = 120.0f;                // bpm del metrónomo (param del evt 36)
// Máscaras de batería stageadas (32 bits no entran en ev.delay): evt 31 las lee.
static uint32_t gDrumMaskStage[DRUM_PATTERNS][3];
// Secuenciador de pasos (ARP_SEQ): staging por slot (evt 43) y de la secuencia
// ACTIVA (evt 44) -> los 16 bytes no entran en el evento, van por estos buffers.
static uint8_t gArpSeqStg[ARP_SLOTS][ARP_SEQ_LEN];
static uint8_t gArpSeqStgLen[ARP_SLOTS];
static uint8_t gArpActStg[ARP_SEQ_LEN];
static uint8_t gArpActStgLen = 0;

static void drainEvents() {
  Evt ev;
  while (xQueueReceive(evtQ, &ev, 0) == pdTRUE) {
    if      (ev.type == 1) dspNoteOn(ev.note, ev.timbre, ev.vel / 255.0f, (int)ev.delay,
                                     (ev.aux == VG_ANY) ? (uint8_t)VG_CHORD : ev.aux);
    else if (ev.type == 0) dspNoteOff(ev.note, ev.aux);
    else if (ev.type == 2) dspAllOff();
    else if (ev.type == 3) dspStrumChord(gStrumSet, gStrumN, curTimbre, strumMs, (float)SAMPLE_RATE);
    else if (ev.type == 4) dspArpStart(gArpSeq, gArpSeqN, curTimbre, arpBpm, arpSteps,
                                       (ArpPattern)arpPat, arpGate, (float)SAMPLE_RATE);
    else if (ev.type == 5) dspArpStop();
    else if (ev.type == 6) dspMonoNote(ev.note, ev.timbre);
    else if (ev.type == 7) dspMonoOff();
    else if (ev.type == 8) dspSetGlide(ev.vel != 0, (float)ev.delay);
    else if (ev.type == 9) dspGlideChord(gStrumSet, gStrumN, curTimbre);
    else if (ev.type == 12) dspDelaySet(gDlyTime, gDlyFb, gDlyMix);
    else if (ev.type == 13) dspDelaySyncBpm(gDlySyncBpm, gDlySyncFrac);
    else if (ev.type == 14) dspSetSustain(ev.vel != 0, (float)ev.delay);
    else if (ev.type == 15) dspSetTremolo(tremOn, gTrmRate, gTrmDepth);
    else if (ev.type == 16) dspSetChorus(chorusOn, gChoRate, gChoDepth, gChoMix);
    else if (ev.type == 17) { curTimbre = (uint8_t)(hrand() % T_COUNT); dspRandomizeSound(curTimbre, hrand()); }
    else if (ev.type == 18) dspResetSound(ev.note);   // restaura el timbre ev.note a fábrica
    else if (ev.type == 19) dspSetBody(bodyOn, gBodyAmt);
    else if (ev.type == 20) dspArpAdvance();
    else if (ev.type == 21) dspArpSetExtSync(ev.vel != 0);
    else if (ev.type == 22) looperRecordToggle(ev.note);
    else if (ev.type == 23) looperToggleMute(ev.note);
    else if (ev.type == 24) looperClear(ev.note);
    else if (ev.type == 25) looperClearAll();
    else if (ev.type == 26) looperStopAll();
    else if (ev.type == 27) dspTriggerKick();
    else if (ev.type == 28) dspTriggerSnare();
    else if (ev.type == 29) dspDrumsSet(drumPat, gDrumBpm);
    else if (ev.type == 30) dspEnvSet(ev.note, gEnvStage[ev.note % T_COUNT]);  // timbre
    else if (ev.type == 31) dspDrumPatternSetInst(ev.note, ev.timbre,   // web-config: patrón
                                                  gDrumMaskStage[ev.note % DRUM_PATTERNS][ev.timbre % 3]);
    else if (ev.type == 32) dspSetReverb(gRevRoom, gRevDamp, gRevMix);  // web-config: reverb
    else if (ev.type == 33) dspTriggerHat();
    else if (ev.type == 34) dspDrumsSetGain(gDrumGainStage);            // volumen de batería
    else if (ev.type == 35) dspDrumsSetEngine(ev.vel != 0);             // motor synth/samples
    else if (ev.type == 36) dspMetroSet(ev.vel != 0, gMetBpm);          // metrónomo on/off + bpm
    else if (ev.type == 37) dspDrumLiveHit(ev.note, ev.vel != 0);       // golpe en vivo (+grabar)
    else if (ev.type == 38) dspDrumPatternCfgSet(ev.note, ev.timbre, ev.vel);  // grilla: bars+div
    else if (ev.type == 39) dspClockSet(gClkBpmStage);                  // BPM del reloj maestro
    else if (ev.type == 40) dspClockSyncTicks(ev.delay);               // fase del reloj = tick externo
    else if (ev.type == 41) dspSetSynthGain(gSynthGainStage);           // volumen de synth
    else if (ev.type == 42) dspSetBassGain(gBassGainStage);             // volumen de sub-bajo
    else if (ev.type == 43 && ev.note < ARP_SLOTS)
                            dspArpSeqSet(ev.note, gArpSeqStg[ev.note], gArpSeqStgLen[ev.note]);  // patrón del slot
    else if (ev.type == 44) dspArpSetActiveSeq(gArpActStg, gArpActStgLen);   // secuencia activa
  }
}

// Diagnóstico de CPU del audio: peor bloque (µs) y bloques que superaron el
// presupuesto (256/44100 = ~5805 µs). Un "late" == riesgo real de underrun.
static volatile uint32_t gPerfMaxUs = 0;
static volatile uint32_t gPerfLate  = 0;
static volatile uint32_t gSampleCount = 0;   // muestras rendereadas (reloj musical)
static const uint32_t    BLOCK_BUDGET_US = (uint32_t)(1000000.0f * AUDIO_BLOCK / SAMPLE_RATE);

// Tarea de audio en el núcleo 0. i2s.write() bloquea en el DMA -> pacea el render.
static void audioTask(void*) {
  for (;;) {
    uint32_t tr0 = (uint32_t)esp_timer_get_time();
    drainEvents();
    dspRenderStereo(bufL, bufR, AUDIO_BLOCK);     // estéreo real (delay L≠R)
    float peak = 0.0f;
    for (int i = 0; i < AUDIO_BLOCK; i++) {
      float l = bufL[i]; if (l > 1) l = 1; if (l < -1) l = -1;
      float r = bufR[i]; if (r > 1) r = 1; if (r < -1) r = -1;
      float a = (l < 0 ? -l : l), b = (r < 0 ? -r : r);
      if (a > peak) peak = a; if (b > peak) peak = b;
      pcmBuf[2*i]   = (int16_t)(l * 32767.0f);    // canal L
      pcmBuf[2*i+1] = (int16_t)(r * 32767.0f);    // canal R
    }
    gOutPeak = peak;                              // lo lee la UI (núcleo 1) para el VU
    // Vuelca la onda al anillo (decimado 1/4 -> ~11 ms en 128 muestras).
    for (int i = 0; i < AUDIO_BLOCK; i += 4) {
      gScope[gScopeW & 255] = (bufL[i] + bufR[i]) * 0.5f;
      gScopeW++;
    }
    gSampleCount += AUDIO_BLOCK;                  // reloj de muestras (looper de eventos)
    // Medición ANTES del write (el write bloquea en el DMA: eso es paceo, no costo).
    uint32_t dt = (uint32_t)esp_timer_get_time() - tr0;
    if (dt > gPerfMaxUs) gPerfMaxUs = dt;
    if (dt > BLOCK_BUDGET_US) gPerfLate++;
    i2s.write((uint8_t*)pcmBuf, sizeof(pcmBuf));
  }
}

// Muestras rendereadas desde el arranque. Es el MISMO reloj que pacea a la
// batería (el render), así que todo lo derivado de acá queda en fase con ella.
uint32_t synthSamplePos() { return gSampleCount; }

void synthPerfStats(uint32_t* maxUs, uint32_t* lateBlocks, bool reset) {
  if (maxUs)      *maxUs = gPerfMaxUs;
  if (lateBlocks) *lateBlocks = gPerfLate;
  if (reset)      gPerfMaxUs = 0;      // el contador de lates es acumulativo
}

static inline void sendEvt(uint8_t type, uint8_t note, uint8_t t,
                           uint8_t vel = 255, uint16_t delay = 0,
                           uint8_t aux = VG_ANY) {
  Evt ev{ type, note, t, vel, delay, aux }; xQueueSend(evtQ, &ev, 0);
}

// RNG chico para humanización (no necesita ser fuerte).
static inline uint32_t hrand() {
  static uint32_t s = 0x2545F491u;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

// Buffers del looper en PSRAM (los asigna synthInit; el motor dsp los usa).
static int16_t* gLpBuf[LOOPER_LAYERS] = { nullptr };

// ----------------------------------------------------------------------------
bool synthInit() {
  dspInit();
  heldCount = 0;
  // 96: la carga de NVS al boot encola ~50 eventos de una (13 timbres + 11
  // patrones x 3 instrumentos + gain/engine); con 64 quedaba poco margen.
  evtQ = xQueueCreate(96, sizeof(Evt));

  // Looper: 4 capas de LOOPER_MAX_SECONDS en PSRAM (int16 mono). Si no hay PSRAM
  // suficiente, el looper queda inactivo (el resto del firmware funciona igual).
  uint32_t lpMax = (uint32_t)LOOPER_MAX_SECONDS * SAMPLE_RATE;
  bool lpOk = true;
  for (int i = 0; i < LOOPER_LAYERS; i++) {
    gLpBuf[i] = (int16_t*)ps_malloc((size_t)lpMax * sizeof(int16_t));
    if (!gLpBuf[i]) lpOk = false;
  }
  if (lpOk) {
    looperInit(gLpBuf, lpMax);
    Serial.printf("[synth] looper: %d capas x %d s en PSRAM (%u KB c/u)\n",
                  LOOPER_LAYERS, LOOPER_MAX_SECONDS, (unsigned)(lpMax * 2 / 1024));
  } else {
    Serial.println("[synth] looper OFF: sin PSRAM suficiente");
  }

  i2s.setPins(PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT, -1, -1);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[synth] ERROR i2s.begin -> revisar pines/cableado PCM5102");
    return false;
  }
  // Prioridad 22: por ENCIMA del host NimBLE (~21) y debajo del controlador BT
  // (~23), que comparten el núcleo 0. Con prioridad 2 el audio era desalojado por
  // las ráfagas BLE (p. ej. los note-off MIDI al soltar un acorde) -> underrun ->
  // ruido. El write() al DMA bloquea, así que el resto del core sigue respirando.
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, NULL, 22, NULL, 0);  // núcleo 0
  ready = true;
  Serial.println("[synth] motor propio listo (DSP puro + ESP_I2S, núcleo 0)");
  return true;
}

void synthAutotest() {
  Serial.println("[synth] autotest BRASS / EPIANO / STRINGS");
  for (uint8_t t = 0; t < T_COUNT; t++) {
    Serial.printf("[synth]   %s\n", dspTimbreName(t));
    sendEvt(1,60,t); sendEvt(1,64,t); sendEvt(1,67,t);
    delay(t == T_STRINGS ? 1800 : 1100);
    sendEvt(0,60,0); sendEvt(0,64,0); sendEvt(0,67,0);
    delay(350);
  }
}

// Al elegir un timbre con nombre lo restauramos a fábrica (deshace un Random previo).
void synthSetTimbre(uint8_t t)         { t %= T_COUNT; sendEvt(18, t, 0); curTimbre = t; }
void synthNextTimbre()                 { uint8_t t = (curTimbre + 1) % T_COUNT; sendEvt(18, t, 0); curTimbre = t; }
uint8_t synthTimbre()                  { return curTimbre; }
const char* synthTimbreName(uint8_t t) { return dspTimbreName(t); }

// CHORUS estéreo (engorda/ensancha). rate ~0.5..1 Hz, depth ~5..10 ms, mix ~0.3..0.5.
void synthSetChorus(bool on, float rateHz, float depthMs, float mix) {
  chorusOn = on; gChoRate = rateHz; gChoDepth = depthMs; gChoMix = mix;
  sendEvt(16, 0, 0);
}
bool synthChorus() { return chorusOn; }

// Generate Random Sound: elige un timbre al azar y randomiza sus parámetros.
void synthRandomizeSound() { sendEvt(17, 0, 0); }

// --- Edición de timbres / patrones (web-config por USB) ---
void synthEnvApply(uint8_t t, const EnvCfg& e) {
  t %= T_COUNT;
  gEnvStage[t] = e;          // slot propio: N applies seguidos no se pisan
  sendEvt(30, t, 0);
}
void synthEnvGet(uint8_t t, EnvCfg* out)     { if (out) *out = dspEnvGet(t); }
void synthEnvFactory(uint8_t t, EnvCfg* out) { if (out) *out = dspEnvFactory(t); }
// Las máscaras ahora son de 32 bits (no entran en ev.delay): van STAGEADAS en
// gDrumMaskStage (definida arriba) y el evento 31 sólo lleva (pat, inst).
void synthDrumPatternSet(int pat, uint32_t kick, uint32_t snare, uint32_t hat) {
  if (pat < 0 || pat >= DRUM_PATTERNS) return;
  gDrumMaskStage[pat][0] = kick;
  gDrumMaskStage[pat][1] = snare;
  gDrumMaskStage[pat][2] = hat;
  for (uint8_t i = 0; i < 3; i++) sendEvt(31, (uint8_t)pat, i);
}
void synthDrumPatternGet(int pat, uint32_t* kick, uint32_t* snare, uint32_t* hat) {
  dspDrumPatternGet(pat, kick, snare, hat);
}
// Grilla del patrón (compases + subdivisión): evt 38 = (pat, bars, div).
void synthDrumPatternCfgSet(int pat, uint8_t bars, uint8_t div) {
  sendEvt(38, (uint8_t)pat, bars, div);
}
void synthDrumPatternCfgGet(int pat, uint8_t* bars, uint8_t* div) {
  dspDrumPatternCfgGet(pat, bars, div);
}
void synthDrumsGridInfo(int* stepsPerBar, int* total) { dspDrumsGridInfo(stepsPerBar, total); }

void synthSetReverb(float room, float damp, float mix) {
  gRevRoom = room; gRevDamp = damp; gRevMix = mix;
  sendEvt(32, 0, 0);
}

// BODY: capa de cuerpo/unísono (detune+drift+transitorio) para dar organicidad.
void synthSetBody(bool on, float amt) {
  bodyOn = on; gBodyAmt = amt;
  sendEvt(19, 0, 0);
}
bool synthBody() { return bodyOn; }

// --- LOOPER (control thread-safe vía cola; la lectura de estado es directa) ---
void synthLooperRecord(int layer)  { sendEvt(22, (uint8_t)layer, 0); }
void synthLooperMute(int layer)    { sendEvt(23, (uint8_t)layer, 0); }
void synthLooperClear(int layer)   { sendEvt(24, (uint8_t)layer, 0); }
void synthLooperClearAll()         { sendEvt(25, 0, 0); }
void synthLooperStop()             { sendEvt(26, 0, 0); }   // STOP del transporte
int  synthLooperState(int layer)   { return looperLayerState(layer); }   // para la UI
// Progreso del loop 0..127 para la barra del OLED (0 si no hay loop maestro).
int  synthLooperProgress() {
  uint32_t len = looperMasterLen();
  return len ? (int)((uint64_t)looperPos() * 128u / len) : 0;
}

// --- Percusión (dispara un golpe; thread-safe vía cola) ---
void synthKick()  { sendEvt(27, 0, 0); }
void synthSnare() { sendEvt(28, 0, 0); }
void synthHat()   { sendEvt(33, 0, 0); }

// Patrón de batería sincronizado al BPM. pattern 0=off, 1..3 = patrón.
void synthDrumsSet(int pattern, float bpm) { drumPat = pattern; gDrumBpm = bpm; sendEvt(29, 0, 0); }
bool synthDrums() { return drumPat > 0; }

// Volumen general de la batería (0..1). El get lee directo del DSP (benigno).
void  synthDrumsSetGain(float g) { gDrumGainStage = g; sendEvt(34, 0, 0); }
float synthDrumsGain()           { return dspDrumsGain(); }
// Volúmenes de synth y sub-bajo. El getter devuelve el valor STAGEADO (inmediato,
// sin esperar a que el evento drene) -> el menú puede leer-modificar-escribir bien.
void  synthSetSynthGain(float g) { gSynthGainStage = g; sendEvt(41, 0, 0); }
float synthSynthGain()           { return gSynthGainStage; }
void  synthSetBassGain(float g)  { gBassGainStage = g;  sendEvt(42, 0, 0); }
float synthBassGain()            { return gBassGainStage; }

// Motor de la batería: true = samples PCM de flash, false = sintetizada.
void synthDrumsSetEngine(bool samples) { sendEvt(35, 0, 0, (uint8_t)(samples ? 1 : 0)); }
int  synthDrumsStepNow()               { return dspDrumsStepNow(); }   // lectura simple (UI)

// Metrónomo (click sync BPM). El bpm viaja por el static gMetBpm (arriba).
void synthMetroSet(bool on, float bpm) { gMetBpm = bpm; sendEvt(36, 0, 0, (uint8_t)(on ? 1 : 0)); }

// Reloj maestro: fija el tempo del transporte (drums/arp/trémolo/looper derivan
// su fase de él). El .ino lo llama en cada cambio de BPM -> fuente única de tempo.
void synthClockSet(float bpm) { gClkBpmStage = bpm; sendEvt(39, 0, 0); }

// Disciplina la FASE del reloj maestro a un clock MIDI externo (tickInBar 0..95,
// 96 ticks/compás 4/4). El .ino lo llama una vez por pulso mientras esclava.
void synthClockSyncTicks(uint32_t tickInBar) { sendEvt(40, 0, 0, 255, (uint16_t)(tickInBar % 96)); }

// Golpe de batería en vivo desde los switches (canal DRUMS). record=grabar al patrón.
void synthDrumHit(int inst, bool record) { sendEvt(37, (uint8_t)inst, 0, (uint8_t)(record ? 1 : 0)); }
bool synthDrumsEngine()                { return dspDrumsEngine(); }

void synthSetStrum(bool on, float ms) { strumOn = on; if (ms > 0) strumMs = ms; }
bool synthStrum()                     { return strumOn; }

void synthSetArp(bool on, float bpm, int stepsPerBeat, int pattern, float gate) {
  if (bpm > 0) arpBpm = bpm;
  if (stepsPerBeat > 0) arpSteps = stepsPerBeat;
  arpPat = pattern;
  if (gate > 0.0f) arpGate = gate;
  arpOn = on;
  if (!on) sendEvt(5, 0, 0);   // parar el arp en la tarea de audio
}
bool synthArp() { return arpOn; }

// Manda la SECUENCIA ya armada (harmonyArpSequence: octavas + scale-run) al arp.
void synthArpPlay(const uint8_t* seq, uint8_t n) {
  if (!ready) return;
  if (n > ARP_MAX_STEPS) n = ARP_MAX_STEPS;
  for (uint8_t i = 0; i < n; i++) gArpSeq[i] = seq[i];
  gArpSeqN = n;
  sendEvt(4, 0, 0);
}

// Config de arp y nombres: son datos de CONFIG que sólo lee el loop (núcleo 1)
// para armar la secuencia; la tarea de audio no los toca -> acceso directo seguro.
ArpCfg      synthArpCfgGet(int s)                 { return dspArpCfgGet(s); }
void        synthArpCfgSet(int s, const ArpCfg& c){ dspArpCfgSet(s, c); }
// Secuenciador de pasos (ARP_SEQ): patrón por slot + secuencia ACTIVA.
uint8_t     synthArpSeqGet(int s, uint8_t* seq)   { return dspArpSeqGet(s, seq); }   // lectura directa (core1)
void        synthArpSeqSet(int s, const uint8_t* seq, uint8_t len) {
  if (s < 0 || s >= ARP_SLOTS || !seq) return;
  if (len > ARP_SEQ_LEN) len = ARP_SEQ_LEN;
  for (uint8_t i = 0; i < len; i++) gArpSeqStg[s][i] = seq[i];
  gArpSeqStgLen[s] = len;
  sendEvt(43, (uint8_t)s, 0);
}
void        synthArpSetActiveSeq(const uint8_t* seq, uint8_t len) {
  if (!seq) return;
  if (len > ARP_SEQ_LEN) len = ARP_SEQ_LEN;
  for (uint8_t i = 0; i < len; i++) gArpActStg[i] = seq[i];
  gArpActStgLen = len;
  sendEvt(44, 0, 0);
}
const char* synthPresetName(int k, int s)         { return dspPresetNameGet(k, s); }
void        synthPresetNameSet(int k, int s, const char* n) { dspPresetNameSet(k, s, n); }

// Sync externo del arp a un clock MIDI entrante (lo gobierna el .ino con los ticks).
void synthArpExtSync(bool on) { sendEvt(21, 0, 0, (uint8_t)(on ? 1 : 0)); }
void synthArpAdvance()        { sendEvt(20, 0, 0); }   // avanza un paso (en un tick externo)

void synthSetMono(bool on) {
  monoOn = on;
  sendEvt(2, 0, 0);          // allOff al cambiar de modo (limpia voces colgadas)
  // El allOff mata las VOCES pero held[]/gBassOn son de ESTA capa: si quedan
  // rancios, el próximo synthPlaySet cree que las notas "ya suenan" y no
  // dispara nada (el loop quedaba sonando a notas sueltas al togglear MONO).
  heldCount = 0;
  gBassOn = false;
}
bool synthMono() { return monoOn; }

void synthMonoNote(uint8_t note) {
  if (!ready) return;
  sendEvt(6, note, curTimbre);
}

void synthSetGlide(bool on, float ms) {
  glideOn = on;
  uint16_t d = (ms < 1) ? 1 : (ms > 2000 ? 2000 : (uint16_t)ms);
  sendEvt(8, 0, 0, (uint8_t)(on ? 1 : 0), d);   // vel=on, delay=ms
}
bool synthGlide() { return glideOn; }

void synthSetSustain(bool on, float ms) {
  sustainOn = on;
  uint16_t d = (ms < 1) ? 1 : (ms > 8000 ? 8000 : (uint16_t)ms);
  sendEvt(14, 0, 0, (uint8_t)(on ? 1 : 0), d);   // vel=on, delay=relMs
}
bool synthSustain() { return sustainOn; }

void synthSetTremolo(bool on, float rateHz, float depth) {
  tremOn = on; gTrmRate = rateHz; gTrmDepth = depth;
  sendEvt(15, 0, 0);                              // aplica con los params en los statics
}
bool synthTremolo() { return tremOn; }

void synthDelaySet(float timeMs, float feedback, float mix) {
  gDlyTime = timeMs; gDlyFb = feedback; gDlyMix = mix;
  sendEvt(12, 0, 0);
}
void synthDelaySyncBpm(float bpm, float beatFraction) {
  gDlySyncBpm = bpm; gDlySyncFrac = beatFraction;
  sendEvt(13, 0, 0);
}

// `timbre` opcional (255 = el activo): el LOOPER pasa el timbre CON EL QUE SE
// GRABÓ el loop, así cambiar TIMBRE en vivo no re-pinta los acordes que suenan.
// (Sólo el camino plano lo respeta; strum/glide/arp usan el timbre activo.)
void synthPlaySet(const uint8_t* notes, uint8_t count, uint8_t timbre) {
  if (!ready) return;
  if (count > MAX_VOICES) count = MAX_VOICES;
  uint8_t tt = (timbre < T_COUNT) ? timbre : curTimbre;

  // ARPEGIADOR: manda el acorde crudo al arp (sin octavas/scale-run; ese camino
  // enriquecido lo arma el .ino con synthArpPlay). Sirve para audiciones simples.
  if (arpOn) {
    synthArpPlay(notes, count);
    heldCount = 0;              // el arp maneja sus propias notas (gArpHeld en dsp)
    return;
  }

  // STRUM: re-dispara todo el acorde rasgueado (sin diff de voice leading).
  if (strumOn) {
    for (uint8_t i = 0; i < heldCount; i++) sendEvt(0, held[i], 0, 255, 0, VG_CHORD);
    for (uint8_t j = 0; j < count; j++) gStrumSet[j] = notes[j];
    gStrumN = count;
    sendEvt(3, 0, 0);                                                 // dispara el rasgueo
    for (uint8_t j = 0; j < count; j++) held[j] = notes[j];
    heldCount = count;
    return;
  }

  // GLIDE poly: las voces que ya suenan se deslizan al acorde nuevo (legato).
  if (glideOn) {
    for (uint8_t j = 0; j < count; j++) gStrumSet[j] = notes[j];
    gStrumN = count;
    sendEvt(9, 0, 0);
    for (uint8_t j = 0; j < count; j++) held[j] = notes[j];
    heldCount = count;
    return;
  }

  // Apaga las notas previas que ya no están en el set nuevo.
  for (uint8_t i = 0; i < heldCount; i++) {
    bool keep = false;
    for (uint8_t j = 0; j < count; j++) if (notes[j] == held[i]) { keep = true; break; }
    if (!keep) sendEvt(0, held[i], 0, 255, 0, VG_CHORD);   // apaga SOLO la capa acorde
  }
  // Dispara sólo las notas nuevas (las comunes se sostienen). Humanización: leve
  // variación de amplitud (vel) y micro-retardo de arranque por nota -> toque natural.
  for (uint8_t j = 0; j < count; j++) {
    bool already = false;
    for (uint8_t i = 0; i < heldCount; i++) if (held[i] == notes[j]) { already = true; break; }
    if (!already) {
      uint8_t  vel   = (uint8_t)(224 + (hrand() % 32));        // 224..255 (~0.88..1.0)
      uint16_t delay = (uint16_t)(hrand() % (SAMPLE_RATE / 200 + 1)); // 0..~5 ms
      sendEvt(1, notes[j], tt, vel, delay, VG_CHORD);
    }
  }
  for (uint8_t j = 0; j < count; j++) held[j] = notes[j];
  heldCount = count;
}

// Notas sueltas (MIDI externo, jingle, melodía del looper): capa MELODÍA —
// el glide poliacorde no las toca y un acorde con la misma altura no las mata.
void synthNoteOn(uint8_t note, uint8_t vel) {
  if (!ready) return;
  if (vel == 0) { sendEvt(0, note, 0, 255, 0, VG_MEL); return; }
  uint8_t v = (vel > 127) ? 255 : (uint8_t)(vel * 2);   // 0..127 -> 0..254
  sendEvt(1, note, curTimbre, v, 0, VG_MEL);
}
// Variante con TIMBRE explícito: la melodía del looper suena con el timbre
// capturado al grabarla, no con el activo.
void synthNoteOnT(uint8_t note, uint8_t vel, uint8_t timbre) {
  if (!ready) return;
  uint8_t v = (vel > 127) ? 255 : (uint8_t)(vel * 2);
  sendEvt(1, note, timbre % T_COUNT, v, 0, VG_MEL);
}
void synthNoteOff(uint8_t note) {
  if (!ready) return;
  sendEvt(0, note, 0, 255, 0, VG_MEL);
}

float synthGetLevel() { return gOutPeak; }

void synthGetScope(float* dst, int n) {
  if (n > 256) n = 256;
  uint16_t w = gScopeW;                // la más vieja está en la posición de escritura
  for (int i = 0; i < n; i++) dst[i] = gScope[(uint16_t)(w + i) & 255];
}

void synthReleaseAll() {
  for (uint8_t i = 0; i < heldCount; i++) sendEvt(0, held[i], 0, 255, 0, VG_CHORD);
  heldCount = 0;
  if (gBassOn) { sendEvt(0, gBassNote, 0, 255, 0, VG_BASS); gBassOn = false; }
  if (arpOn)  sendEvt(5, 0, 0);   // soltar = silencio: también frena el arp (modo sigue activo)
  if (monoOn) sendEvt(7, 0, 0);   // y suelta la voz mono
}

// Suelta SOLO el acorde + sub-bajo (y frena el arp), CONSERVANDO la voz mono.
// Lo usa el looper: sus eventos OFF no deben cortar la melodía que el usuario
// está tocando en MONO por encima del loop.
void synthReleaseChord() {
  for (uint8_t i = 0; i < heldCount; i++) sendEvt(0, held[i], 0, 255, 0, VG_CHORD);
  heldCount = 0;
  if (gBassOn) { sendEvt(0, gBassNote, 0, 255, 0, VG_BASS); gBassOn = false; }
  if (arpOn) sendEvt(5, 0, 0);
}

// Suelta SOLO la voz mono (la melodía en vivo), sin tocar el acorde del loop.
void synthMonoOff() { sendEvt(7, 0, 0); }

// Sub-bajo dedicado: nota `note` (<0 = apagar) con el timbre `timbre`. Sólo
// re-dispara si cambió (conducción suave: la nota grave se sostiene entre acordes
// que comparten raíz). Se apaga con synthReleaseAll.
void synthSetBass(int note, uint8_t timbre) {
  if (!ready) return;
  if (note < 0 || note > 127) {
    if (gBassOn) { sendEvt(0, gBassNote, 0, 255, 0, VG_BASS); gBassOn = false; }
    return;
  }
  if (gBassOn && gBassNote == (uint8_t)note && gBassTimbre == timbre) return;  // sin cambio
  if (gBassOn) sendEvt(0, gBassNote, 0, 255, 0, VG_BASS);   // corta el bajo anterior
  sendEvt(1, (uint8_t)note, timbre, 255, 0, VG_BASS);       // capa BAJO, nivel pleno
  gBassNote = (uint8_t)note; gBassTimbre = timbre; gBassOn = true;
}
