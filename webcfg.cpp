/* ============================================================================
   webcfg.cpp  -  Configuración por USB (Web Serial) + persistencia NVS.
   Ver webcfg.h para el protocolo. Capa de HARDWARE: puede usar Arduino y
   Preferences; toda mutación del DSP pasa por la cola de synth (thread-safe).
   ============================================================================ */
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_TinyUSB.h>   // tud_cdc_*: FIFO crudo del CDC (ver webcfgPoll)
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "config.h"
#include "state.h"
#include "harmony.h"
#include "dsp.h"
#include "synth.h"
#include "midi.h"     // midiSendSysExText: respuestas por SysEx (transporte MIDI)
#include "webcfg.h"

#define WEBCFG_FW "tolhuin-3.2"

extern AppState app;   // estado global del sketch (tonalidad, fx, voicing...)

// ----------------------------------------------------------------------------
// Salida de respuestas con dos transportes: SIEMPRE al Serial (monitor/editor
// por Web Serial) y, si el comando llegó por SysEx MIDI, también se acumula en
// un buffer que se manda como F0 7D 02 <JSON> F7 al terminar el comando.
// ----------------------------------------------------------------------------
static bool gViaMidi = false;
static char gOut[600];
static int  gOutLen = 0;

static void outf(const char* fmt, ...) {
  char tmp[360];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n > (int)sizeof(tmp) - 1) n = sizeof(tmp) - 1;
  Serial.print(tmp);
  if (gViaMidi) {
    int c = (int)sizeof(gOut) - 1 - gOutLen;
    if (c > n) c = n;
    if (c > 0) { memcpy(gOut + gOutLen, tmp, c); gOutLen += c; }
  }
}

static Preferences prefs;

// ----------------------------------------------------------------------------
// fmDec/fmDec2 se guardan como incrementos por muestra; el protocolo habla en
// ms (más humano para el editor). Conversión ida y vuelta:
// ----------------------------------------------------------------------------
static float incToMs(float inc) { return (inc > 0.0f) ? 1000.0f / (inc * (float)SAMPLE_RATE) : 0.0f; }
static float msToInc(float ms)  { return (ms  > 0.0f) ? 1.0f / (ms * (float)SAMPLE_RATE / 1000.0f) : 0.0f; }

// Tabla de campos float de EnvCfg (nombre del protocolo -> miembro).
// `inMs` marca los que viajan en ms pero se guardan como incremento.
struct EnvField { const char* key; float EnvCfg::* mem; bool inMs; };
static const EnvField ENV_FIELDS[] = {
  { "atk",     &EnvCfg::atkMs,   false },
  { "dec",     &EnvCfg::decMs,   false },
  { "sus",     &EnvCfg::sus,     false },
  { "rel",     &EnvCfg::relMs,   false },
  { "cut",     &EnvCfg::cutHz,   false },
  { "res",     &EnvCfg::res,     false },
  { "fenv",    &EnvCfg::fEnvOct, false },
  { "fatk",    &EnvCfg::fAtkMs,  false },
  { "fdec",    &EnvCfg::fDecMs,  false },
  { "fsus",    &EnvCfg::fSus,    false },
  { "frel",    &EnvCfg::fRelMs,  false },
  { "lrate",   &EnvCfg::lfoRate, false },
  { "vib",     &EnvCfg::vib,     false },
  { "trem",    &EnvCfg::trem,    false },
  { "fmdec",   &EnvCfg::fmDec,   true  },
  { "fmidx",   &EnvCfg::fmIdx,   false },
  { "fmfloor", &EnvCfg::fmFloor, false },
  { "fmdec2",  &EnvCfg::fmDec2,  true  },
  { "fmidx2",  &EnvCfg::fmIdx2,  false },
  { "tine",    &EnvCfg::tineRatio, false },
  { "wtmorph", &EnvCfg::wtMorph, false },
};
static const int ENV_NFIELDS = sizeof(ENV_FIELDS) / sizeof(ENV_FIELDS[0]);

// Emite un EnvCfg como línea JSON (cmd = "env" o "factory").
static void printEnvJson(const char* cmd, int t, const EnvCfg& e) {
  outf("{\"cmd\":\"%s\",\"t\":%d", cmd, t);
  for (int i = 0; i < ENV_NFIELDS; i++) {
    float v = e.*(ENV_FIELDS[i].mem);
    if (ENV_FIELDS[i].inMs) v = incToMs(v);
    outf(",\"%s\":%.5g", ENV_FIELDS[i].key, (double)v);
  }
  outf(",\"perc\":%d,\"family\":%d}\n", e.perc ? 1 : 0, (int)e.family);
}

static void printOk()               { outf("{\"ok\":1}\n"); }
static void printErr(const char* m) { outf("{\"ok\":0,\"err\":\"%s\"}\n", m); }

// Emite un slot de arp como JSON (config + nombre).
static void printArpJson(int slot) {
  ArpCfg c = synthArpCfgGet(slot);
  outf("{\"cmd\":\"arp\",\"slot\":%d,\"style\":%u,\"oct\":%u,\"rate\":%u,"
       "\"gate\":%u,\"scale\":%u,\"name\":\"%s\"}\n",
       slot, c.style, c.octaves, c.rate, c.gate, c.scaleRun, synthPresetName(1, slot));
}

// #setarp <slot> style=.. oct=.. rate=.. gate=.. scale=..  (patch parcial)
static void cmdSetArp(char* args) {
  char* tok = strtok(args, " ");
  if (!tok) { printErr("slot"); return; }
  int s = atoi(tok);
  if (s < 0 || s >= ARP_SLOTS) { printErr("slot"); return; }
  ArpCfg c = synthArpCfgGet(s);
  while ((tok = strtok(NULL, " ")) != NULL) {
    char* eq = strchr(tok, '='); if (!eq) continue;
    *eq = 0; int v = atoi(eq + 1);
    if      (strcmp(tok, "style") == 0) c.style    = (uint8_t)v;
    else if (strcmp(tok, "oct") == 0)   c.octaves  = (uint8_t)v;
    else if (strcmp(tok, "rate") == 0)  c.rate     = (uint8_t)v;
    else if (strcmp(tok, "gate") == 0)  c.gate     = (uint8_t)v;
    else if (strcmp(tok, "scale") == 0) c.scaleRun = (uint8_t)(v ? 1 : 0);
  }
  synthArpCfgSet(s, c);
  printOk();
}

// ----------------------------------------------------------------------------
// Persistencia NVS. Un blob por timbre (EnvCfg tal cual) + máscaras de batería.
// "cfgv" guarda sizeof(EnvCfg): si el struct cambia, lo viejo se descarta.
// ----------------------------------------------------------------------------
// SESIÓN del intérprete: cómo quedó el equipo (para arrancar donde lo dejaste).
// Formato propio versionado (independiente del cfgv de los timbres).
struct SessCfg {
  uint8_t  ver;                       // = SESS_VER
  uint8_t  tonic, mode, preset, timbre;
  int8_t   octave;
  uint16_t bpm;
  uint8_t  drumPat, arpSlot, tremMode;
  uint8_t  chorus, body, glide, sustain, subBass;
  uint8_t  joyMap;                    // mapa de joystick (JoyMap) [v3]
  uint8_t  voiceLead;                 // conducción de voces (VoiceLeadMode) [v4]
};
#define SESS_VER 4
static int gSessTimbre = -1;          // timbre restaurado (-1 = sin sesión)

static void nvsSave() {
  prefs.begin("tolhuin", false);
  prefs.putUShort("cfgv", (uint16_t)sizeof(EnvCfg));
  prefs.putUChar("tcnt", (uint8_t)T_COUNT);  // también versiona el orden del enum Timbre
  char key[8];
  for (uint8_t t = 0; t < T_COUNT; t++) {
    EnvCfg e; synthEnvGet(t, &e);
    snprintf(key, sizeof(key), "env%u", t);
    prefs.putBytes(key, &e, sizeof(e));
  }
  // Sesión del intérprete (el timbre vive en synth, el resto en app).
  SessCfg ses = { SESS_VER, app.tonic, (uint8_t)app.mode, app.preset, synthTimbre(),
                  app.octaveOffset, app.bpm, app.drumPat, app.arpSlot, app.tremMode,
                  (uint8_t)app.chorus, (uint8_t)app.body, (uint8_t)app.glide,
                  (uint8_t)app.sustain, (uint8_t)app.subBass, app.joyMap,
                  app.voiceLeadMode };
  prefs.putBytes("ses", &ses, sizeof(ses));
  uint32_t k[DRUM_PATTERNS], s[DRUM_PATTERNS], h[DRUM_PATTERNS];
  uint8_t  bd[DRUM_PATTERNS * 2];              // bars[12] + div[12] en un solo blob
  for (int p = 0; p < DRUM_PATTERNS; p++) {
    synthDrumPatternGet(p, &k[p], &s[p], &h[p]);
    synthDrumPatternCfgGet(p, &bd[p], &bd[DRUM_PATTERNS + p]);
  }
  prefs.putBytes("dk4", k, sizeof(k));         // claves nuevas: máscaras de 32 bits
  prefs.putBytes("ds4", s, sizeof(s));
  prefs.putBytes("dh4", h, sizeof(h));
  prefs.putBytes("dbd", bd, sizeof(bd));       // grilla por patrón (bars + div)
  prefs.putFloat("drg", synthDrumsGain());
  prefs.putFloat("syg", synthSynthGain());     // volumen de synth
  prefs.putFloat("bsg", synthBassGain());      // volumen de sub-bajo
  prefs.putUChar("dre", synthDrumsEngine() ? 1 : 0);
  prefs.remove("drk"); prefs.remove("drs"); prefs.remove("drh");   // limpia el formato viejo
  // Slots de arp (ArpCfg) + nombres de presets (drums y arps).
  for (int s = 0; s < ARP_SLOTS; s++) {
    ArpCfg c = synthArpCfgGet(s);
    char key[8]; snprintf(key, sizeof(key), "arp%d", s);
    prefs.putBytes(key, &c, sizeof(c));
    snprintf(key, sizeof(key), "an%d", s);
    prefs.putString(key, synthPresetName(1, s));
  }
  for (int p = 1; p < DRUM_PATTERNS; p++) {
    char key[8]; snprintf(key, sizeof(key), "dn%d", p);
    prefs.putString(key, synthPresetName(0, p));
  }
  prefs.end();
}

static bool nvsLoad() {
  prefs.begin("tolhuin", true);
  bool any = false;
  // Historial de layouts: 14 (sin HBRASS), 15 (HBRASS), 17 (EPIANO B/C).
  uint8_t storedTimbres = prefs.getUChar("tcnt", 0);
  if (storedTimbres == 0) {
    if      (prefs.isKey("env16")) storedTimbres = 17;
    else if (prefs.isKey("env14")) storedTimbres = 15;
    else                           storedTimbres = 14;
  }

  // TIMBRES: además del tamaño se valida el orden del enum. Al migrar desde el
  // layout de 14 slots, 0..7 se conservan, HBRASS (8) queda de fábrica y los
  // seis USR anteriores se desplazan de 8..13 a 9..14.
  if (prefs.getUShort("cfgv", 0) == (uint16_t)sizeof(EnvCfg)) {
    char key[8];
    if (storedTimbres == T_COUNT) {
      for (uint8_t t = 0; t < T_COUNT; t++) {
        EnvCfg e;
        snprintf(key, sizeof(key), "env%u", t);
        if (prefs.getBytes(key, &e, sizeof(e)) == sizeof(e)) { synthEnvApply(t, e); any = true; }
      }
    } else if (storedTimbres == 15) {
      for (uint8_t oldT = 0; oldT < storedTimbres; oldT++) {
        EnvCfg e;
        snprintf(key, sizeof(key), "env%u", oldT);
        uint8_t newT = (oldT <= T_HBRASS) ? oldT : (uint8_t)(oldT + 2);
        if (prefs.getBytes(key, &e, sizeof(e)) == sizeof(e)) { synthEnvApply(newT, e); any = true; }
      }
    } else if (storedTimbres == 14) {
      for (uint8_t oldT = 0; oldT < storedTimbres; oldT++) {
        EnvCfg e;
        snprintf(key, sizeof(key), "env%u", oldT);
        uint8_t newT = (oldT < T_HBRASS) ? oldT : (uint8_t)(oldT + 3);
        if (prefs.getBytes(key, &e, sizeof(e)) == sizeof(e)) { synthEnvApply(newT, e); any = true; }
      }
    }
  }

  // BATERÍA: máscaras de 32 bits + grilla (o migración desde las de 16).
  {
    uint32_t k[DRUM_PATTERNS] = {0}, s[DRUM_PATTERNS] = {0}, h[DRUM_PATTERNS] = {0};
    if (prefs.getBytes("dk4", k, sizeof(k)) == sizeof(k) &&
        prefs.getBytes("ds4", s, sizeof(s)) == sizeof(s)) {
      prefs.getBytes("dh4", h, sizeof(h));
      for (int p = 1; p < DRUM_PATTERNS; p++) synthDrumPatternSet(p, k[p], s[p], h[p]);
      uint8_t bd[DRUM_PATTERNS * 2];
      if (prefs.getBytes("dbd", bd, sizeof(bd)) == sizeof(bd))
        for (int p = 1; p < DRUM_PATTERNS; p++)
          synthDrumPatternCfgSet(p, bd[p], bd[DRUM_PATTERNS + p]);
      any = true;
    } else {
      uint16_t k2[DRUM_PATTERNS] = {0}, s2[DRUM_PATTERNS] = {0}, h2[DRUM_PATTERNS] = {0};
      if (prefs.getBytes("dk2", k2, sizeof(k2)) == sizeof(k2) &&
          prefs.getBytes("ds2", s2, sizeof(s2)) == sizeof(s2)) {
        prefs.getBytes("dh2", h2, sizeof(h2));   // migración: 16 -> 32 bits directo
        for (int p = 1; p < DRUM_PATTERNS; p++) synthDrumPatternSet(p, k2[p], s2[p], h2[p]);
        any = true;
      }
    }
    synthDrumsSetGain(prefs.getFloat("drg", 1.0f));
    synthSetSynthGain(prefs.getFloat("syg", 1.0f));       // volumen de synth
    synthSetBassGain(prefs.getFloat("bsg", 1.0f));        // volumen de sub-bajo
    synthDrumsSetEngine(prefs.getUChar("dre", 1) != 0);   // por defecto: samples
  }

  // ARPS + NOMBRES (si faltan, quedan los de fábrica de dspInit).
  for (int s = 0; s < ARP_SLOTS; s++) {
    ArpCfg c; char key[8]; snprintf(key, sizeof(key), "arp%d", s);
    if (prefs.getBytes(key, &c, sizeof(c)) == sizeof(c)) { synthArpCfgSet(s, c); any = true; }
    snprintf(key, sizeof(key), "an%d", s);
    if (prefs.isKey(key)) { String nm = prefs.getString(key); synthPresetNameSet(1, s, nm.c_str()); }
  }
  for (int p = 1; p < DRUM_PATTERNS; p++) {
    char key[8]; snprintf(key, sizeof(key), "dn%d", p);
    if (prefs.isKey(key)) { String nm = prefs.getString(key); synthPresetNameSet(0, p, nm.c_str()); }
  }

  // SESIÓN del intérprete -> app (los toggles/timbre los APLICA el .ino al
  // terminar el arranque; acá sólo se restauran los datos).
  {
    // Migración compatible: v3 y v4 comparten sizeof (18 B: el padding del
    // uint16_t iguala ambos). Se distingue por el campo `ver`, NO por longitud.
    // En v3 el byte que hoy ocupa `voiceLead` es padding basura -> se fuerza
    // VL_SMOOTH (nunca se interpreta un byte viejo como un modo válido); el
    // resto de la sesión v3 se preserva.
    SessCfg s;
    memset(&s, 0, sizeof(s));
    s.voiceLead = VL_SMOOTH;
    bool okSes = (prefs.getBytes("ses", &s, sizeof(s)) == sizeof(s) &&
                  (s.ver == 3 || s.ver == 4));
    if (s.ver != 4) s.voiceLead = VL_SMOOTH;   // v3 (o campo ausente): por defecto
    if (okSes) {
      app.tonic = s.tonic % 12;
      app.mode  = (Mode)(s.mode % 2);
      app.preset = s.preset % HP_COUNT;
      app.octaveOffset = (s.octave < -2) ? -2 : (s.octave > 2 ? 2 : s.octave);
      app.bpm = (s.bpm < 40 || s.bpm > 300) ? 120 : s.bpm;
      app.drumPat = (s.drumPat >= 1 && s.drumPat < DRUM_PATTERNS) ? s.drumPat : 1;
      app.arpSlot = s.arpSlot % ARP_SLOTS;
      app.tremMode = s.tremMode % 4;
      app.chorus = s.chorus != 0;  app.body = s.body != 0;
      app.glide = s.glide != 0;    app.sustain = s.sustain != 0;
      app.subBass = s.subBass != 0;
      app.joyMap = s.joyMap % JM_COUNT;
      // Fuera de rango o campo ausente -> VL_SMOOTH (clamp, NUNCA módulo: un byte
      // corrupto/viejo no debe reinterpretarse como un modo válido).
      app.voiceLeadMode = harmonyVoiceLeadNorm((int)s.voiceLead);
      uint8_t st = s.timbre;
      if      (storedTimbres == 15 && st >  T_HBRASS) st += 2;
      else if (storedTimbres == 14 && st >= T_HBRASS) st += 3;
      gSessTimbre = st % T_COUNT;
      any = true;
    }
  }
  prefs.end();
  return any;
}

// API pública (menú físico + comandos #save/#load).
void webcfgSave() { nvsSave(); }
bool webcfgLoad() { return nvsLoad(); }
int  webcfgSessionTimbre(int def) { return (gSessTimbre >= 0) ? gSessTimbre : def; }

// ----------------------------------------------------------------------------
// Comandos.
// ----------------------------------------------------------------------------
static void cmdHello() {
  outf("{\"cmd\":\"hello\",\"fw\":\"%s\",\"timbres\":%d,\"drumpats\":%d,\"drumsteps\":%d,"
       "\"arpslots\":%d,\"sr\":%u,\"names\":[",
       WEBCFG_FW, T_COUNT, DRUM_PATTERNS, DRUM_STEPS, ARP_SLOTS, SAMPLE_RATE);
  for (uint8_t t = 0; t < T_COUNT; t++)
    outf("%s\"%s\"", t ? "," : "", dspTimbreName(t));
  outf("],\"arprates\":[");
  for (int r = 0; r < ARP_RATES; r++) outf("%s\"%s\"", r ? "," : "", dspArpRateName((uint8_t)r));
  outf("]}\n");
}

// #setenv <t> k=v [k=v ...]  -> patch parcial sobre los parámetros vivos.
static void cmdSetEnv(char* args) {
  char* tok = strtok(args, " ");
  if (!tok) { printErr("timbre"); return; }
  int t = atoi(tok);
  if (t < 0 || t >= T_COUNT) { printErr("timbre"); return; }
  EnvCfg e; synthEnvGet((uint8_t)t, &e);
  while ((tok = strtok(NULL, " ")) != NULL) {
    char* eq = strchr(tok, '=');
    if (!eq) continue;
    *eq = 0;
    float v = (float)atof(eq + 1);
    if (strcmp(tok, "perc") == 0)   { e.perc = (v != 0.0f); continue; }
    if (strcmp(tok, "family") == 0) { e.family = (uint8_t)((int)v % T_ENGINE_COUNT); continue; }
    for (int i = 0; i < ENV_NFIELDS; i++) {
      if (strcmp(tok, ENV_FIELDS[i].key) == 0) {
        e.*(ENV_FIELDS[i].mem) = ENV_FIELDS[i].inMs ? msToInc(v) : v;
        break;
      }
    }
  }
  synthEnvApply((uint8_t)t, e);
  printOk();
}

// Parsea hasta `max` floats separados por espacio. Devuelve cuántos leyó.
static int parseFloats(char* args, float* v, int max) {
  int n = 0;
  for (char* tok = strtok(args, " "); tok && n < max; tok = strtok(NULL, " "))
    v[n++] = (float)atof(tok);
  return n;
}

// #state -> estado global (para que el editor sincronice su UI al conectar).
static void cmdState() {
  outf("{\"cmd\":\"state\",\"timbre\":%u,\"tonic\":%u,\"mode\":%u,\"preset\":%u,"
       "\"voicing\":%u,\"lead\":%u,\"octave\":%d,\"bpm\":%u,\"chorus\":%d,\"body\":%d,"
       "\"glide\":%d,\"sustain\":%d,\"mono\":%d,\"drums\":%u,\"trem\":%u,"
       "\"arp\":%u,\"arpslot\":%u,\"subbass\":%d}\n",
       synthTimbre(), app.tonic, (unsigned)app.mode, app.preset,
       (unsigned)app.voicing, (unsigned)app.voiceLeadMode, (int)app.octaveOffset, app.bpm,
       app.chorus ? 1 : 0, app.body ? 1 : 0, app.glide ? 1 : 0,
       app.sustain ? 1 : 0, app.mono ? 1 : 0,
       (unsigned)(app.drumsOn ? app.drumPat : 0), app.tremMode,
       app.arpMode, app.arpSlot, app.subBass ? 1 : 0);
}

// #degree <1..7> [zona 0..8] -> toca el grado con el MISMO camino que los botones
// físicos (armonía diatónica + voice-leading + voicing + octava + línea de bajo)
// y devuelve las notas + el símbolo, para el visualizador de voicings de la web.
static void cmdDegree(char* args) {
  int d = 0, z = 0;
  if (args) sscanf(args, "%d %d", &d, &z);
  if (d < 1 || d > 7 || z < 0 || z > 8) { printErr("grado/zona"); return; }
  uint8_t deg = (uint8_t)(d - 1);
  ColorZone zone = (ColorZone)z;

  uint8_t notes[MAX_CHORD_NOTES];
  uint8_t n = harmonyChordPreset(deg, zone, app.tonic, app.mode, app.preset, notes);

  // Con el arp activo: arma la secuencia (octavas + scale-run) y audiciona el arp.
  if (app.arpMode) {
    harmonyApplyOctave(notes, n, app.octaveOffset);
    ArpCfg c = synthArpCfgGet(app.arpSlot);
    uint8_t seq[ARP_MAX_STEPS];
    uint8_t sn = harmonyArpSequence(notes, n, c.octaves, c.scaleRun != 0,
                                    app.tonic, app.mode, seq, ARP_MAX_STEPS);
    synthArpPlay(seq, sn);
    char sym0[16];
    harmonyChordSymbol(deg, zone, app.tonic, app.mode, app.preset, sym0, sizeof(sym0));
    outf("{\"cmd\":\"chord\",\"deg\":%d,\"zone\":%d,\"sym\":\"%s\",\"arp\":1,\"notes\":[", d, z, sym0);
    for (uint8_t i = 0; i < sn; i++) outf("%s%u", i ? "," : "", seq[i]);
    outf("]}\n");
    return;
  }

  // Pipeline ÚNICO compartido con el firmware físico (harmonyBuildVoiced): usa
  // joyMap + chordQual + voiceLeadMode + voicing + octava -> MISMAS notas que
  // playCurrent para el mismo estado. (Antes divergía: preset + SMOOTH histórico.)
  n = harmonyBuildVoiced(deg, zone, app.tonic, app.mode, app.preset,
                         app.chordQual[deg % 7], app.joyMap,
                         (VoiceLeadMode)app.voiceLeadMode, app.voicing,
                         app.octaveOffset, notes);
  synthPlaySet(notes, n);

  uint8_t bt[MAX_CHORD_NOTES];                      // bajo dedicado (como playCurrent)
  int bd = (app.bassDegree >= 0) ? app.bassDegree : (int)deg;
  harmonyChord((uint8_t)bd, CZ_CENTER, app.tonic, app.mode, bt);
  int bass = (int)bt[0] + 12 * app.octaveOffset;
  while (bass > 47)      bass -= 12;
  while (bass < MIDI_LO) bass += 12;
  uint8_t chordTimbre = synthTimbre();
  uint8_t bassTimbre = app.subBass
      ? (chordTimbre == T_HBRASS ? (uint8_t)T_HBRASS : (uint8_t)T_SINE)
      : chordTimbre;
  synthSetBass(bass, bassTimbre);

  // Símbolo joy-aware + campos extra para el visualizador: `lead` (algoritmo) y
  // `bass` (voz dedicada, separada del acorde principal).
  char sym[16];
  harmonyChordSymbolJoy(deg, zone, app.tonic, app.mode, app.preset,
                        app.chordQual[deg % 7], app.joyMap, sym, sizeof(sym));
  outf("{\"cmd\":\"chord\",\"deg\":%d,\"zone\":%d,\"sym\":\"%s\",\"lead\":%u,\"bass\":%d,\"notes\":[",
       d, z, sym, (unsigned)app.voiceLeadMode, bass);
  for (uint8_t i = 0; i < n; i++) outf("%s%u", i ? "," : "", notes[i]);
  outf("]}\n");
}

// #chord 60,64,67  -> audición del acorde (usa la conducción de voces normal).
static void cmdChord(char* args) {
  uint8_t notes[MAX_CHORD_NOTES];
  uint8_t n = 0;
  for (char* tok = strtok(args, ", "); tok && n < MAX_CHORD_NOTES; tok = strtok(NULL, ", ")) {
    int v = atoi(tok);
    if (v > 0 && v < 128) notes[n++] = (uint8_t)v;
  }
  if (!n) { printErr("notas"); return; }
  synthPlaySet(notes, n);
  printOk();
}

static void processLine(char* line) {
  if (line[0] != '#') return;              // no es un comando del web-config
  char* sp = strchr(line, ' ');
  char* args = NULL;
  if (sp) { *sp = 0; args = sp + 1; }
  const char* cmd = line + 1;

  if      (strcmp(cmd, "hello") == 0) cmdHello();
  else if (strcmp(cmd, "getenv") == 0) {
    int t = args ? atoi(args) : -1;
    if (t < 0 || t >= T_COUNT) { printErr("timbre"); return; }
    EnvCfg e; synthEnvGet((uint8_t)t, &e);
    printEnvJson("env", t, e);
  }
  else if (strcmp(cmd, "factory") == 0) {
    int t = args ? atoi(args) : -1;
    if (t < 0 || t >= T_COUNT) { printErr("timbre"); return; }
    EnvCfg e; synthEnvFactory((uint8_t)t, &e);
    printEnvJson("factory", t, e);
  }
  else if (strcmp(cmd, "setenv") == 0) { if (args) cmdSetEnv(args); else printErr("args"); }
  else if (strcmp(cmd, "getdrum") == 0) {
    int p = args ? atoi(args) : -1;
    if (p < 0 || p >= DRUM_PATTERNS) { printErr("patron"); return; }
    uint32_t k, s, h; synthDrumPatternGet(p, &k, &s, &h);
    uint8_t bars, div; synthDrumPatternCfgGet(p, &bars, &div);
    // steps = largo REAL de la grilla de este patrón (bars × 4 × div).
    outf("{\"cmd\":\"drum\",\"p\":%d,\"steps\":%d,\"bars\":%u,\"div\":%u,"
         "\"kick\":%lu,\"snare\":%lu,\"hat\":%lu,"
         "\"gain\":%.3f,\"eng\":%d,\"name\":\"%s\"}\n",
         p, (int)bars * 4 * (int)div, bars, div,
         (unsigned long)k, (unsigned long)s, (unsigned long)h,
         (double)synthDrumsGain(), synthDrumsEngine() ? 1 : 0,
         synthPresetName(0, p));
  }
  else if (strcmp(cmd, "setdrum") == 0) {
    // "#setdrum p kick snare hat [bars div]" — máscaras de 32 bits; bars 1..2,
    // div 4 (binaria) o 3 (ternaria). Sin bars/div conserva la grilla actual.
    int p = -1, bars = -1, div = -1;
    unsigned long k = 0, s = 0, h = 0;
    if (args) sscanf(args, "%d %lu %lu %lu %d %d", &p, &k, &s, &h, &bars, &div);
    if (p < 1 || p >= DRUM_PATTERNS) { printErr("patron"); return; }
    synthDrumPatternSet(p, (uint32_t)k, (uint32_t)s, (uint32_t)h);
    if (bars > 0 && div > 0) synthDrumPatternCfgSet(p, (uint8_t)bars, (uint8_t)div);
    printOk();
  }
  else if (strcmp(cmd, "drumgain") == 0) {
    if (!args) { printErr("args"); return; }
    synthDrumsSetGain((float)atof(args));
    printOk();
  }
  else if (strcmp(cmd, "synthgain") == 0) {              // volumen de synth (0..2)
    if (!args) { printErr("args"); return; }
    synthSetSynthGain((float)atof(args));
    printOk();
  }
  else if (strcmp(cmd, "bassgain") == 0) {               // volumen de sub-bajo (0..2)
    if (!args) { printErr("args"); return; }
    synthSetBassGain((float)atof(args));
    printOk();
  }
  else if (strcmp(cmd, "drumeng") == 0) {                // 1 = samples, 0 = synth
    if (!args) { printErr("args"); return; }
    synthDrumsSetEngine(atoi(args) != 0);
    printOk();
  }
  else if (strcmp(cmd, "kick") == 0)  { synthKick();  printOk(); }
  else if (strcmp(cmd, "snarehit") == 0) { synthSnare(); printOk(); }
  else if (strcmp(cmd, "hat") == 0)   { synthHat();   printOk(); }
  else if (strcmp(cmd, "drums") == 0) {
    int p = 0; float bpm = 120.0f;
    if (args) sscanf(args, "%d %f", &p, &bpm);
    synthDrumsSet(p, bpm);
    printOk();
  }
  else if (strcmp(cmd, "bpm") == 0) {                    // tempo (afecta arp/drums/delay)
    int b = args ? atoi(args) : -1;
    if (b < 40 || b > 300) { printErr("bpm"); return; }
    app.bpm = (uint16_t)b;
    if (app.arpMode) {
      ArpCfg c = synthArpCfgGet(app.arpSlot);
      synthSetArp(true, (float)app.bpm, dspArpRateSteps(c.rate), c.style, (float)c.gate / 100.0f);
    }
    if (app.drumsOn) synthDrumsSet(app.drumPat, (float)app.bpm);
    printOk();
  }
  // --- Arpegiador ---
  else if (strcmp(cmd, "getarp") == 0) {
    int s = args ? atoi(args) : -1;
    if (s < 0 || s >= ARP_SLOTS) { printErr("slot"); return; }
    printArpJson(s);
  }
  else if (strcmp(cmd, "setarp") == 0) { if (args) cmdSetArp(args); else printErr("args"); }
  else if (strcmp(cmd, "arp") == 0) {                    // #arp <slot> <on 0/1>: audición
    int s = -1, on = 1;
    if (args) sscanf(args, "%d %d", &s, &on);
    if (s < 0 || s >= ARP_SLOTS) { printErr("slot"); return; }
    app.arpSlot = (uint8_t)s; app.arpMode = on ? 1 : 0;
    if (app.arpMode) { app.strum = false; synthSetStrum(false, 0); app.mono = false; synthSetMono(false); }
    ArpCfg c = synthArpCfgGet(s);
    synthSetArp(app.arpMode != 0, (float)app.bpm, dspArpRateSteps(c.rate), c.style, (float)c.gate / 100.0f);
    printOk();
  }
  // --- Nombres de presets: #drumname <slot> <texto...> / #arpname <slot> <texto...> ---
  else if (strcmp(cmd, "drumname") == 0 || strcmp(cmd, "arpname") == 0) {
    int kind = (cmd[0] == 'a') ? 1 : 0;
    if (!args) { printErr("args"); return; }
    char* sp2 = strchr(args, ' ');
    int slot = atoi(args);
    const char* nm = sp2 ? sp2 + 1 : "";
    int lim = (kind == 0) ? DRUM_PATTERNS : ARP_SLOTS;
    if (slot < 0 || slot >= lim) { printErr("slot"); return; }
    synthPresetNameSet(kind, slot, nm);
    printOk();
  }
  else if (strcmp(cmd, "state") == 0)   cmdState();
  else if (strcmp(cmd, "degree") == 0)  cmdDegree(args);
  else if (strcmp(cmd, "timbre") == 0) {
    int t = args ? atoi(args) : -1;
    if (t < 0 || t >= T_COUNT) { printErr("timbre"); return; }
    synthSetTimbre((uint8_t)t);
    printOk();
  }
  else if (strcmp(cmd, "key") == 0) {
    int k = args ? atoi(args) : -1;
    if (k < 0 || k > 11) { printErr("key"); return; }
    app.tonic = (uint8_t)k; harmonyVoiceLeadReset();
    printOk();
  }
  else if (strcmp(cmd, "mode") == 0) {
    int m = args ? atoi(args) : -1;
    if (m < 0 || m > 1) { printErr("mode"); return; }
    app.mode = (Mode)m; harmonyVoiceLeadReset();
    printOk();
  }
  else if (strcmp(cmd, "octave") == 0) {
    int o = args ? atoi(args) : -9;
    if (o < -2 || o > 2) { printErr("octave"); return; }
    app.octaveOffset = (int8_t)o;
    printOk();
  }
  else if (strcmp(cmd, "voicing") == 0) {
    int v = args ? atoi(args) : -1;
    if (v < 0 || v >= VC_COUNT) { printErr("voicing"); return; }
    app.voicing = (Voicing)v;
    printOk();
  }
  else if (strcmp(cmd, "preset") == 0) {
    int p = args ? atoi(args) : -1;
    if (p < 0 || p >= HP_COUNT) { printErr("preset"); return; }
    app.preset = (uint8_t)p;
    printOk();
  }
  else if (strcmp(cmd, "lead") == 0) {                   // conducción de voces (VoiceLeadMode)
    int v = args ? atoi(args) : -1;
    if (v < 0 || v >= VL_COUNT) { printErr("lead"); return; }  // inválido -> error, no se aplica
    app.voiceLeadMode = (uint8_t)v;
    harmonyVoiceLeadReset();                             // cambiar de algoritmo reinicia el historial
    // No re-dispara audio acá (el visualizador re-emite #degree para rehacer el
    // acorde); confirma el valor EFECTIVO aplicado para que la web no lo asuma.
    outf("{\"ok\":1,\"lead\":%u}\n", (unsigned)app.voiceLeadMode);
  }
  // --- Efectos (sincronizan el estado del menú físico donde existe el flag) ---
  else if (strcmp(cmd, "chorus") == 0) {
    float v[4] = { 0, 0.7f, 7.0f, 0.4f };
    int nv = args ? parseFloats(args, v, 4) : 0;
    if (nv < 1) { printErr("args"); return; }
    app.chorus = (v[0] != 0);
    synthSetChorus(app.chorus, v[1], v[2], v[3]);
    printOk();
  }
  else if (strcmp(cmd, "reverb") == 0) {
    float v[3];
    if (!args || parseFloats(args, v, 3) < 3) { printErr("args"); return; }
    synthSetReverb(v[0], v[1], v[2]);
    printOk();
  }
  else if (strcmp(cmd, "delay") == 0) {
    float v[3];
    if (!args || parseFloats(args, v, 3) < 3) { printErr("args"); return; }
    synthDelaySet(v[0], v[1], v[2]);
    printOk();
  }
  else if (strcmp(cmd, "fxmode") == 0) {
    int mode = args ? atoi(args) : -1;
    if (mode < 0 || mode > 2) { printErr("args"); return; }
    dspSetFxMode((uint8_t)mode);
    printOk();
  }
  else if (strcmp(cmd, "trem") == 0) {
    float v[3] = { 0, 4.0f, 0.6f };
    int nv = args ? parseFloats(args, v, 3) : 0;
    if (nv < 1) { printErr("args"); return; }
    synthSetTremolo(v[0] != 0, v[1], v[2]);
    printOk();
  }
  else if (strcmp(cmd, "body") == 0) {
    float v[2] = { 0, 0.6f };
    int nv = args ? parseFloats(args, v, 2) : 0;
    if (nv < 1) { printErr("args"); return; }
    app.body = (v[0] != 0);
    synthSetBody(app.body, v[1]);
    printOk();
  }
  else if (strcmp(cmd, "glide") == 0) {
    float v[2] = { 0, 220.0f };
    int nv = args ? parseFloats(args, v, 2) : 0;
    if (nv < 1) { printErr("args"); return; }
    app.glide = (v[0] != 0);
    synthSetGlide(app.glide, v[1]);
    printOk();
  }
  else if (strcmp(cmd, "sustain") == 0) {
    float v[2] = { 0, 1500.0f };
    int nv = args ? parseFloats(args, v, 2) : 0;
    if (nv < 1) { printErr("args"); return; }
    app.sustain = (v[0] != 0);
    synthSetSustain(app.sustain, v[1]);
    printOk();
  }
  else if (strcmp(cmd, "subbass") == 0) {                // sub-bajo sine dedicado on/off
    app.subBass = args && atoi(args) != 0;
    printOk();
  }
  else if (strcmp(cmd, "noteon") == 0)  { if (args) { synthNoteOn((uint8_t)atoi(args), 100); printOk(); } }
  else if (strcmp(cmd, "noteoff") == 0) { if (args) { synthNoteOff((uint8_t)atoi(args)); printOk(); } }
  else if (strcmp(cmd, "chord") == 0)   { if (args) cmdChord(args); else printErr("args"); }
  else if (strcmp(cmd, "chordoff") == 0) { synthReleaseAll(); printOk(); }
  else if (strcmp(cmd, "perf") == 0) {
    bool reset = args && strcmp(args, "reset") == 0;
    uint32_t maxUs = 0, late = 0;
    synthPerfStats(&maxUs, &late, reset);
    outf("{\"cmd\":\"perf\",\"maxUs\":%lu,\"lateBlocks\":%lu,\"budgetUs\":%lu,\"voices\":%d,\"reset\":%d}\n",
         (unsigned long)maxUs, (unsigned long)late,
         (unsigned long)((1000000ULL * AUDIO_BLOCK) / SAMPLE_RATE),
         dspActiveVoices(), reset ? 1 : 0);
  }
  else if (strcmp(cmd, "save") == 0) {
    nvsSave();
    // Eco de confirmación: la web lo usa para marcar "guardado ✓" sin ambigüedad.
    outf("{\"ok\":1,\"saved\":1,\"timbres\":%d,\"drumpats\":%d,\"arpslots\":%d}\n",
         T_COUNT, DRUM_PATTERNS, ARP_SLOTS);
  }
  else if (strcmp(cmd, "load") == 0)    { if (nvsLoad()) printOk(); else printErr("nvs vacia"); }
  else printErr("comando");
}

// ----------------------------------------------------------------------------
void webcfgInit() {
  if (nvsLoad()) Serial.println("[webcfg] config de usuario cargada de NVS");
  else           Serial.println("[webcfg] sin config guardada (fabrica)");
}

// Comando llegado por SysEx MIDI (F0 7D 01 <texto> F7, lo llama midi.cpp).
// Procesa la línea y devuelve la respuesta acumulada como F0 7D 02 <JSON> F7.
void webcfgFeedMidi(const char* text, int len) {
  char line[320];
  if (len > (int)sizeof(line) - 1) len = sizeof(line) - 1;
  memcpy(line, text, len);
  line[len] = 0;
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
  if (!len) return;
  Serial.printf("[webcfg] midi: %s\n", line);   // diagnóstico: se ve en el monitor serie
  gViaMidi = true; gOutLen = 0;
  processLine(line);
  if (gOutLen > 0) midiSendSysExText(gOut, gOutLen);
  gViaMidi = false;
}

// Ensamblado de línea compartido por los dos caminos de RX del CDC.
static void feedChar(char c) {
  static char buf[320];
  static int  len = 0;
  if (c == '\r') return;
  if (c == '\n') {
    buf[len] = 0;
    if (len > 0) processLine(buf);
    len = 0;
  } else if (len < (int)sizeof(buf) - 1) {
    buf[len++] = c;
  } else {
    len = 0;   // línea demasiado larga: descartar (protege el buffer)
  }
}

void webcfgPoll() {
  // Camino normal (por si el core drena el CDC).
  while (Serial.available() > 0) feedChar((char)Serial.read());
  // Camino REAL de esta build: con Adafruit TinyUSB + core USB conviviendo, el
  // callback fuerte de RX del CDC no queda linkeado (gana el débil no-op del
  // stack de Adafruit; verificado con nm/addr2line sobre el .elf). Los bytes
  // entrantes quedan en el FIFO crudo de TinyUSB y NADIE los drena: el FIFO se
  // llena y los writes del host se CUELGAN (el endpoint NAKea para siempre).
  // Drenarlo acá restaura el USB serial entrante y re-arma el endpoint.
  while (tud_cdc_available()) {
    int32_t c = tud_cdc_read_char();
    if (c < 0) break;
    feedChar((char)c);
  }
}
