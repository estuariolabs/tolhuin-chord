// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   dsp.cpp  -  Núcleo de síntesis PURO (sin hardware). Compila en host y ESP32.
   Portado tal cual del motor MVP (ESP_I2S) pero SIN I2S ni FreeRTOS:
     - pasa-bajos de 1 polo por voz, corte distinto por timbre
     - vibrato global sutil (cuerdas + algo de epiano; brass firme)
     - EPIANO con piso de brillo FM en el sostenido (no se vuelve flauta)
   La asignación de voces (note-on/off, robo) vive acá; la cola de eventos y
   el I2S viven en synth.cpp (wrapper de hardware).
   ============================================================================ */
#include <math.h>
#include "dsp.h"

// Arduino compila con -Os (tamaño); este archivo es el camino caliente del audio
// y con -Os los timbres PolyBLEP (STRINGS: 3 saws + 2 filtros por voz; BRASS:
// 4 flancos BLEP) no entraban en el presupuesto de bloque (~5.8 ms) con muchas
// voces -> underrun (ruido al soltar/cambiar acordes). -O2 sólo acá.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

// En el ESP32 el código vive en flash y la caché la comparten los dos cores:
// las ráfagas BLE la desalojan y el render se frena por stalls de SPI justo en
// el peor momento. El render top-level va a IRAM (DSP_HOT); en host es un no-op
// (regla dura: dsp.cpp compila con g++ de PC, sin headers de Arduino).
#ifdef ESP_PLATFORM
#  include <esp_attr.h>
#  define DSP_HOT IRAM_ATTR
#else
#  define DSP_HOT
#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// OJO: tiene que haber EXACTAMENTE T_COUNT nombres. Un inicializador de menos
// deja NULLs silenciosos y el printf("%s", NULL) del #hello REINICIA la placa
// (pasó al ampliar a 6 USR). El test de host verifica nombres no-nulos.
static const char* T_NAME[T_COUNT] = { "BRASS", "EPIANO", "STR", "SINE", "TRI", "ORGAN",
                                       "FLUTE", "WAVE",
                                       "USR1", "USR2", "USR3", "USR4", "USR5", "USR6" };

// ----------------------------------------------------------------------------
// Una voz polifónica: fases, envolvente de amplitud (ADSR) y de FM, LPF.
// ----------------------------------------------------------------------------
struct Voice {
  bool    active = false;
  uint8_t note   = 0;
  uint8_t timbre = 0;     // indice en ENV (params/envolventes)
  uint8_t osc    = 0;     // FAMILIA de oscilador que renderiza (= timbre, salvo USR)
  uint8_t group  = 0;     // CAPA dueña de la voz (VoiceGroup: acorde/melodía/bajo)
  float   ph0=0, ph1=0, ph2=0;     // fases 0..1
  float   inc0=0, inc1=0, inc2=0;  // incremento de fase por muestra
  Adsr    amp;                     // envolvente de amplitud (sin clicks)
  Adsr    fenv;                    // envolvente de filtro (abre/cierra el cutoff)
  Svf     filt;                    // pasa-bajos por voz (canal L / mono)
  Svf     filtR;                   // pasa-bajos del canal R (sólo STRINGS, ancho estéreo)
  Lfo     lfo;                     // LFO por voz (vibrato + trémolo)
  float   lfoVal=0;                // valor del LFO en el bloque actual
  float   fmEnv=0;                 // envolvente del índice FM cuerpo (epiano)
  float   fmEnv2=0;                // envolvente del índice FM tine/ataque (epiano)
  float   ampScale=1.0f;           // ganancia de la voz (humanización/dinámica)
  int     startDelay=0;            // micro-retardo de arranque en muestras (humanización)
  float   panL=0.7071f, panR=0.7071f;  // paneo equipotencia (ancho estéreo por voz)
  float   curMidi=60, targMidi=60;     // altura actual y objetivo (para glide/portamento)
  float   glideStep=0;                 // semitonos por bloque hacia el objetivo (tiempo constante)
  float   detune=0;                    // BODY: offset fijo de afinación por voz (spread de ensamble)
  float   drift=0;                     // BODY: random walk lento de pitch (inestabilidad analógica)
  float   atkPitch=0;                  // BODY: transitorio de pitch al ataque (se asienta)
};
static Voice voices[MAX_VOICES];

// Config por timbre (struct EnvCfg pública en dsp.h; la edita el web-config).
// ENV = vivo; ENV_DEFAULT = preset del usuario (lo que restauran dspResetSound
// y el cambio de timbre); ENV_FACTORY = fábrica inmutable (para "reset" web).
static EnvCfg ENV[T_COUNT];
static EnvCfg ENV_DEFAULT[T_COUNT];
static EnvCfg ENV_FACTORY[T_COUNT];

// ----------------------------------------------------------------------------
// Implementación de Adsr (declarada en dsp.h).
// ----------------------------------------------------------------------------
void Adsr::config(float atkMs, float decMs, float susLevel, float relMs, bool percussive, float sr) {
  atkInc  = 1.0f / (atkMs * sr / 1000.0f);
  decInc  = 1.0f / (decMs * sr / 1000.0f);
  relInc  = 1.0f / (relMs * sr / 1000.0f);
  sustain = susLevel;
  perc    = percussive;
}
void Adsr::noteOn()  { stage = ST_ATK; }      // arranca desde el nivel actual -> sin click
void Adsr::noteOff() { if (stage != ST_IDLE) stage = ST_REL; }
void Adsr::setReleaseMs(float relMs, float sr) {   // sustain dinámico: cambia sólo el release
  if (relMs < 1.0f) relMs = 1.0f;
  relInc = 1.0f / (relMs * sr / 1000.0f);
}
float Adsr::process() {
  switch (stage) {
    case ST_ATK: level += atkInc; if (level >= 1.0f) { level = 1.0f; stage = ST_DEC; } break;
    case ST_DEC:
      level -= decInc;
      if (perc) { if (level <= 0.0f) { level = 0.0f; stage = ST_IDLE; } }
      else if (level <= sustain) { level = sustain; stage = ST_SUS; }
      break;
    case ST_SUS: break;
    case ST_REL: level -= relInc; if (level <= 0.0f) { level = 0.0f; stage = ST_IDLE; } break;
    default:  level = 0.0f; break;
  }
  return level;
}

// ----------------------------------------------------------------------------
// Implementación de Svf (filtro state-variable TPT, declarado en dsp.h).
// ----------------------------------------------------------------------------
void Svf::setCoeffs(float cutoffHz, float Q, float sr) {
  if (cutoffHz < 20.0f)          cutoffHz = 20.0f;
  if (cutoffHz > sr * 0.45f)     cutoffHz = sr * 0.45f;   // margen bajo Nyquist
  if (Q < 0.5f)                  Q = 0.5f;
  float g = tanf(PI * cutoffHz / sr);
  k  = 1.0f / Q;
  a1 = 1.0f / (1.0f + g * (g + k));
  a2 = g * a1;
  a3 = g * a2;
}
float Svf::lp(float in) {
  float v3 = in - ic2eq;
  float v1 = a1 * ic1eq + a2 * v3;
  float v2 = ic2eq + a2 * ic1eq + a3 * v3;
  ic1eq = 2.0f * v1 - ic1eq;
  ic2eq = 2.0f * v2 - ic2eq;
  return v2;   // salida pasa-bajos
}

// ----------------------------------------------------------------------------
// Implementación de Lfo (declarado en dsp.h).
// ----------------------------------------------------------------------------
float Lfo::process(float dt) {
  phase += rate * dt;
  phase -= floorf(phase);          // envuelve a 0..1
  return sinf(2.0f * PI * phase);
}

// ----------------------------------------------------------------------------
// Implementación de DelayStereo (declarado en dsp.h).
// Canal derecho con 3/4 del tiempo del izquierdo -> L≠R (amplitud estéreo).
// ----------------------------------------------------------------------------
void DelayStereo::setParams(float timeMs, float feedback, float mixAmt, float sr) {
  int d = (int)(timeMs * sr / 1000.0f);
  if (d < 1) d = 1; if (d > MAXLEN - 1) d = MAXLEN - 1;
  dL = d;
  int dr = (int)(d * 3 / 4); if (dr < 1) dr = 1;
  dR = dr;
  fb  = feedback; if (fb < 0) fb = 0; if (fb > 0.95f) fb = 0.95f;   // estable
  mix = mixAmt;   if (mix < 0) mix = 0; if (mix > 1) mix = 1;
}
void DelayStereo::setTime(float timeMs, float sr) {
  int d = (int)(timeMs * sr / 1000.0f);
  if (d < 1) d = 1; if (d > MAXLEN - 1) d = MAXLEN - 1;
  dL = d;
  int dr = (int)(d * 3 / 4); if (dr < 1) dr = 1;
  dR = dr;
}
void DelayStereo::process(float inMono, float& outL, float& outR) {
  int rL = widx - dL; if (rL < 0) rL += MAXLEN;
  int rR = widx - dR; if (rR < 0) rR += MAXLEN;
  float dl = bufL[rL];
  float dr = bufR[rR];
  bufL[widx] = inMono + dl * fb;          // realimentación que decae
  bufR[widx] = inMono + dr * fb;
  if (++widx >= MAXLEN) widx = 0;
  outL = inMono * (1.0f - mix) + dl * mix;
  outR = inMono * (1.0f - mix) + dr * mix;
}
void DelayStereo::processWet(float inMono, float& wetL, float& wetR) {
  int rL = widx - dL; if (rL < 0) rL += MAXLEN;
  int rR = widx - dR; if (rR < 0) rR += MAXLEN;
  float dl = bufL[rL];
  float dr = bufR[rR];
  bufL[widx] = inMono + dl * fb;          // realimentación que decae
  bufR[widx] = inMono + dr * fb;
  if (++widx >= MAXLEN) widx = 0;
  wetL = dl * mix;                         // sólo el eco (send)
  wetR = dr * mix;
}
void DelayStereo::reset() {
  for (int i = 0; i < MAXLEN; i++) { bufL[i] = 0.0f; bufR[i] = 0.0f; }
  widx = 0;
}

// ----------------------------------------------------------------------------
// Implementación de Reverb (Freeverb-lite, declarado en dsp.h).
// ----------------------------------------------------------------------------
void Reverb::setParams(float roomSize, float damping, float mixAmt) {
  if (roomSize < 0) roomSize = 0; if (roomSize > 1) roomSize = 1;
  if (damping  < 0) damping  = 0; if (damping  > 1) damping  = 1;
  fb   = 0.70f + roomSize * 0.28f;   // 0.70..0.98 (estable)
  damp = damping * 0.4f;
  mix  = mixAmt; if (mix < 0) mix = 0; if (mix > 1) mix = 1;
}
// Un comb amortiguado: y = buf; lowpass(y) realimentado a la entrada.
static inline float combTick(float* buf, int len, int& idx, float& store,
                             float in, float fb, float damp) {
  float y = buf[idx];
  store = y * (1.0f - damp) + store * damp;   // amortiguamiento de agudos
  buf[idx] = in + store * fb;
  if (++idx >= len) idx = 0;
  return y;
}
// Un allpass de Schroeder (g = 0.5).
static inline float allpassTick(float* buf, int len, int& idx, float in) {
  float bufout = buf[idx];
  float out = -in + bufout;
  buf[idx] = in + bufout * 0.5f;
  if (++idx >= len) idx = 0;
  return out;
}
float Reverb::process(float in) {
  float x = in * 0.18f;            // ganancia de entrada (evita saturar 4 combs)
  float out = combTick(c0, C0, i0, s0, x, fb, damp)
            + combTick(c1, C1, i1, s1, x, fb, damp)
            + combTick(c2, C2, i2, s2, x, fb, damp)
            + combTick(c3, C3, i3, s3, x, fb, damp);
  out = allpassTick(ap0, A0, ai0, out);
  out = allpassTick(ap1, A1, ai1, out);
  return out * mix;               // cola húmeda escalada
}
void Reverb::reset() {
  for (int i = 0; i < C0; i++) c0[i] = 0; for (int i = 0; i < C1; i++) c1[i] = 0;
  for (int i = 0; i < C2; i++) c2[i] = 0; for (int i = 0; i < C3; i++) c3[i] = 0;
  for (int i = 0; i < A0; i++) ap0[i] = 0; for (int i = 0; i < A1; i++) ap1[i] = 0;
  i0 = i1 = i2 = i3 = 0; ai0 = ai1 = 0; s0 = s1 = s2 = s3 = 0;
}

static float sineTab[1024];
static float fluteTab[1024];   // FLUTE precomputada (suma de armónicos H1..H5) -> 1 lookup por muestra
static float organTab[1024];   // ORGAN precomputado (4 drawbars) -> 1 lookup por muestra
// WAVE: banco de 4 frames (oscuro -> brillante) para el morph. El render hace
// 2 lookups + crossfade por muestra; la posición la barre la env de filtro.
// 512 muestras por frame (no 1024): con <=12 armónicos sobra resolución y los
// 8 KB que ahorra evitan desbordar la DRAM (quedaba a 368 bytes del tope).
#define WT_LEN 512
static float waveTab[4][WT_LEN];

static DelayStereo gDelay;   // delay estéreo global (efecto de salida)
static Reverb      gReverb;  // reverb global (cola difusa antes del delay)
static Chorus      gChorus;  // chorus estéreo global (engrosa, antes de reverb/delay)
static bool        gChorusOn = false;
static bool        gBodyOn = false;     // capa BODY (cuerpo/unísono por offsets de pitch)
static float       gBodyAmt = 0.0f;     // intensidad 0..1

// --- Estado del LOOPER de 4 capas (buffers en PSRAM, asignados por synth.cpp) ---
enum { LP_EMPTY = 0, LP_REC, LP_PLAY, LP_OVERDUB, LP_MUTED };
struct LpLayer { int16_t* buf = nullptr; uint32_t len = 0, pos = 0, recLeft = 0; int state = LP_EMPTY; float gain = 0.85f; };
static LpLayer  gLp[LOOPER_LAYERS];
static uint32_t gLpMax = 0;             // capacidad por capa (muestras)
static uint32_t gLpMaster = 0;          // largo del loop maestro (0 = todavía nada grabado)
static uint32_t gLpPos = 0;             // posición GLOBAL del loop (todas las capas en fase)
static bool     gLpRun = true;          // transporte: false = STOP (no suena, congelado)
static bool     gLpQuantize = false;    // opt-in: cuantiza el largo del loop a compases del reloj maestro
static bool     gLpRecPend  = false;    // grabación de la MAESTRA armada, espera el próximo downbeat
static bool     gLpStopPend = false;    // fin de la MAESTRA armado, espera el downbeat (largo = compases enteros)
static int      gLpRecLayer = 0;        // capa que se está grabando como maestra (para el diferido)
static inline int16_t lpClip16(int v) { return (v > 32767) ? 32767 : (v < -32768 ? -32768 : (int16_t)v); }

// --- Percusión (bombo + caja + hi-hat), one-shot. Dos motores:
//     SYNTH (osciladores/ruido) o SAMPLES (drum_samples.h, PCM en flash). ---
#include "drum_samples.h"
struct KickV  { bool on=false; float ph=0, amp=0, pitch=0; };
struct SnareV { bool on=false; float ph1=0, ph2=0, amp=0, tone=0; };
struct HatV   { bool on=false; float amp=0, lp=0; };     // hi-hat: ruido pasa-altos
static KickV  gKick;
static SnareV gSnare;
static HatV   gHat;
// Reproductor de samples: posición fraccional + interpolación lineal. `step` =
// DS_RATE/SAMPLE_RATE (los WAV están a 22050 -> 0.5). Retrigger = choke (pos=0).
struct SampleV { const int16_t* d=nullptr; uint32_t len=0; float pos=0, gain=1; bool on=false; };
static SampleV gSmp[3];                                  // 0=kick 1=snare 2=hat
static float   gSmpStep = 0.5f;                          // se calcula en dspInit
static bool    gDrumSamples = true;                      // motor: true=samples, false=synth
static float  gKAmpDec, gKPitDec, gSAmpDec, gSToneDec;   // coeficientes de decaimiento (dspInit)
static float  gHAmpDec, gHatHpK;                         // hi-hat: decay (~45 ms) + coef del HPF
static float  gDrumGain = 1.0f;                          // volumen general de la batería (0..1)
static float  gSynthGain = 1.0f;                         // volumen de las voces (acorde + melodía)
static float  gBassGain  = 1.0f;                         // volumen del sub-bajo dedicado (VG_BASS)
static bool   gDrumsOn = false;                          // patrón de batería sincronizado al BPM
static int    gDrumPat = 0;                              // 0=off, 1..3 = patrón
static int    gDrumStepSamples = 1;         // largo del paso en muestras (para la escritura en vivo)
static int    gDrumStep = -1;               // paso vigente del patrón (derivado del reloj maestro; -1 = ninguno)
// Patrones de batería. Cada patrón tiene su GRILLA propia: `bars` compases
// (1..2) × 4 pulsos × `div` pasos por pulso (4 = binaria/semicorcheas,
// 3 = ternaria/tresillos) -> hasta 32 pasos, máscaras uint32_t (bit s = golpe
// en el paso s). Los de fábrica son 1 compás binario (16 pasos, como antes):
//  1 básico (kick 1,3 / snare 2,4) · 2 four-on-floor · 3 con síncopa de bombo.
static uint32_t DRUM_KICK[DRUM_PATTERNS]  = { 0x0000, 0x0101, 0x1111, 0x1141, 0, 0, 0, 0 };
static uint32_t DRUM_SNARE[DRUM_PATTERNS] = { 0x0000, 0x1010, 0x1010, 0x1010, 0, 0, 0, 0 };
static uint32_t DRUM_HAT[DRUM_PATTERNS]   = { 0x0000, 0x4444, 0x4444, 0x4444, 0, 0, 0, 0 };
static uint8_t  DRUM_BARS[DRUM_PATTERNS];   // compases del patrón (1..2), dspInit -> 1
static uint8_t  DRUM_DIV[DRUM_PATTERNS];    // pasos por pulso: 4 binaria / 3 ternaria
static int      gDrumTotal = 16;            // pasos totales del patrón ACTIVO
static float    gDrumBpmCur = 120.0f;       // último BPM aplicado (recalcular al editar cfg)

// METRÓNOMO: click sintetizado (blip de seno con decay ~12 ms) en cada pulso,
// acento más agudo y fuerte en el 1 del compás. Corre con su propio contador de
// muestras (independiente del patrón de batería: sirve para grabar loops sin
// drums). Suena por el bus de batería (dspDrumTick).
static bool  gMetOn = false;
static int   gMetSpb = 1, gMetCounter = 1, gMetBeat = 3;   // spb = samples por pulso
static float gMetEnv = 0.0f, gMetPhase = 0.0f, gMetInc = 0.0f, gMetDecay = 0.999f;

// Notas pendientes del rasgueo (STRUM): se disparan cuando su cuenta llega a 0.
struct PendNote { uint8_t note, timbre; int samplesLeft; bool armed; };
static PendNote gPending[MAX_CHORD_NOTES];

static void clearPending() {
  for (auto& p : gPending) { p.armed = false; p.samplesLeft = 0; }
}

// Estado del arpegiador.
static bool       gArpOn = false;
static bool       gArpExtSync = false;   // arp gobernado por clock MIDI externo (no reloj interno)
static int        gArpStepSamples = 1, gArpStep = 0;   // largo del paso (para el gate) + índice de patrón
static int        gArpGridSteps = 0, gArpGrid = -1;    // pasos de la grilla por compás + índice vigente (reloj maestro)
static uint8_t    gArpNotes[ARP_MAX_STEPS], gArpN = 0, gArpTimbre = 0;
static ArpPattern gArpPat = ARP_UP;
static float      gArpGate = 1.0f;                 // largo de nota 0..1 del paso
static int        gArpGateLeft = 0;                // muestras hasta el note-off (gate)
static uint8_t    gArpHeld[ARP_MAX_STEPS];         // notas sonando AHORA por el arp
static uint8_t    gArpHeldN = 0;
static int        gArpLast = -1;                   // última nota (compat/tests)

// --- Config de arp por slot + tabla de subdivisiones + nombres de presets ---
static const int   ARP_RATE_STEPS[ARP_RATES] = { 1, 2, 3, 4, 6, 8 };  // por pulso
static const char* ARP_RATE_NAME [ARP_RATES] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
static ArpCfg gArpSlot[ARP_SLOTS];
static char   gDrumName[DRUM_PATTERNS][PRESET_NAME_LEN];
static char   gArpName [ARP_SLOTS][PRESET_NAME_LEN];
// Secuenciador de pasos (ARP_SEQ): patrón por slot + la secuencia ACTIVA que
// reproduce el arp (copiada del slot vigente al armar) + su posición.
static uint8_t gArpSeqSlot[ARP_SLOTS][ARP_SEQ_LEN];
static uint8_t gArpSeqSlotLen[ARP_SLOTS];
static uint8_t gArpSeqActive[ARP_SEQ_LEN];
static uint8_t gArpSeqActiveLen = 0;
static uint8_t gArpSeqPos = 0;

int         dspArpRateSteps(uint8_t r) { return ARP_RATE_STEPS[r % ARP_RATES]; }
const char* dspArpRateName (uint8_t r) { return ARP_RATE_NAME [r % ARP_RATES]; }
ArpCfg      dspArpCfgGet(int s)        { return gArpSlot[(s < 0 || s >= ARP_SLOTS) ? 0 : s]; }
void        dspArpCfgSet(int s, const ArpCfg& c) {
  if (s < 0 || s >= ARP_SLOTS) return;
  ArpCfg v = c;
  if (v.style >= ARP_COUNT) v.style = ARP_UP;
  if (v.octaves < 1) v.octaves = 1; if (v.octaves > 4) v.octaves = 4;
  if (v.rate >= ARP_RATES) v.rate = 1;
  if (v.gate < 5)  v.gate = 5; if (v.gate > 100) v.gate = 100;
  v.scaleRun = v.scaleRun ? 1 : 0;
  gArpSlot[s] = v;
}
// --- Secuencia de pasos por slot (ARP_SEQ) ---
void dspArpSeqSet(int s, const uint8_t* seq, uint8_t len) {
  if (s < 0 || s >= ARP_SLOTS || !seq) return;
  if (len > ARP_SEQ_LEN) len = ARP_SEQ_LEN;
  for (uint8_t i = 0; i < len; i++) gArpSeqSlot[s][i] = seq[i];
  gArpSeqSlotLen[s] = (len < 1) ? 1 : len;
}
uint8_t dspArpSeqGet(int s, uint8_t* seq) {
  if (s < 0 || s >= ARP_SLOTS || !seq) return 0;
  uint8_t len = gArpSeqSlotLen[s];
  for (uint8_t i = 0; i < len; i++) seq[i] = gArpSeqSlot[s][i];
  return len;
}
void dspArpSetActiveSeq(const uint8_t* seq, uint8_t len) {
  if (!seq) return;
  if (len > ARP_SEQ_LEN) len = ARP_SEQ_LEN;
  for (uint8_t i = 0; i < len; i++) gArpSeqActive[i] = seq[i];
  gArpSeqActiveLen = (len < 1) ? 1 : len;
  if (gArpSeqPos >= gArpSeqActiveLen) gArpSeqPos = 0;
}
void dspPresetNameSet(int kind, int slot, const char* name) {
  char* dst = nullptr;
  if      (kind == 0 && slot >= 0 && slot < DRUM_PATTERNS) dst = gDrumName[slot];
  else if (kind == 1 && slot >= 0 && slot < ARP_SLOTS)     dst = gArpName[slot];
  if (!dst || !name) return;
  int i = 0;
  for (; name[i] && i < PRESET_NAME_LEN - 1; i++) dst[i] = name[i];
  dst[i] = 0;
}
const char* dspPresetNameGet(int kind, int slot) {
  if (kind == 0 && slot >= 0 && slot < DRUM_PATTERNS) return gDrumName[slot];
  if (kind == 1 && slot >= 0 && slot < ARP_SLOTS)     return gArpName[slot];
  return "";
}

// ----------------------------------------------------------------------------
static bool  gGlideOn = false;
static int   gGlideBlocks = 1;             // bloques que dura el glide (TIEMPO constante)
static int   gMonoVoice = -1;               // voz dedicada al modo monofónico (-1 = ninguna)
static bool  gSustainOn = false;            // sustain conmutable: release largo al soltar
static float gSustainRelMs = 1500.0f;       // release usado cuando el sustain está activo
static bool  gTremOn = false;               // tremolo global de salida (sincronizable a BPM)
static float gTremPhase = 0.0f;             // fase 0..1 del LFO de tremolo
static float gTremInc = 0.0f;               // incremento de fase por muestra (rate/SR)
static float gTremDepth = 0.0f;             // profundidad 0..1

// --- RELOJ MAESTRO (transporte musical) -------------------------------------
// Un único contador de fase, en muestras, define la grilla del compás (4/4).
// Batería, arpegiador y trémolo NO llevan fase propia: DERIVAN su posición de
// este reloj, así comparten el "1" (downbeat) y caen en fase entre sí. El looper
// (opt-in) cuantiza su largo a compases y alinea el tope del loop al downbeat.
// gClkBar cuenta compases (para patrones de batería de 2 compases: la fase de la
// FRASE = gClkBar % bars, así el patrón largo también queda anclado al reloj).
static float    gClkBpm      = 120.0f;   // tempo del reloj maestro
static uint32_t gClkBarSamps = 0;        // muestras por compás 4/4 (0 = sin fijar aún)
static uint32_t gClkPos      = 0;        // posición dentro del compás [0, gClkBarSamps)
static uint32_t gClkBar      = 0;        // compás actual (monotónico; para frases multi-compás)
static bool     gClkRun      = true;     // transporte del reloj (avanza cada bloque)
static bool     gClkDownbeat = false;    // true en el bloque que cruza el "1" (lo usan trem/looper)

// Recalcula las muestras por compás a partir del BPM, conservando la fase actual.
static void clockSetBpm(float bpm) {
  if (bpm < 20.0f)  bpm = 20.0f;
  if (bpm > 400.0f) bpm = 400.0f;
  gClkBpm = bpm;
  gClkBarSamps = (uint32_t)(60.0f / bpm * (float)SAMPLE_RATE * 4.0f);   // 4 pulsos por compás
  if (gClkBarSamps < 1) gClkBarSamps = 1;
  if (gClkPos >= gClkBarSamps) gClkPos %= gClkBarSamps;
}

// Suelta una voz (amp + filtro) respetando el sustain global: si está activo,
// alarga el release para que la nota resuene en vez de cortarse.
static void voiceRelease(Voice& v) {
  if (gSustainOn) {
    v.amp.setReleaseMs(gSustainRelMs, (float)SAMPLE_RATE);
    v.fenv.setReleaseMs(gSustainRelMs, (float)SAMPLE_RATE);
  }
  v.amp.noteOff();
  v.fenv.noteOff();
}

static inline float msInc(float ms)        { return 1.0f / (ms * SAMPLE_RATE / 1000.0f); }
static inline float midiToFreq(uint8_t n)  { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }
static inline float midiToFreqF(float m)   { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
static inline float fastSine(float ph)    { ph -= floorf(ph); return sineTab[((int)(ph*1024.0f)) & 1023]; }
static inline float lpCoef(float fc)      { return 1.0f - expf(-2.0f * PI * fc / SAMPLE_RATE); }
// Ruido blanco chico (aliento de la flauta). xorshift, no necesita ser fuerte.
static inline float dspNoise() {
  static uint32_t s = 0x9E3779B9u;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return (float)((int32_t)s) * (1.0f / 2147483648.0f);   // -1..1
}

// ----------------------------------------------------------------------------
// PolyBLEP: corrección polinómica de los saltos (band-limited step) para
// matar el alias en agudos. `t` es la fase 0..1, `dt` el incremento por muestra.
// Devuelve el residuo a sumar/restar cerca de la discontinuidad.
// ----------------------------------------------------------------------------
static inline float polyBlep(float t, float dt) {
  if (t < dt) {                 // justo después del salto
    t /= dt;
    return t + t - t * t - 1.0f;
  } else if (t > 1.0f - dt) {   // justo antes del salto
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

// --- Osciladores (naive y band-limited) ---
float dspOscSawNaive(float ph) { return 2.0f * ph - 1.0f; }

float dspOscSawBlep(float ph, float dt) {
  // Diente de sierra ascendente: salto descendente de -2 al envolver 1->0.
  return (2.0f * ph - 1.0f) - polyBlep(ph, dt);
}

float dspOscSquareNaive(float ph) { return (ph < 0.5f) ? 1.0f : -1.0f; }

float dspOscSquareBlep(float ph, float dt) {
  // Cuadrada: flanco de subida en ph=0 (+2) y de bajada en ph=0.5 (-2).
  float s = (ph < 0.5f) ? 1.0f : -1.0f;
  s += polyBlep(ph, dt);
  float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
  s -= polyBlep(ph2, dt);
  return s;
}

float dspOscPulseBlep(float ph, float dt, float width) {
  // Pulso de ancho `width`: flanco de subida en ph=0, de bajada en ph=width.
  // Se resta la componente de continua (2*width-1) para que quede centrado.
  float s = (ph < width) ? 1.0f : -1.0f;
  s += polyBlep(ph, dt);
  float ph2 = ph - width; if (ph2 < 0.0f) ph2 += 1.0f;
  s -= polyBlep(ph2, dt);
  return s - (2.0f * width - 1.0f);
}

// ----------------------------------------------------------------------------
// Chorus estéreo (implementación; declarado en dsp.h).
// ----------------------------------------------------------------------------
// Lectura fraccional de un buffer circular: interpola entre dos muestras.
static inline float chFracRead(const float* buf, int len, int widx, float delay) {
  float rp = (float)widx - delay;
  while (rp < 0.0f) rp += (float)len;
  int i0 = (int)rp; float fr = rp - (float)i0;
  int i1 = i0 + 1; if (i1 >= len) i1 -= len;
  return buf[i0] + (buf[i1] - buf[i0]) * fr;
}
void Chorus::setParams(float rateHz, float depthMs, float mixAmt, float sr) {
  if (rateHz < 0.01f) rateHz = 0.01f;
  lfoInc = rateHz / sr;
  depthS = depthMs * 0.001f * sr;
  baseS  = depthS + 0.003f * sr;                 // base = profundidad + 3 ms (retardo siempre > 0)
  if (baseS + depthS >= (float)(MAXLEN - 2)) depthS = (float)(MAXLEN - 2) - baseS;
  if (mixAmt < 0.0f) mixAmt = 0.0f; if (mixAmt > 1.0f) mixAmt = 1.0f;
  mix = mixAmt;
}
void Chorus::process(float in, float& wetL, float& wetR) {
  buf[widx] = in;
  float modL = baseS + depthS * (0.5f + 0.5f * fastSine(lfoPhase));
  float modR = baseS + depthS * (0.5f + 0.5f * fastSine(lfoPhase + 0.25f));   // cuadratura -> ancho
  wetL = chFracRead(buf, MAXLEN, widx, modL) * mix;
  wetR = chFracRead(buf, MAXLEN, widx, modR) * mix;
  lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
  widx++; if (widx >= MAXLEN) widx = 0;
}
void Chorus::reset() {
  for (int i = 0; i < MAXLEN; i++) buf[i] = 0.0f;
  widx = 0; lfoPhase = 0.0f;
}

static void envInit() {
  // Amplitud:    atkMs decMs  sus    relMs    FM: fmDec      fmIdx fmFloor
  // Filtro:      cutHz  res  fEnvOct fAtkMs fDecMs fSus fRelMs  LFO: rate vib    trem    perc
  ENV[T_BRASS]   = { 6,   40,  0.85f, 120,        0,         0,    0,
                     3400, 1.3f, 1.1f, 8,   120,  0.85f, 120,      5.0f, 0.0008f, 0.00f,  false };
  // EPIANO (Rhodes FM, recalibrado): índices en unidades de FASE (0..1 = 2π).
  // Cuerpo FM 1:1: ataque 0.36 (~2.3 rad, golpe con carne) ablandándose a
  // 0.36*0.22 ≈ 0.08 (~0.5 rad, sostenido cálido, casi seno). Tine ADITIVO:
  // parcial a 5.02x (levemente inarmónico, como el martillo real) con amplitud
  // 0.30 y decay ~130 ms. Decay largo (2.2 s) para que la nota cante; filtro
  // más cerrado y sin honk (res 0.6); trémolo suave tipo suitcase; sin vibrato.
  ENV[T_EPIANO]  = { 2,   2200, 0.0f, 280,        msInc(420), 0.36f, 0.22f,
                     2400, 0.6f, 1.6f, 1,   200,  0.35f, 220,      4.8f, 0.0004f, 0.09f,  true,
                     msInc(130), 0.30f, 5.02f };
  // STRINGS = el pad insignia. CALIBRADO contra el HiChord por FFT (tools/
  // render_ref.cpp): su acorde es un saw-ensemble BRILLANTE. Filtro base
  // 4500 Hz + res 1.3 clava hi/mid=1.14 (ref 1.09) y el centroide sube de
  // 2596 a ~2434 con el sub fuerte. El registro grave lo da el SUB-BAJO sine
  // dedicado (app.subBass), no las cuerdas. Detune ±0.6% + chorus = ancho.
  ENV[T_STRINGS] = { 38,  60,  0.80f, 420,        0,         0,    0,
                     4500, 1.3f, 1.6f, 160, 400,  0.88f, 500,      5.2f, 0.0040f, 0.18f,  false };
  // --- paleta extendida (T4.1) ---
  ENV[T_SINE]    = { 20,  100, 0.80f, 300,        0,         0,    0,
                     3600, 0.7f, 1.5f, 30,  200,  0.88f, 300,      5.0f, 0.0020f, 0.00f,  false };
  ENV[T_TRIANGLE]= { 10,  80,  0.85f, 250,        0,         0,    0,
                     3900, 0.7f, 1.8f, 12,  160,  0.88f, 250,      5.0f, 0.0010f, 0.00f,  false };
  ENV[T_ORGAN]   = { 4,   20,  1.00f, 80,         0,         0,    0,
                     5400, 0.8f, 1.0f, 4,   40,   0.95f, 80,       6.0f, 0.0000f, 0.08f,  false };
  // FLUTE: ataque blando (aliento), sostenido casi pleno, vibrato suave. Filtro
  // abierto (la flauta es fundamental + cola corta), sin resonancia. Trémolo leve.
  ENV[T_FLUTE]   = { 80,  120, 0.92f, 320,        0,         0,    0,
                     2600, 0.6f, 0.8f, 80,  200,  0.90f, 320,      5.5f, 0.0035f, 0.05f,  false };
  // WAVE (pad morphing): la env de filtro (fAtk lento, fSus medio) BARRE los 4
  // frames -> el espectro se abre al atacar y se asienta en un color medio.
  // wtMorph = profundidad del barrido (1 = recorre todo el banco).
  ENV[T_WAVE]    = { 25,  180, 0.80f, 380,        0,         0,    0,
                     4200, 0.9f, 1.4f, 90,  320,  0.55f, 380,      5.0f, 0.0025f, 0.10f,  false };
  ENV[T_WAVE].wtMorph = 1.0f;
  // Familia de oscilador: los timbres base renderizan con su propio motor.
  for (int t = 0; t < T_USER1; t++) ENV[t].family = (uint8_t)t;
  // Timbres de USUARIO: nacen como copias editables de la paleta base (puntos de
  // partida musicales; la web les cambia familia y parámetros, y viajan en los
  // paquetes de sonido). Se reparten entre las familias para variar.
  static const uint8_t USER_SEED[] = { T_EPIANO, T_STRINGS, T_BRASS, T_ORGAN, T_FLUTE, T_SINE };
  for (int u = T_USER1; u < T_COUNT; u++) {
    uint8_t seed = USER_SEED[(u - T_USER1) % (int)(sizeof(USER_SEED))];
    ENV[u] = ENV[seed];
    ENV[u].family = seed;
  }

  for (int t = 0; t < T_COUNT; t++) {
    ENV_DEFAULT[t] = ENV[t];   // preset del usuario (restaurar tras random/timbre)
    ENV_FACTORY[t] = ENV[t];   // fábrica inmutable (para "reset" desde el web-config)
  }
}

void dspResetSound(uint8_t t) {
  if (t < T_COUNT) ENV[t] = ENV_DEFAULT[t];
}

// --- Edición de timbres (web-config). Set escribe vivo + preset del usuario ---
EnvCfg dspEnvGet(uint8_t t)     { return ENV[t % T_COUNT]; }
EnvCfg dspEnvFactory(uint8_t t) { return ENV_FACTORY[t % T_COUNT]; }
void   dspEnvSet(uint8_t t, const EnvCfg& e) {
  if (t >= T_COUNT) return;
  ENV[t] = e;
  ENV_DEFAULT[t] = e;
}

// --- Edición de patrones de batería (slots 1..7; el 0 es "off") ---
void dspDrumPatternSet(int pat, uint32_t kick, uint32_t snare, uint32_t hat) {
  if (pat < 1 || pat >= DRUM_PATTERNS) return;
  DRUM_KICK[pat] = kick; DRUM_SNARE[pat] = snare; DRUM_HAT[pat] = hat;
}
void dspDrumPatternGet(int pat, uint32_t* kick, uint32_t* snare, uint32_t* hat) {
  if (pat < 0 || pat >= DRUM_PATTERNS) {
    if (kick) *kick = 0; if (snare) *snare = 0; if (hat) *hat = 0;
    return;
  }
  if (kick)  *kick  = DRUM_KICK[pat];
  if (snare) *snare = DRUM_SNARE[pat];
  if (hat)   *hat   = DRUM_HAT[pat];
}
void dspDrumPatternSetInst(int pat, int inst, uint32_t mask) {
  if (pat < 1 || pat >= DRUM_PATTERNS) return;
  if      (inst == 0) DRUM_KICK[pat]  = mask;
  else if (inst == 1) DRUM_SNARE[pat] = mask;
  else if (inst == 2) DRUM_HAT[pat]   = mask;
}

// Grilla del patrón: compases (1..2) y subdivisión (4 = binaria semicorcheas,
// 3 = ternaria tresillos). Si es el patrón que está sonando, recalcula el reloj
// EN VIVO (la cuantización de los golpes sigue automáticamente esta config).
void dspDrumPatternCfgSet(int pat, uint8_t bars, uint8_t div) {
  if (pat < 1 || pat >= DRUM_PATTERNS) return;
  if (bars < 1) bars = 1; if (bars > 2) bars = 2;
  DRUM_BARS[pat] = bars;
  DRUM_DIV[pat]  = (div == 3) ? 3 : 4;
  if (gDrumsOn && gDrumPat == pat) dspDrumsSet(pat, gDrumBpmCur);
}
void dspDrumPatternCfgGet(int pat, uint8_t* bars, uint8_t* div) {
  bool okp = (pat >= 0 && pat < DRUM_PATTERNS);
  if (bars) *bars = okp ? DRUM_BARS[pat] : 1;
  if (div)  *div  = okp ? DRUM_DIV[pat]  : 4;
}
// Info de la grilla ACTIVA para la UI: pasos por compás y total.
void dspDrumsGridInfo(int* stepsPerBar, int* total) {
  int d = (gDrumPat >= 1) ? DRUM_DIV[gDrumPat] : 4;
  if (stepsPerBar) *stepsPerBar = 4 * d;
  if (total)       *total = gDrumTotal;
}

// Randomiza ENV[t] con rangos musicales (Generate Random Sound). La familia de
// oscilador la varía la capa synth eligiendo un timbre al azar antes de llamar.
void dspRandomizeSound(uint8_t t, uint32_t seed) {
  if (t >= T_COUNT) return;
  uint32_t s = seed ? seed : 0x00C0FFEEu;
  auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s >> 8) * (1.0f / 16777216.0f); };
  EnvCfg& e = ENV[t];
  e.atkMs   = 2.0f   + rnd() * 200.0f;
  e.decMs   = 30.0f  + rnd() * 400.0f;
  e.sus     = 0.30f  + rnd() * 0.65f;
  e.relMs   = 80.0f  + rnd() * 500.0f;
  e.cutHz   = 400.0f + rnd() * 5000.0f;
  e.res     = 0.6f   + rnd() * 1.2f;
  e.fEnvOct = rnd() * 3.0f;
  e.fAtkMs  = 5.0f   + rnd() * 150.0f;
  e.fDecMs  = 40.0f  + rnd() * 400.0f;
  e.fSus    = 0.30f  + rnd() * 0.60f;
  e.fRelMs  = 80.0f  + rnd() * 500.0f;
  e.lfoRate = 3.0f   + rnd() * 6.0f;
  e.vib     = rnd() * 0.006f;
  e.trem    = rnd() * 0.25f;
}

static void voiceSetFreq(Voice& v, float midi);   // (definida tras voiceStart)

// keepPhase = true conserva fase/LPF (retrigger legato sin click); false reinicia.
static void voiceStart(Voice& v, uint8_t note, uint8_t t, bool keepPhase,
                       float vel, int delaySamples) {
  const EnvCfg& e = ENV[t];
  v.active = true; v.note = note; v.timbre = t;
  // Familia de oscilador: los timbres base renderizan con su propio motor; los
  // slots de usuario despachan por ENV[t].family (clampeada a las 8 familias).
  v.osc = (t >= T_USER1) ? (uint8_t)(e.family % T_USER1) : t;
  v.ampScale = vel; v.startDelay = (delaySamples > 0) ? delaySamples : 0;
  if (!keepPhase) { v.ph0 = v.ph1 = v.ph2 = 0; v.filt.reset(); v.filtR.reset(); v.lfo.reset(); }
  v.amp.config(e.atkMs, e.decMs, e.sus, e.relMs, e.perc, (float)SAMPLE_RATE);
  v.amp.noteOn();                       // arranca ataque desde el nivel actual
  // envolvente de filtro: misma forma percusiva que la amplitud por timbre
  v.fenv.config(e.fAtkMs, e.fDecMs, e.fSus, e.fRelMs, e.perc, (float)SAMPLE_RATE);
  v.fenv.noteOn();
  v.lfo.setRate(e.lfoRate);
  v.fmEnv = 1.0f;
  v.fmEnv2 = 1.0f;
  // Paneo por nota (equipotencia): los acordes se abren en el campo estéreo.
  // Determinístico por nota (misma nota = misma posición) y disperso para
  // decorrelacionar L/R como la referencia premium.
  int h = (note * 5) % 12;                // 0..11 disperso por nota
  float pan = (h - 5.5f) / 6.0f;          // ~[-0.92, +0.92]
  float ang = (pan + 1.0f) * 0.25f * PI;  // 0..PI/2
  v.panL = cosf(ang);
  v.panR = sinf(ang);
  v.curMidi = v.targMidi = (float)note;
  // BODY: offsets de pitch por voz (cuerpo/unísono). detune fijo = spread de
  // ensamble (cada nota afina un poco distinto); atkPitch = transitorio que se
  // asienta. drift (dinámico) se arma en blockSetup.
  if (gBodyOn) {
    // Rango AUDIBLE (antes ±5 cents: imperceptible -> "BODY no hace nada").
    // ±11 cents de spread estático + blip de ataque de ~1/4 de semitono que se
    // asienta: el efecto "ensamble analógico" ahora se escucha al toggle.
    v.detune   = dspNoise() * 0.11f * gBodyAmt;     // ±11 cents * amt (estático por voz)
    v.atkPitch = 0.25f * gBodyAmt;                  // blip inicial (~se asienta en blockSetup)
  } else { v.detune = 0.0f; v.atkPitch = 0.0f; }
  v.drift = 0.0f;
  voiceSetFreq(v, v.curMidi + v.detune + v.atkPitch);
}

// Fija los incrementos de fase de la voz a partir de una altura MIDI (fraccional).
// Permite glide: al deslizar curMidi se recalculan los incrementos por bloque.
static void voiceSetFreq(Voice& v, float midi) {
  const EnvCfg& e = ENV[v.timbre];
  float f = midiToFreqF(midi);
  if (v.osc == T_STRINGS) {                    // 3 saws detuneados
    v.inc0 = f*0.994f/SAMPLE_RATE;
    v.inc1 = f       /SAMPLE_RATE;
    v.inc2 = f*1.006f/SAMPLE_RATE;
  } else if (v.osc == T_EPIANO) {              // carrier + mod cuerpo (1:1) + mod tine
    v.inc0 = f/SAMPLE_RATE;
    v.inc1 = f/SAMPLE_RATE;
    v.inc2 = e.tineRatio * f / SAMPLE_RATE;
  } else if (v.osc == T_BRASS) {               // dos pulsos detuneados
    v.inc0 = f         / SAMPLE_RATE;
    v.inc1 = f*1.0045f / SAMPLE_RATE;
  } else {                                     // SINE / TRIANGLE / ORGAN: un oscilador
    v.inc0 = f/SAMPLE_RATE;
  }
}

// Render de una voz a estéreo (L,R). STRINGS reparte sus 3 saws entre L/R para
// dar ancho; el resto de los timbres son mono (L=R).
static inline void renderVoiceStereo(Voice& v, float& outL, float& outR) {
  const EnvCfg& e = ENV[v.timbre];
  if (v.startDelay > 0) { v.startDelay--; outL = outR = 0.0f; return; }  // micro-retardo (humanización)
  float env = v.amp.process();                    // ----- envolvente ADSR (sin clicks) -----
  v.fenv.process();                               // avanza la env de filtro (cutoff a tasa de bloque)
  if (!v.amp.active()) { v.active = false; outL = outR = 0; return; }
  float sc   = 1.0f + e.vib  * v.lfoVal;          // vibrato (pitch); 0 en brass
  float trem = 1.0f + e.trem * v.lfoVal;          // trémolo (amplitud); 0 en brass/epiano

  if (v.osc == T_STRINGS) {                       // 3 saws band-limited repartidos L/R
    float d0 = v.inc0*sc, d1 = v.inc1*sc, d2 = v.inc2*sc;
    v.ph0 += d0; if (v.ph0>=1) v.ph0-=1;
    v.ph1 += d1; if (v.ph1>=1) v.ph1-=1;
    v.ph2 += d2; if (v.ph2>=1) v.ph2-=1;
    float a = dspOscSawBlep(v.ph0,d0);            // detune -0.6%  -> más a L
    float b = dspOscSawBlep(v.ph1,d1);            // central      -> centro
    float c = dspOscSawBlep(v.ph2,d2);            // detune +0.6% -> más a R
    const float NORM = 0.66f;                     // mantiene el nivel mono ~0.33*(a+b+c)
    float sL = NORM * (0.7f*a + 0.5f*b + 0.3f*c);
    float sR = NORM * (0.3f*a + 0.5f*b + 0.7f*c);
    sL = v.filt.lp(sL);                           // un filtro por canal (estados distintos)
    sR = v.filtR.lp(sR);
    float g = env * trem * v.ampScale;
    outL = sL * g;
    outR = sR * g;
    return;
  }

  float s;                                        // ----- timbres mono -----
  if (v.osc == T_EPIANO) {
    // Rhodes en dos capas. CUERPO: FM 1:1 con índice moderado que se ablanda a
    // un piso cálido (el "wah" del golpe). TINE: parcial ADITIVO a ~5x con decay
    // corto (el "ding" del martillo). OJO: fastSine trabaja en unidades de FASE
    // (0..1 = 2π), los índices FM van en esa escala — el tine viejo era un
    // modulador FM de índice ~12 rad: puro clang inarmónico (sonaba feo).
    v.fmEnv  -= e.fmDec;  if (v.fmEnv  < e.fmFloor) v.fmEnv  = e.fmFloor;
    v.fmEnv2 -= e.fmDec2; if (v.fmEnv2 < 0.0f)      v.fmEnv2 = 0.0f;
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;     // portadora
    v.ph1 += v.inc1*sc; if (v.ph1>=1) v.ph1-=1;     // modulador cuerpo (ratio 1)
    v.ph2 += v.inc2*sc; if (v.ph2>=1) v.ph2-=1;     // parcial tine (~5x, aditivo)
    float fm = (e.fmIdx * v.fmEnv) * fastSine(v.ph1);
    s = fastSine(v.ph0 + fm)
      + (e.fmIdx2 * v.fmEnv2) * fastSine(v.ph2);    // fmIdx2 = AMPLITUD del tine
  } else if (v.osc == T_SINE) {                   // seno puro
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;
    s = fastSine(v.ph0);
  } else if (v.osc == T_TRIANGLE) {               // triángulo (alias bajo, naive)
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;
    s = 1.0f - 4.0f * fabsf(v.ph0 - 0.5f);
  } else if (v.osc == T_ORGAN) {                  // órgano aditivo (drawbars) -> wavetable precomputada
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;
    s = organTab[((int)(v.ph0*1024.0f)) & 1023];   // UN lookup en vez de 4 senos -> menos CPU
  } else if (v.osc == T_FLUTE) {                  // flauta: wavetable (H1..H5 precomputados) + aliento
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;
    s = fluteTab[((int)(v.ph0*1024.0f)) & 1023];   // UN lookup en vez de 5 senos -> menos CPU
    s += dspNoise() * 0.05f;                        // aliento: ruido suave (lo filtra el SVF y lo modula la env)
  } else if (v.osc == T_WAVE) {                   // wavetable MORPHING: 4 frames + crossfade
    v.ph0 += v.inc0*sc; if (v.ph0>=1) v.ph0-=1;
    float m = e.wtMorph * v.fenv.level;            // posición del morph (la barre la env de filtro)
    if (m < 0.0f) m = 0.0f; if (m > 1.0f) m = 1.0f;
    float x = m * 3.0f;
    int   f0 = (int)x; if (f0 > 2) f0 = 2;
    float fr = x - (float)f0;
    int   ix = ((int)(v.ph0*(float)WT_LEN)) & (WT_LEN - 1);
    s = waveTab[f0][ix] + (waveTab[f0 + 1][ix] - waveTab[f0][ix]) * fr;
  } else {                                        // BRASS: dos pulsos detuneados (HiChord)
    const float PW = 0.32f;                        // ancho reedy (no 50%) = más brillo
    float d0 = v.inc0*sc, d1 = v.inc1*sc;
    v.ph0 += d0; if (v.ph0>=1) v.ph0-=1;
    v.ph1 += d1; if (v.ph1>=1) v.ph1-=1;
    s = (dspOscPulseBlep(v.ph0, d0, PW) + dspOscPulseBlep(v.ph1, d1, PW)) * 0.5f;
  }
  s = v.filt.lp(s);                               // pasa-bajos resonante de 2 polos (SVF)
  float g = s * env * trem * v.ampScale;
  outL = g * v.panL * 1.4142f;                    // paneo equipotencia (×√2 compensa el centro)
  outR = g * v.panR * 1.4142f;
}

// ----------------------------------------------------------------------------
// API pública
// ----------------------------------------------------------------------------
void dspInit() {
  for (int i = 0; i < 1024; i++) sineTab[i] = sinf(2.0f * PI * i / 1024.0f);
  // FLUTE: misma suma aditiva de antes (H1..H5) pero precomputada en una tabla,
  // así el render es UN lookup por muestra (no 5 senos) -> mucho menos CPU.
  for (int i = 0; i < 1024; i++) {
    float w = 2.0f * PI * i / 1024.0f;
    fluteTab[i] = ( sinf(w)         * 1.00f
                  + sinf(2.0f * w)  * 0.40f
                  + sinf(3.0f * w)  * 0.22f
                  + sinf(4.0f * w)  * 0.12f
                  + sinf(5.0f * w)  * 0.06f ) * 0.52f;
    organTab[i] = ( sinf(w)         * 1.00f       // fundamental
                  + sinf(2.0f * w)  * 0.50f       // octava
                  + sinf(3.0f * w)  * 0.30f       // octava + quinta
                  + sinf(4.0f * w)  * 0.20f )     // doble octava
                  * 0.50f;
  }
  // WAVE: 4 frames de brillo creciente (aditivos, <=12 armónicos: mismo criterio
  // anti-alias que ORGAN/FLUTE). El morph los recorre oscuro -> brillante.
  //   0: casi seno (fundamental + un toque de octava)      "oscuro"
  //   1: impares suaves (hueco, clarinete)                 "medio"
  //   2: serie 1/n completa (saw redondeada)               "abierto"
  //   3: 1/sqrt(n) con impares reforzados (chispa met.)    "brillante"
  for (int f = 0; f < 4; f++) {
    float peak = 0.0f;
    for (int i = 0; i < WT_LEN; i++) {
      float w = 2.0f * PI * i / WT_LEN, s = 0.0f;
      switch (f) {
        case 0: s = sinf(w) + 0.15f * sinf(2.0f * w); break;
        case 1: for (int h = 1; h <= 7;  h += 2) s += sinf(h * w) / (float)h; break;
        case 2: for (int h = 1; h <= 12; h++)    s += sinf(h * w) / (float)h; break;
        case 3: for (int h = 1; h <= 12; h++)
                  s += sinf(h * w) / sqrtf((float)h) * ((h & 1) ? 1.0f : 0.55f);
                break;
      }
      waveTab[f][i] = s;
      float a = (s < 0) ? -s : s; if (a > peak) peak = a;
    }
    // Normaliza cada frame al mismo pico: el morph cambia el COLOR, no el volumen.
    float g = (peak > 0.0f) ? 0.85f / peak : 1.0f;
    for (int i = 0; i < WT_LEN; i++) waveTab[f][i] *= g;
  }
  envInit();
  for (auto& v : voices) {
    v.active = false;
    v.amp.stage = Adsr::ST_IDLE;  v.amp.level = 0;
    v.fenv.stage = Adsr::ST_IDLE; v.fenv.level = 0;
    v.filt.reset(); v.filtR.reset();
    v.lfo.reset(); v.lfoVal = 0;
  }
  clearPending();
  gArpOn = false; gArpExtSync = false; gArpLast = -1; gArpStep = 0;
  gArpGridSteps = 0; gArpGrid = -1;
  gArpN = 0; gArpHeldN = 0; gArpGate = 1.0f; gArpGateLeft = 0; gArpPat = ARP_UP;
  gMonoVoice = -1; gGlideOn = false;
  gSustainOn = false; gSustainRelMs = 1500.0f;
  gTremOn = false; gTremPhase = 0.0f; gTremInc = 0.0f; gTremDepth = 0.0f;
  // Reloj maestro: tempo por defecto, fase en el "1". drums/arp lo re-fijan al usarse.
  gClkRun = true; gClkPos = 0; gClkBar = 0; gClkDownbeat = false; clockSetBpm(120.0f);
  // reverb + delay "dreamy": cola difusa media y eco estéreo sutil.
  gReverb.reset();
  gReverb.setParams(0.42f, 0.55f, 0.14f);         // sala chica, cola oscura, mezcla sutil (menos "eclesiástico")
  gDelay.reset();
  gDelay.setParams(250.0f, 0.35f, 0.30f, (float)SAMPLE_RATE);
  gChorus.reset();
  gChorus.setParams(0.6f, 6.0f, 0.4f, (float)SAMPLE_RATE);   // valores por defecto (off hasta activar)
  gChorusOn = false;
  gBodyOn = false; gBodyAmt = 0.0f;
  // Looper: reinicia el estado de reproducción (los buffers los mantiene looperInit).
  for (int i = 0; i < LOOPER_LAYERS; i++) { gLp[i].state = LP_EMPTY; gLp[i].len = 0; gLp[i].pos = 0; }
  gLpMaster = 0; gLpPos = 0; gLpRun = true; gLpRecPend = false; gLpStopPend = false; gLpRecLayer = 0;
  // Percusión: coeficientes de decaimiento (factor por muestra = exp(-1/(tau*sr))).
  gKAmpDec  = expf(-1.0f / (0.16f  * (float)SAMPLE_RATE));   // bombo: amplitud ~160 ms
  gKPitDec  = expf(-1.0f / (0.025f * (float)SAMPLE_RATE));   // bombo: caída de tono ~25 ms
  gSAmpDec  = expf(-1.0f / (0.095f * (float)SAMPLE_RATE));   // caja: ruido ~95 ms
  gSToneDec = expf(-1.0f / (0.055f * (float)SAMPLE_RATE));   // caja: parche tonal ~55 ms
  gHAmpDec  = expf(-1.0f / (0.045f * (float)SAMPLE_RATE));   // hi-hat: chick corto ~45 ms
  gHatHpK   = 1.0f - expf(-2.0f * PI * 6500.0f / (float)SAMPLE_RATE);  // HPF ~6.5 kHz
  // Arp: presets de fábrica (varían estilo/octavas/rate/gate/escala) + nombres.
  static const ArpCfg ARP_FACT[ARP_SLOTS] = {
    { ARP_UP,       1, 3, 80, 0 },   // clasico  (1/16)
    { ARP_UPDOWN,   2, 3, 70, 0 },   // sube-baja
    { ARP_DOWN,     1, 1, 90, 0 },   // baja     (1/8)
    { ARP_RANDOM,   2, 3, 60, 0 },   // random
    { ARP_UP,       2, 3, 80, 1 },   // escala   (scale-run)
    { ARP_CHORD,    1, 1, 90, 0 },   // acorde   (ritmo)
  };
  static const char* ARP_FACT_NAME[ARP_SLOTS] =
    { "clasico", "sube-baja", "baja", "random", "escala", "acorde" };
  for (int s = 0; s < ARP_SLOTS; s++) {
    gArpSlot[s] = ARP_FACT[s];
    dspPresetNameSet(1, s, ARP_FACT_NAME[s]);
    // Secuenciador de pasos: patrón por defecto = run ascendente de 8 pasos
    // (0..7, se toma % gArpN). Sólo suena si el slot está en estilo ARP_SEQ.
    gArpSeqSlotLen[s] = 8;
    for (int i = 0; i < ARP_SEQ_LEN; i++) gArpSeqSlot[s][i] = (uint8_t)(i & 7);
  }
  gArpSeqActiveLen = 0; gArpSeqPos = 0;
  static const char* DRUM_FACT_NAME[DRUM_PATTERNS] =
    { "off", "basico", "4floor", "sincopa", "user1", "user2", "user3", "user4",
      "user5", "user6", "user7", "user8" };
  for (int p = 0; p < DRUM_PATTERNS; p++) dspPresetNameSet(0, p, DRUM_FACT_NAME[p]);

  gKick.on = false; gSnare.on = false; gHat.on = false;
  // Samples de batería (flash): datos, ganancia por instrumento y paso de lectura.
  gSmp[0] = { DS_KICK,  DS_KICK_LEN,  0, 0.90f, false };
  gSmp[1] = { DS_SNARE, DS_SNARE_LEN, 0, 0.70f, false };
  gSmp[2] = { DS_HAT,   DS_HAT_LEN,   0, 0.50f, false };
  gSmpStep = (float)DS_RATE / (float)SAMPLE_RATE;
  gDrumSamples = true;
  gDrumGain = 1.0f; gSynthGain = 1.0f; gBassGain = 1.0f;
  gDrumsOn = false; gDrumPat = 0; gDrumStep = -1;
  // Grilla por patrón: fábrica = 1 compás binario (16 pasos, como siempre).
  for (int p = 0; p < DRUM_PATTERNS; p++) { DRUM_BARS[p] = 1; DRUM_DIV[p] = 4; }
  gDrumTotal = 16;
  // Metrónomo: decay del click ~12 ms; nace apagado.
  gMetDecay = expf(-1.0f / (0.012f * (float)SAMPLE_RATE));
  gMetOn = false; gMetEnv = 0.0f;
}

void dspNoteOn(uint8_t note, uint8_t t, float vel, int delaySamples, uint8_t group) {
  // misma nota DEL MISMO GRUPO aún sonando: retrigger legato (otra capa con la
  // misma altura NO se roba: cada toma conserva su voz).
  for (auto& v : voices)
    if (v.active && v.note==note && v.group==group && v.amp.stage!=Adsr::ST_REL) {
      voiceStart(v,note,t,true,vel,delaySamples); v.group = group; return;
    }
  // voz libre: arranque limpio (nivel ya en 0)
  for (auto& v : voices)
    if (!v.active) { voiceStart(v,note,t,false,vel,delaySamples); v.group = group; return; }
  // robo: con sustain las colas en RELEASE se acumulan; robar PRIMERO la cola más
  // silenciosa (protege las notas que están realmente sonando). Si no hay ninguna
  // en release, la voz de menor nivel. keepPhase=true -> sin reiniciar fase/filtro
  // (evita el click/crack al cambiar de acorde).
  Voice* q = nullptr;
  for (auto& v : voices)
    if (v.amp.stage == Adsr::ST_REL && (!q || v.amp.level < q->amp.level)) q = &v;
  if (!q) { q = &voices[0]; for (auto& v : voices) if (v.amp.level < q->amp.level) q = &v; }
  voiceStart(*q, note, t, true, vel, delaySamples);
  q->group = group;
}

void dspNoteOff(uint8_t note, uint8_t group) {
  // group=VG_ANY apaga sin mirar la capa; con grupo, sólo la voz de ESA capa
  // (la melodía del loop y un acorde con la misma altura ya no se matan).
  for (auto& v : voices)
    if (v.active && v.note==note && v.amp.stage!=Adsr::ST_REL &&
        (group == VG_ANY || v.group == group)) voiceRelease(v);
}

void dspAllOff() {
  for (auto& v : voices) if (v.active) voiceRelease(v);
  clearPending();
  gArpOn = false; gArpLast = -1; gArpHeldN = 0; gArpGateLeft = 0;
  gMonoVoice = -1;
}

void dspSetGlide(bool on, float ms) {
  gGlideOn = on;
  if (ms < 1.0f) ms = 1.0f;
  // TIEMPO constante: todas las voces tardan `ms` en llegar, sin importar el salto.
  gGlideBlocks = (int)(ms * 0.001f * (float)SAMPLE_RATE / (float)AUDIO_BLOCK);
  if (gGlideBlocks < 1) gGlideBlocks = 1;
}

void dspSetSustain(bool on, float relMs) {
  gSustainOn = on;
  if (relMs < 1.0f) relMs = 1.0f;
  gSustainRelMs = relMs;
}

// Reverb editable (web-config): tamaño de sala, amortiguamiento y mezcla.
// Los valores de fábrica los fija dspInit (sala chica, cola oscura, mezcla sutil).
void dspSetReverb(float roomSize, float damping, float mix) {
  gReverb.setParams(roomSize, damping, mix);
}

void dspSetChorus(bool on, float rateHz, float depthMs, float mix) {
  gChorusOn = on;
  gChorus.setParams(rateHz, depthMs, mix, (float)SAMPLE_RATE);
}

void dspSetBody(bool on, float amt) {
  gBodyOn = on;
  if (amt < 0.0f) amt = 0.0f; if (amt > 1.0f) amt = 1.0f;
  gBodyAmt = amt;
}

void dspSetTremolo(bool on, float rateHz, float depth) {
  gTremOn = on;
  if (rateHz < 0.05f) rateHz = 0.05f;
  gTremInc = rateHz / (float)SAMPLE_RATE;
  if (depth < 0.0f) depth = 0.0f; if (depth > 1.0f) depth = 1.0f;
  gTremDepth = depth;
}

// Arranca el glide de una voz hacia `target`: paso por bloque proporcional a la
// distancia / gGlideBlocks -> todas las voces llegan al mismo tiempo. Glide off
// = salto instantáneo.
static void startGlide(Voice& v, float target) {
  v.targMidi = target;
  if (gGlideOn && gGlideBlocks > 1 && v.curMidi != target) {
    v.glideStep = (target - v.curMidi) / (float)gGlideBlocks;
  } else {
    v.curMidi = target; v.glideStep = 0;
    voiceSetFreq(v, v.curMidi);
  }
}

void dspMonoNote(uint8_t note, uint8_t timbre) {
  Voice& v = voices[0];                       // voz dedicada al modo monofónico
  if (v.active && v.amp.stage != Adsr::ST_REL) {
    // legato: cambiar la altura objetivo sin re-atacar la envolvente
    v.timbre = timbre; v.note = note;
    startGlide(v, (float)note);          // legato: desliza (tiempo constante) o salta
  } else {
    voiceStart(v, note, timbre, false, 1.0f, 0);   // arranque fresco (con ataque)
  }
  v.group = VG_MEL;                            // es melodía: el glide de acordes no la toca
  gMonoVoice = 0;
}

void dspMonoOff() {
  if (gMonoVoice >= 0) voiceRelease(voices[gMonoVoice]);
  gMonoVoice = -1;
}

// Retarguetea una voz a una nota nueva (legato: conserva la envolvente; glide la
// desliza, si está apagado salta).
static void voiceRetarget(Voice& v, uint8_t note, uint8_t timbre) {
  v.note = note; v.timbre = timbre;
  startGlide(v, (float)note);            // desliza con tiempo constante (o salta si glide off)
}

void dspGlideChord(const uint8_t* notes, uint8_t n, uint8_t timbre) {
  if (n > MAX_CHORD_NOTES) n = MAX_CHORD_NOTES;
  bool usedV[MAX_VOICES] = { false };
  bool placed[MAX_CHORD_NOTES] = { false };

  // Foto de las voces que YA sonaban (candidatas a deslizar). SOLO las del
  // grupo ACORDE: la melodía del loop y el bajo pertenecen a otras capas y el
  // glide no debe re-asignarlas ni liberarlas (se "chocaban" entre tomas).
  // Las voces frescas del paso 3 NO están acá, así que el paso 4 no las apaga.
  bool wasGlidable[MAX_VOICES];
  for (uint8_t k = 0; k < MAX_VOICES; k++)
    wasGlidable[k] = voices[k].active && voices[k].amp.stage != Adsr::ST_REL &&
                     voices[k].group == VG_CHORD;

  // 1) tonos comunes exactos: la voz que ya está en esa altura se queda (no se mueve).
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t k = 0; k < MAX_VOICES; k++) {
      if (usedV[k] || !wasGlidable[k]) continue;
      if ((int)(voices[k].targMidi + 0.5f) == (int)notes[i]) {
        voiceRetarget(voices[k], notes[i], timbre);
        usedV[k] = true; placed[i] = true; break;
      }
    }
  }
  // 2) resto: a la voz activa más cercana (desliza la menor distancia).
  for (uint8_t i = 0; i < n; i++) {
    if (placed[i]) continue;
    int best = -1, bestd = 1 << 30;
    for (uint8_t k = 0; k < MAX_VOICES; k++) {
      if (usedV[k] || !wasGlidable[k]) continue;
      int d = (int)(voices[k].targMidi + 0.5f) - (int)notes[i]; if (d < 0) d = -d;
      if (d < bestd) { bestd = d; best = k; }
    }
    if (best >= 0) { voiceRetarget(voices[best], notes[i], timbre); usedV[best] = true; placed[i] = true; }
  }
  // 3) notas sin voz disponible: arranque fresco.
  for (uint8_t i = 0; i < n; i++) if (!placed[i]) dspNoteOn(notes[i], timbre);
  // 4) voces que YA sonaban y no se reutilizaron: liberar (no toca las frescas).
  for (uint8_t k = 0; k < MAX_VOICES; k++)
    if (wasGlidable[k] && !usedV[k]) voiceRelease(voices[k]);
}

StrumPlan dspMakeStrum(const uint8_t* notes, uint8_t n, float strumMs, float sr) {
  StrumPlan p;
  if (n > MAX_CHORD_NOTES) n = MAX_CHORD_NOTES;
  for (uint8_t i = 0; i < n; i++) p.note[i] = notes[i];
  // orden ascendente (insertion sort: n es chico)
  for (int i = 1; i < n; i++) {
    uint8_t key = p.note[i]; int j = i - 1;
    while (j >= 0 && p.note[j] > key) { p.note[j+1] = p.note[j]; j--; }
    p.note[j+1] = key;
  }
  int step = (int)(strumMs * sr / 1000.0f); if (step < 0) step = 0;
  for (uint8_t i = 0; i < n; i++) p.atSample[i] = (int)i * step;   // espaciado por el retardo
  p.count = n;
  return p;
}

void dspStrumChord(const uint8_t* notes, uint8_t n, uint8_t timbre, float strumMs, float sr) {
  StrumPlan p = dspMakeStrum(notes, n, strumMs, sr);
  clearPending();
  for (uint8_t i = 0; i < p.count; i++) {
    gPending[i] = { p.note[i], timbre, p.atSample[i], true };
  }
}

int dspArpStepIndex(int step, int n, ArpPattern pat) {
  if (n <= 1) return 0;
  int k = ((step % n) + n) % n;
  switch (pat) {
    case ARP_DOWN:   return (n - 1) - k;
    case ARP_UPDOWN: {
      int period = 2 * n - 2;            // sube y baja sin repetir extremos
      int pos = step % period; if (pos < 0) pos += period;
      return (pos < n) ? pos : (period - pos);
    }
    case ARP_DOWNUP: {
      int period = 2 * n - 2;            // baja y sube (espejo de updown)
      int pos = step % period; if (pos < 0) pos += period;
      int up = (pos < n) ? pos : (period - pos);
      return (n - 1) - up;
    }
    case ARP_CONVERGE:                   // afuera->adentro: 0, n-1, 1, n-2, ...
      return (k % 2 == 0) ? (k / 2) : (n - 1 - k / 2);
    case ARP_DIVERGE: {                  // adentro->afuera (converge al revés)
      int kk = n - 1 - k;
      return (kk % 2 == 0) ? (kk / 2) : (n - 1 - kk / 2);
    }
    case ARP_UP:
    default:         return k;           // RANDOM/CHORD se resuelven en el trigger
  }
}

// RNG chico para el estilo RANDOM del arp (no necesita ser fuerte).
static inline uint32_t arpRand() {
  static uint32_t s = 0x1234567u;
  s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}

// Apaga todas las notas que el arp tiene sonando ahora.
static void arpNotesOff() {
  for (uint8_t i = 0; i < gArpHeldN; i++) dspNoteOff(gArpHeld[i]);
  gArpHeldN = 0;
}

void dspArpStart(const uint8_t* notes, uint8_t n, uint8_t timbre,
                 float bpm, int stepsPerBeat, ArpPattern pat, float gate, float sr) {
  if (n > ARP_MAX_STEPS) n = ARP_MAX_STEPS;
  for (uint8_t i = 0; i < n; i++) gArpNotes[i] = notes[i];   // secuencia tal cual (ya ordenada)
  bool wasOn = gArpOn;
  gArpN = n; gArpTimbre = timbre; gArpPat = pat;
  gArpGate = (gate < 0.05f) ? 0.05f : (gate > 1.0f ? 1.0f : gate);
  if (bpm < 1) bpm = 1; if (stepsPerBeat < 1) stepsPerBeat = 1;
  (void)sr;                                     // el reloj maestro corre a SAMPLE_RATE
  clockSetBpm(bpm);                             // el arp fija el tempo del reloj maestro
  gArpGridSteps = stepsPerBeat * 4;             // pasos de la grilla por compás (4 pulsos)
  gArpStepSamples = (int)(gClkBarSamps / (uint32_t)gArpGridSteps);   // largo del paso (para el gate)
  if (gArpStepSamples < 1) gArpStepSamples = 1;
  // Si el arp YA estaba sonando (p.ej. cambió el color), conservar reloj y paso:
  // el set nuevo entra en tiempo real sin cortar. Sólo arranca de cero si estaba
  // apagado: engancha la grilla a la fase vigente ("uno antes" -> el catch-up
  // dispara el paso actual en el próximo bloque, en fase con el "1").
  if (!wasOn) {
    uint32_t ss = gClkBarSamps / (uint32_t)gArpGridSteps; if (ss < 1) ss = 1;
    int cur = (int)(gClkPos / ss); if (cur >= gArpGridSteps) cur = gArpGridSteps - 1;
    gArpGrid = (cur - 1 + gArpGridSteps) % gArpGridSteps;
    gArpStep = 0; gArpHeldN = 0; gArpLast = -1; gArpSeqPos = 0;   // el secuenciador arranca en el paso 0
  }
  gArpOn = (n > 0);
}

void dspArpStop() {
  gArpOn = false; gArpExtSync = false;
  arpNotesOff();
  gArpLast = -1; gArpGateLeft = 0;
}

int dspArpLastNote() { return gArpLast; }

// Un paso del SECUENCIADOR (estilo ARP_SEQ): índice de nota / silencio / ligadura.
// La ligadura NO corta la nota anterior (legato); el silencio la corta y calla.
static void arpTriggerSeqStep() {
  if (gArpSeqActiveLen == 0) { gArpStep++; return; }
  uint8_t v = gArpSeqActive[gArpSeqPos];
  gArpSeqPos = (uint8_t)((gArpSeqPos + 1) % gArpSeqActiveLen);
  gArpStep++;
  if (v == ARP_STEP_TIE) { gArpGateLeft = 0; return; }   // ligadura: sostiene (legato)
  arpNotesOff();                                          // nota nueva o silencio: corta la anterior
  if (v == ARP_STEP_REST) { gArpLast = -1; gArpGateLeft = 0; return; }   // silencio
  int idx = v % gArpN;                                    // transponible sobre el acorde vigente
  uint8_t note = gArpNotes[idx];
  dspNoteOn(note, gArpTimbre);
  gArpHeld[gArpHeldN++] = note;
  gArpLast = note;
  gArpGateLeft = (gArpGate < 0.98f) ? (int)(gArpStepSamples * gArpGate) : 0;
}

// Dispara el siguiente paso: apaga lo anterior y toca el set del paso (1 nota,
// o TODO el acorde en estilo CHORD). La usan el reloj interno (blockSetup) y el
// sync externo (dspArpAdvance).
static void arpTriggerStep() {
  if (!gArpOn || gArpN <= 0) return;
  if (gArpPat == ARP_SEQ) { arpTriggerSeqStep(); return; }   // secuenciador de pasos
  arpNotesOff();                                    // corta lo que sonaba del arp
  if (gArpPat == ARP_CHORD) {                       // acorde entero como ritmo
    for (uint8_t i = 0; i < gArpN; i++) {
      dspNoteOn(gArpNotes[i], gArpTimbre);
      gArpHeld[gArpHeldN++] = gArpNotes[i];
    }
    gArpLast = gArpN ? gArpNotes[gArpN - 1] : -1;
  } else {
    int idx = (gArpPat == ARP_RANDOM) ? (int)(arpRand() % (uint32_t)gArpN)
                                      : dspArpStepIndex(gArpStep, gArpN, gArpPat);
    uint8_t note = gArpNotes[idx];
    dspNoteOn(note, gArpTimbre);
    gArpHeld[gArpHeldN++] = note;
    gArpLast = note;
  }
  // Gate: si es < ~legato, programar el note-off a `gate` del paso; si es legato
  // (≈1) no se corta antes (se apaga en el próximo paso).
  gArpGateLeft = (gArpGate < 0.98f) ? (int)(gArpStepSamples * gArpGate) : 0;
  gArpStep++;
}

// Cuantización del looper al compás (definición de la capa maestra en el "1").
// Se llama al cruzar el downbeat. Sólo actúa con gLpQuantize activo.
static void looperOnDownbeat();

// Modulación a tasa de bloque: LFO (vibrato/trémolo) y cutoff del filtro.
static void blockSetup(int n) {
  float blockDt = (float)n / (float)SAMPLE_RATE;    // duración del bloque en segundos

  // RELOJ MAESTRO: avanza la fase del compás y marca el cruce del "1" (downbeat).
  // Todo lo que sigue (batería, arp, trémolo, looper) DERIVA su fase de gClkPos.
  gClkDownbeat = false;
  if (gClkRun && gClkBarSamps > 0) {
    gClkPos += (uint32_t)n;
    if (gClkPos >= gClkBarSamps) { gClkPos -= gClkBarSamps; gClkBar++; gClkDownbeat = true; }
  }

  // STRUM: disparar las notas pendientes cuyo retardo ya venció (tasa de bloque).
  for (auto& p : gPending) {
    if (!p.armed) continue;
    p.samplesLeft -= n;
    if (p.samplesLeft <= 0) { dspNoteOn(p.note, p.timbre); p.armed = false; }
  }

  // PATRÓN DE BATERÍA: el paso OBJETIVO se DERIVA de la posición del reloj maestro
  // (exacto, sin drift) y se lo alcanza disparando cada paso cruzado en el bloque
  // (robusto a cualquier tamaño de bloque). Para patrones de 2 compases, gClkBar %
  // bars da la fase de la FRASE, así el patrón largo también queda anclado al "1".
  // Al cruzar el downbeat de la frase, target vuelve a 0 -> golpe en fase con
  // arp/trémolo.
  if (gDrumsOn && gClkBarSamps > 0 && gDrumTotal > 0) {
    int bars = (gDrumPat >= 1) ? DRUM_BARS[gDrumPat] : 1; if (bars < 1) bars = 1;
    int stepsPerBar = gDrumTotal / bars; if (stepsPerBar < 1) stepsPerBar = 1;
    uint32_t stepSamps = gClkBarSamps / (uint32_t)stepsPerBar; if (stepSamps < 1) stepSamps = 1;
    int inBar = (int)(gClkPos / stepSamps); if (inBar >= stepsPerBar) inBar = stepsPerBar - 1;
    int barInPat = (int)(gClkBar % (uint32_t)bars);
    int target = barInPat * stepsPerBar + inBar;         // paso absoluto 0..gDrumTotal-1
    int guard = 0;
    while (gDrumStep != target && guard++ < gDrumTotal) {
      gDrumStep = (gDrumStep + 1) % gDrumTotal;
      if (DRUM_KICK[gDrumPat]  & (1u << gDrumStep)) dspTriggerKick();
      if (DRUM_SNARE[gDrumPat] & (1u << gDrumStep)) dspTriggerSnare();
      if (DRUM_HAT[gDrumPat]   & (1u << gDrumStep)) dspTriggerHat();
    }
  }

  // ARP: gate (largo de nota). Cuando vence, apaga el set del arp antes del
  // próximo paso -> staccato. A tasa de bloque (~6 ms de resolución, imperceptible).
  if (gArpOn && gArpGateLeft > 0) {
    gArpGateLeft -= n;
    if (gArpGateLeft <= 0) { arpNotesOff(); gArpGateLeft = 0; }
  }
  // ARPEGIADOR (reloj INTERNO): igual que la batería, el paso de la grilla se
  // deriva del reloj maestro (target exacto + alcance por pasos) -> el arp cae en
  // fase con el "1". El sync externo (gArpExtSync) avanza sólo por dspArpAdvance().
  if (gArpOn && gArpN > 0 && !gArpExtSync && gClkBarSamps > 0 && gArpGridSteps > 0) {
    uint32_t stepSamps = gClkBarSamps / (uint32_t)gArpGridSteps; if (stepSamps < 1) stepSamps = 1;
    int target = (int)(gClkPos / stepSamps); if (target >= gArpGridSteps) target = gArpGridSteps - 1;
    int guard = 0;
    while (gArpGrid != target && guard++ < gArpGridSteps) {
      gArpGrid = (gArpGrid + 1) % gArpGridSteps;
      arpTriggerStep();
    }
  }

  // LOOPER: cuantización de arranque/fin de la capa maestra al compás.
  if (gClkDownbeat) looperOnDownbeat();

  // TRÉMOLO: en fase con el "1". Reseteo la fase al punto de ACENTO (0.75), donde
  // tg=1 (volumen máximo): así el pico del trémolo cae JUSTO en el downbeat y en
  // cada subdivisión de la grilla (16 ciclos/compás en 1/16) -> suena "pegado" al
  // golpe, no medio ciclo corrido. (Con el rate sincronizado al BPM la corrección
  // por downbeat es mínima; sólo realinea el drift de float y los cambios de tempo.)
  if (gTremOn && gClkDownbeat) gTremPhase = 0.75f;

  for (auto& v : voices) {
    if (!v.active) continue;
    const EnvCfg& e = ENV[v.timbre];
    // Glide de TIEMPO constante: cada voz avanza su propio paso; al pasar el
    // objetivo se clava. Todas llegan juntas sin importar el tamaño del salto.
    bool needFreq = false;
    if (v.curMidi != v.targMidi) {
      v.curMidi += v.glideStep;
      if (v.glideStep == 0 ||
          (v.glideStep > 0 && v.curMidi >= v.targMidi) ||
          (v.glideStep < 0 && v.curMidi <= v.targMidi)) v.curMidi = v.targMidi;
      needFreq = true;
    }
    // BODY: transitorio de ataque que se asienta + drift lento (inestabilidad).
    if (v.atkPitch != 0.0f) { v.atkPitch *= 0.82f; if (fabsf(v.atkPitch) < 1e-4f) v.atkPitch = 0.0f; needFreq = true; }
    if (gBodyOn) {
      v.drift += dspNoise() * 0.0018f * gBodyAmt;   // paso del random walk (audible)
      v.drift *= 0.99f;                              // tiende a 0 (no se aleja)
      needFreq = true;
    } else if (v.drift != 0.0f) {
      v.drift *= 0.85f; if (fabsf(v.drift) < 1e-4f) v.drift = 0.0f;   // al apagar BODY vuelve a afinación
      needFreq = true;
    }
    if (needFreq) voiceSetFreq(v, v.curMidi + v.detune + v.drift + v.atkPitch);
    v.lfoVal = v.lfo.process(blockDt);              // valor del LFO para todo el bloque
    float fc = e.cutHz * powf(2.0f, e.fEnvOct * v.fenv.level);
    v.filt.setCoeffs(fc, e.res, (float)SAMPLE_RATE);
    if (v.osc == T_STRINGS)                         // canal R de las cuerdas (ancho estéreo)
      v.filtR.setCoeffs(fc, e.res, (float)SAMPLE_RATE);
  }
}

// --- Sync externo del arp (lo maneja el integrador con el clock MIDI entrante) ---
void dspArpAdvance()           { arpTriggerStep(); }   // avanza un paso AHORA
void dspArpSetExtSync(bool on) { gArpExtSync = on; }   // true = ignora el reloj interno

// tanh rápido (racional clampeado) para el soft-clip del camino por muestra.
// El tanhf de libm es software en Xtensa (cientos de ciclos); a 2 llamadas por
// muestra × 44100 Hz se comía ~10-15% del core y era el empujón que faltaba para
// el underrun con muchas voces (ruido al terminar/cambiar acordes). Error <1e-3
// en |x|<=1, saturación monótona a ±1 en |x|>=3: indistinguible como soft-clip.
static inline float fastTanh(float x) {
  if (x >  3.0f) return  1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Mezcla estéreo seca (sin master): suma de voces en L y R.
static inline void renderMixStereoRaw(float& mixL, float& mixR) {
  mixL = 0; mixR = 0;
  for (auto& v : voices) {
    if (!v.active) continue;
    float l, r; renderVoiceStereo(v, l, r);
    float g = (v.group == VG_BASS) ? gBassGain : gSynthGain;   // sub-bajo y voces por separado
    mixL += l * g; mixR += r * g;
  }
}

void dspRenderBlock(float* out, int n) {
  blockSetup(n);
  for (int i = 0; i < n; i++) {
    float mL, mR; renderMixStereoRaw(mL, mR);
    out[i] = fastTanh(0.5f * (mL + mR) * MASTER_GAIN);   // mono = (L+R)/2; nunca clippea duro
  }
}

// Render estéreo seco (sin efectos): para tests del ancho estéreo.
void dspRenderStereoDry(float* outL, float* outR, int n) {
  blockSetup(n);
  for (int i = 0; i < n; i++) {
    float mL, mR; renderMixStereoRaw(mL, mR);
    outL[i] = fastTanh(mL * MASTER_GAIN);
    outR[i] = fastTanh(mR * MASTER_GAIN);
  }
}

// ----------------------------------------------------------------------------
// LOOPER de 4 capas (implementación; el estado se declara arriba con los globals).
// ----------------------------------------------------------------------------
void looperInit(int16_t* const bufs[LOOPER_LAYERS], uint32_t maxLen) {
  gLpMax = maxLen; gLpMaster = 0; gLpRun = true;
  for (int i = 0; i < LOOPER_LAYERS; i++) {
    gLp[i].buf = bufs[i]; gLp[i].len = 0; gLp[i].pos = 0; gLp[i].state = LP_EMPTY; gLp[i].gain = 0.85f;
  }
}

void looperRecordToggle(int L) {
  if (L < 0 || L >= LOOPER_LAYERS || !gLp[L].buf) return;
  gLpRun = true;                              // grabar arranca el transporte (si estaba en STOP)
  LpLayer& y = gLp[L];
  if (gLpMaster == 0) {                       // todavía no hay loop maestro
    // CUANTIZADO: la maestra arranca y termina en el "1" -> largo = compases
    // enteros. El arranque/fin real los hace looperOnDownbeat() en el downbeat.
    if (gLpQuantize && gClkBarSamps > 0) {
      if (gLpRecPend) { gLpRecPend = false; return; }        // armada sin arrancar -> cancela
      if (y.state == LP_REC) { gLpStopPend = true; return; } // grabando -> arma el fin en el "1"
      if (y.state == LP_EMPTY) { gLpRecPend = true; gLpRecLayer = L; return; }  // arma el arranque
    }
    if (y.state == LP_EMPTY) { y.state = LP_REC; y.pos = 0; y.len = 0; }
    else if (y.state == LP_REC) {             // stop -> define el maestro
      if (y.pos == 0) { y.state = LP_EMPTY; return; }
      y.len = y.pos; gLpMaster = y.len; y.state = LP_PLAY; gLpPos = 0;
    }
  } else {                                    // ya hay loop maestro
    // EMPTY -> graba una vuelta COMPLETA (recLeft = master) y auto-para. Así
    // nunca queda buffer sin grabar (evita la basura de PSRAM = ruido metálico).
    if      (y.state == LP_EMPTY)   { y.state = LP_REC;     y.len = gLpMaster; y.recLeft = gLpMaster; }
    // LP_REC: no se frena antes (auto-para al completar la vuelta).
    else if (y.state == LP_PLAY)    { y.state = LP_OVERDUB; y.recLeft = gLpMaster; }
    else if (y.state == LP_OVERDUB) { y.state = LP_PLAY; }
    else if (y.state == LP_MUTED)   { y.state = LP_PLAY; }
  }
}

// Cruce del "1": ejecuta el arranque/fin diferidos de la capa maestra (cuantización).
// Al arrancar y terminar en downbeats, el largo del loop queda en compases enteros
// y su tope (gLpPos=0) coincide con el "1" del reloj -> looper en fase con drums/arp.
static void looperOnDownbeat() {
  if (!gLpQuantize) return;
  if (gLpRecPend && gLpMaster == 0) {
    LpLayer& y = gLp[gLpRecLayer];
    if (y.state == LP_EMPTY) { y.state = LP_REC; y.pos = 0; y.len = 0; }
    gLpRecPend = false;
  } else if (gLpStopPend && gLpMaster == 0) {
    LpLayer& y = gLp[gLpRecLayer];
    if (y.state == LP_REC && y.pos > 0) {
      y.len = y.pos; gLpMaster = y.pos; y.state = LP_PLAY; gLpPos = 0;
    }
    gLpStopPend = false;
  }
}

void looperToggleMute(int L) {
  if (L < 0 || L >= LOOPER_LAYERS) return;
  if (gLp[L].state == LP_PLAY)  gLp[L].state = LP_MUTED;
  else if (gLp[L].state == LP_MUTED) gLp[L].state = LP_PLAY;
}

void looperClear(int L) {
  if (L < 0 || L >= LOOPER_LAYERS) return;
  gLp[L].state = LP_EMPTY; gLp[L].len = 0; gLp[L].pos = 0;
  bool anyo = false;                          // si no queda ninguna capa con contenido, liberar el maestro
  for (int i = 0; i < LOOPER_LAYERS; i++) if (gLp[i].state != LP_EMPTY) anyo = true;
  if (!anyo) gLpMaster = 0;
}

void looperClearAll() {
  for (int i = 0; i < LOOPER_LAYERS; i++) { gLp[i].state = LP_EMPTY; gLp[i].len = 0; gLp[i].pos = 0; }
  gLpMaster = 0; gLpPos = 0; gLpRecPend = false; gLpStopPend = false;
}

// STOP del transporte: las capas dejan de sonar (congeladas) y vuelven al inicio.
// Conserva el contenido (grabar/volver a entrar reanuda). Para borrar usar Clear.
void looperStopAll() {
  gLpRun = false; gLpPos = 0; gLpRecPend = false; gLpStopPend = false;
  for (int i = 0; i < LOOPER_LAYERS; i++) {
    if (gLp[i].state == LP_REC) { gLp[i].state = LP_EMPTY; gLp[i].len = 0; }  // descarta grabación parcial
    else if (gLp[i].state == LP_OVERDUB) gLp[i].state = LP_PLAY;              // overdub: corta, conserva
  }
}

void     dspLooperQuantize(bool on) { gLpQuantize = on; }
bool     dspLooperQuantizeOn()      { return gLpQuantize; }
int      looperLayerState(int L) { return (L >= 0 && L < LOOPER_LAYERS) ? gLp[L].state : LP_EMPTY; }
uint32_t looperMasterLen()       { return gLpMaster; }
uint32_t looperPos()             { return gLpPos; }

// Procesa UNA muestra: graba `src` (mezcla seca) en las capas REC/OVERDUB y
// devuelve la suma de las capas que reproducen (con soft-clip).
// Todas las capas comparten la posición GLOBAL del loop (gLpPos) -> quedan en
// fase. La capa maestra (mientras crece, sin master) usa su propia pos.
float looperTick(float src) {
  if (!gLpRun) return 0.0f;                     // STOP: ni suena ni avanza (congelado)
  int16_t s16 = lpClip16((int)(src * 32767.0f));

  // --- Caso A: todavía se está grabando la capa MAESTRA (define el largo) ---
  if (gLpMaster == 0) {
    for (int L = 0; L < LOOPER_LAYERS; L++) {
      LpLayer& y = gLp[L];
      if (y.state == LP_REC) {
        y.buf[y.pos] = s16; y.pos++; y.len = y.pos;
        if (y.pos >= gLpMax) { y.len = gLpMax; gLpMaster = gLpMax; y.state = LP_PLAY; gLpPos = 0; }
      }
    }
    return 0.0f;                               // mientras se graba la maestra no suena nada más
  }

  // --- Caso B: hay maestro -> todas las capas usan la posición global gLpPos ---
  float sum = 0.0f;
  for (int L = 0; L < LOOPER_LAYERS; L++) {
    LpLayer& y = gLp[L];
    switch (y.state) {
      case LP_REC:                             // capa nueva grabando una vuelta completa
        y.buf[gLpPos] = s16;
        if (--y.recLeft == 0) y.state = LP_PLAY;
        break;
      case LP_PLAY:
        sum += (float)y.buf[gLpPos] * (1.0f / 32768.0f) * y.gain;
        break;
      case LP_OVERDUB: {
        float ex = (float)y.buf[gLpPos] * (1.0f / 32768.0f);
        sum += ex * y.gain;
        y.buf[gLpPos] = lpClip16((int)y.buf[gLpPos] + (int)s16);  // existente + nuevo
        if (--y.recLeft == 0) y.state = LP_PLAY;                  // overdub UNA vuelta -> PLAY
        break; }
      default: break;                          // MUTED / EMPTY: no suman
    }
  }
  gLpPos++; if (gLpPos >= gLpMaster) gLpPos = 0;   // avanza la posición global del loop
  return fastTanh(sum);                           // soft-clip: 4 capas a tope no clippean duro
}

// ----------------------------------------------------------------------------
// PERCUSIÓN (implementación). Bombo = seno con caída rápida de tono + amplitud.
// Caja = ruido (cuerpo) + dos senos (parche) con decaimientos cortos.
// ----------------------------------------------------------------------------
void dspTriggerKick() {
  if (gDrumSamples) { gSmp[0].pos = 0; gSmp[0].on = true; return; }   // retrigger = choke
  gKick.on = true;  gKick.amp = 0.95f; gKick.pitch = 1.0f; gKick.ph = 0.0f;
}
void dspTriggerSnare() {
  if (gDrumSamples) { gSmp[1].pos = 0; gSmp[1].on = true; return; }
  gSnare.on = true; gSnare.amp = 0.85f; gSnare.tone = 0.7f; gSnare.ph1 = 0.0f; gSnare.ph2 = 0.0f;
}
void dspTriggerHat() {
  if (gDrumSamples) { gSmp[2].pos = 0; gSmp[2].on = true; return; }
  gHat.on = true;   gHat.amp = 0.55f;
}

// Motor de la batería: true = samples (drum_samples.h), false = sintetizada.
void dspDrumsSetEngine(bool samples) {
  gDrumSamples = samples;
  gKick.on = gSnare.on = gHat.on = false;                 // corta lo que sonaba
  gSmp[0].on = gSmp[1].on = gSmp[2].on = false;
}
bool dspDrumsEngine() { return gDrumSamples; }

// Volumen general de la batería (0..1); escala TODOS los golpes en la mezcla.
void  dspDrumsSetGain(float g) { if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f; gDrumGain = g; }
float dspDrumsGain()           { return gDrumGain; }
void  dspSetSynthGain(float g) { if (g < 0.0f) g = 0.0f; if (g > 2.0f) g = 2.0f; gSynthGain = g; }
float dspSynthGain()           { return gSynthGain; }
void  dspSetBassGain(float g)  { if (g < 0.0f) g = 0.0f; if (g > 2.0f) g = 2.0f; gBassGain = g; }
float dspBassGain()            { return gBassGain; }

// Patrón de batería sincronizado al BPM. pattern 0=off, 1..DRUM_PATTERNS-1.
void dspDrumsSet(int pattern, float bpm) {
  if (pattern < 0) pattern = 0; if (pattern >= DRUM_PATTERNS) pattern = DRUM_PATTERNS - 1;
  bool wasOn = gDrumsOn;
  gDrumPat = pattern; gDrumsOn = (pattern > 0);
  if (bpm < 20.0f) bpm = 20.0f;
  gDrumBpmCur = bpm;
  clockSetBpm(bpm);                        // la batería fija el tempo del reloj maestro
  // El paso dura según la SUBDIVISIÓN del patrón: 4/pulso binaria, 3 ternaria.
  int div  = (pattern >= 1) ? DRUM_DIV[pattern]  : 4;
  int bars = (pattern >= 1) ? DRUM_BARS[pattern] : 1;
  int stepsPerBar = 4 * div;
  gDrumTotal = bars * stepsPerBar;
  gDrumStepSamples = (int)(gClkBarSamps / (uint32_t)stepsPerBar);   // largo del paso (escritura en vivo)
  if (gDrumStepSamples < 1) gDrumStepSamples = 1;
  if (gDrumStep >= gDrumTotal) gDrumStep %= gDrumTotal;   // cambio de largo en vivo
  if (gDrumsOn && !wasOn) {                 // STOP -> PLAY: reinicia la GROOVE desde el "1"
    // Reinicia el reloj maestro al downbeat: la batería (y trémolo/arp) arrancan
    // en fase desde el paso 0, como un drum machine al apretar play. Sin esto, al
    // derivar del reloj continuo, la batería entraba a mitad de compás.
    gClkPos = 0; gClkBar = 0; gClkDownbeat = false;
    gDrumStep = gDrumTotal - 1;             // "uno antes" del 0 -> el catch-up dispara el paso 0
  }
}

// --- Reloj maestro: API pública -------------------------------------------
// Fija el tempo del transporte (drums/arp/trémolo/looper derivan su fase de él).
// El integrador (.ino) lo llama en cada cambio de BPM para que el reloj sea la
// única fuente de verdad, incluso si en ese momento no hay batería ni arp activos.
void     dspClockSet(float bpm) { clockSetBpm(bpm); }
void     dspClockReset()        { gClkPos = 0; gClkBar = 0; gClkDownbeat = false; }   // vuelve al "1"
float    dspClockBpm()          { return gClkBpm; }
uint32_t dspClockPos()          { return gClkPos; }        // muestras desde el "1" (UI/tests)
uint32_t dspClockBarSamples()   { return gClkBarSamps; }   // muestras por compás (0 = sin fijar)

// Disciplina la FASE del reloj maestro a un clock MIDI externo (24 PPQN, 96 ticks
// por compás 4/4): fija gClkPos a la posición exacta del tick dentro del compás.
// El .ino lo llama una vez por pulso mientras esclava; entre pulsos el reloj corre
// suave por su cuenta y este realineado corrige el drift. tick 0 -> el "1".
void     dspClockSyncTicks(uint32_t tickInBar) {
  if (gClkBarSamps == 0) return;
  tickInBar %= 96;
  gClkPos = (uint32_t)((uint64_t)tickInBar * (uint64_t)gClkBarSamps / 96ULL);
}

// Paso actual de la grilla de batería (0..total-1), para el indicador del OLED.
// -1 si el patrón está apagado.
int dspDrumsStepNow() {
  // gDrumStep = paso ya disparado (el que SUENA), derivado del reloj maestro.
  if (!gDrumsOn || gDrumTotal <= 0) return -1;
  return (gDrumStep < 0) ? 0 : (gDrumStep % gDrumTotal);
}

// Golpe EN VIVO de un cuerpo (0=bombo 1=caja 2=hi-hat) desde los switches.
// record=true y patrón sonando: además lo ESCRIBE cuantizado al paso más
// cercano del patrón activo (overdub estilo TR: el golpe queda en el loop de
// batería). Si cuantiza al paso siguiente, la grilla lo re-dispara al llegar
// (flam corto; el choke de los samples lo disimula).
void dspDrumLiveHit(int inst, bool record) {
  if      (inst == 0) dspTriggerKick();
  else if (inst == 1) dspTriggerSnare();
  else if (inst == 2) dspTriggerHat();
  if (!record || !gDrumsOn || gDrumPat < 1) return;
  // Cuantiza al paso más cercano de la GRILLA DEL PATRÓN (compases y subdivisión
  // editables): el transcurrido del paso actual sale del reloj maestro (gClkPos %
  // largo-de-paso). Antes de la mitad -> paso actual; después -> el siguiente.
  int elapsed = (gDrumStepSamples > 0) ? (int)(gClkPos % (uint32_t)gDrumStepSamples) : 0;
  int cur = (gDrumStep < 0) ? 0 : gDrumStep;
  int step = (2 * elapsed < gDrumStepSamples) ? cur : (cur + 1);
  uint32_t m = 1u << (((step % gDrumTotal) + gDrumTotal) % gDrumTotal);
  if      (inst == 0) DRUM_KICK[gDrumPat]  |= m;
  else if (inst == 1) DRUM_SNARE[gDrumPat] |= m;
  else if (inst == 2) DRUM_HAT[gDrumPat]   |= m;
}

// Metrónomo on/off + tempo. Reinicia la cuenta: el próximo click cae ya mismo
// y el "1" queda donde se activó (o donde cambió el BPM).
void dspMetroSet(bool on, float bpm) {
  gMetOn = on;
  if (bpm < 20.0f) bpm = 20.0f;
  gMetSpb = (int)(60.0f / bpm * (float)SAMPLE_RATE);
  if (gMetSpb < 1) gMetSpb = 1;
  gMetCounter = 1; gMetBeat = 3;                     // el próximo pulso es el "1"
  if (!on) gMetEnv = 0.0f;
}
bool dspMetro() { return gMetOn; }

float dspDrumTick() {
  float s = 0.0f;
  // METRÓNOMO: blip de seno con decay corto; el "1" del compás va más agudo y
  // fuerte. Independiente del patrón (sirve de guía para grabar loops).
  if (gMetOn) {
    if (--gMetCounter <= 0) {
      gMetCounter += gMetSpb;
      gMetBeat = (gMetBeat + 1) & 3;
      gMetEnv = (gMetBeat == 0) ? 0.9f : 0.5f;
      gMetPhase = 0.0f;
      gMetInc = ((gMetBeat == 0) ? 2093.0f : 1568.0f) / (float)SAMPLE_RATE;
    }
    if (gMetEnv > 0.001f) {
      gMetPhase += gMetInc; if (gMetPhase >= 1.0f) gMetPhase -= 1.0f;
      s += fastSine(gMetPhase) * gMetEnv;
      gMetEnv *= gMetDecay;
    }
  }
  if (gKick.on) {
    gKick.pitch *= gKPitDec;                         // el tono cae rápido (~25 ms): "pluck" del bombo
    float f = 45.0f + 85.0f * gKick.pitch;           // 130 Hz -> 45 Hz
    gKick.ph += f / (float)SAMPLE_RATE; if (gKick.ph >= 1.0f) gKick.ph -= 1.0f;
    gKick.amp *= gKAmpDec;                            // amplitud cae (~160 ms)
    s += fastSine(gKick.ph) * gKick.amp;
    if (gKick.amp < 0.001f) gKick.on = false;
  }
  if (gSnare.on) {
    gSnare.amp  *= gSAmpDec;                          // ruido (~95 ms)
    gSnare.tone *= gSToneDec;                         // parche tonal (~55 ms)
    gSnare.ph1 += 180.0f / (float)SAMPLE_RATE; if (gSnare.ph1 >= 1.0f) gSnare.ph1 -= 1.0f;
    gSnare.ph2 += 330.0f / (float)SAMPLE_RATE; if (gSnare.ph2 >= 1.0f) gSnare.ph2 -= 1.0f;
    s += dspNoise() * gSnare.amp * 0.7f
       + (fastSine(gSnare.ph1) + fastSine(gSnare.ph2)) * gSnare.tone * 0.33f;
    if (gSnare.amp < 0.001f && gSnare.tone < 0.001f) gSnare.on = false;
  }
  if (gHat.on) {
    float n = dspNoise();
    gHat.lp += gHatHpK * (n - gHat.lp);              // pasa-bajos que sigue al ruido...
    float hp = n - gHat.lp;                          // ...restado = pasa-altos (chick metálico)
    gHat.amp *= gHAmpDec;                            // decay corto (~45 ms, hat cerrado)
    s += hp * gHat.amp;
    if (gHat.amp < 0.001f) gHat.on = false;
  }
  // Motor de SAMPLES: lectura desde flash con interpolación lineal (paso 0.5:
  // los WAV están a 22050 y el motor corre a 44100). One-shot, sin loop.
  for (auto& sp : gSmp) {
    if (!sp.on) continue;
    uint32_t i = (uint32_t)sp.pos;
    float    fr = sp.pos - (float)i;
    float    a = (float)sp.d[i];
    float    b = (i + 1 < sp.len) ? (float)sp.d[i + 1] : 0.0f;
    s += (a + (b - a) * fr) * (1.0f / 32768.0f) * sp.gain;
    sp.pos += gSmpStep;
    if (sp.pos >= (float)(sp.len - 1)) sp.on = false;
  }
  // Soft-clip del bus de batería: kick+snare+hat simultáneos pueden sumar >2 de
  // pico; sin esto clipearía duro en la salida. (El gain general aplica después,
  // lineal, así que el volumen sigue escalando exacto.)
  return fastTanh(s);
}

void DSP_HOT dspRenderStereo(float* outL, float* outR, int n) {
  blockSetup(n);
  for (int i = 0; i < n; i++) {
    float mL, mR; renderMixStereoRaw(mL, mR);
    float dryL = fastTanh(mL * MASTER_GAIN);
    float dryR = fastTanh(mR * MASTER_GAIN);
    float lp = looperTick(0.5f * (dryL + dryR));     // graba SÓLO las voces; suma las capas
    dryL += lp; dryR += lp;                          // las capas suenan ANTES de reverb/delay
    if (gChorusOn) {                                 // chorus (engorda y ensancha) antes de reverb/delay
      float cl, cr; gChorus.process(0.5f * (dryL + dryR), cl, cr);
      dryL += cl; dryR += cr;
    }
    float mono = 0.5f * (dryL + dryR);
    float wet  = gReverb.process(mono);             // cola de reverb (mono)
    float eL, eR; gDelay.processWet(mono + wet, eL, eR);   // ecos del delay (L≠R)
    float oL = dryL + wet + eL;
    float oR = dryR + wet + eR;
    if (gTremOn) {                                  // tremolo de salida (sincronizado a BPM)
      float tg = 1.0f - gTremDepth * (0.5f + 0.5f * fastSine(gTremPhase));  // 1-depth .. 1
      oL *= tg; oR *= tg;
      gTremPhase += gTremInc; if (gTremPhase >= 1.0f) gTremPhase -= 1.0f;
    }
    float drum = dspDrumTick() * gDrumGain;         // batería: en vivo, seca, NO se graba en el looper
    oL += drum; oR += drum;
    outL[i] = fastTanh(oL * 0.5f);                // soft-clip final: voces+fx+drums no clippean duro (anti-saturación PAM8403)
    outR[i] = fastTanh(oR * 0.5f);
  }
}

void dspDelaySet(float timeMs, float feedback, float mix) {
  gDelay.setParams(timeMs, feedback, mix, (float)SAMPLE_RATE);
}
void dspDelaySyncBpm(float bpm, float beatFraction) {
  if (bpm < 1) bpm = 1;
  gDelay.setTime((60000.0f / bpm) * beatFraction, (float)SAMPLE_RATE);
}
float dspDelayTimeMs() { return gDelay.dL * 1000.0f / (float)SAMPLE_RATE; }

const char* dspTimbreName(uint8_t t) { return T_NAME[t % T_COUNT]; }

int dspActiveVoices() {
  int c = 0;
  for (auto& v : voices) if (v.active) c++;
  return c;
}
