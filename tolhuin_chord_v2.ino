/* ============================================================================
   TOLHUIN CHORD  -  Sintetizador de acordes diatónicos (motor propio, sin AMY)
   ----------------------------------------------------------------------------
   Arquitectura en capas (ver CLAUDE.md):
     harmony.cpp  -> acorde diatónico + color + voice leading + voicing + octava
     dsp.cpp      -> síntesis pura: osc band-limited + ADSR + filtro SVF + LFO
                     + delay + reverb + strum/arp + ancho estéreo   (puro, testeable)
     synth.cpp    -> wrapper I2S + tarea FreeRTOS (núcleo 0) + cola  (hardware)
     input.cpp    -> botones + joystick (ADS1115)                    (hardware)
     midi.cpp     -> entrada MIDI USB (TinyUSB) + BLE (NimBLE)        (hardware)
     este .ino    -> lee los controles y dispara los acordes

   MIDI: clock externo (USB/BLE) sincroniza delay y arp; notas tocan el synth.
         BLE aparece como "TOLHUIN".

   Controles:
     7 botones -> grado diatónico I..vii
       mantené un grado + tocá otro -> el sostenido es el BAJO (slash/inversión/pedal)
     HOLD     -> TAP: sostiene el acorde ; LARGO: activa/desactiva STRUM
     VOICING  -> (tocando) TAP: cicla inversiones ; LARGO: cicla arpegiador
                 (menú abierto) TAP: sube el valor del ítem ; LARGO: lo baja
     joystick -> COLOR del acorde (8 zonas + centro)
     SW del joystick (capa ARMÓNICA; sólo el click, el stick no interviene):
       click SIN grado apretado   -> alterna modo MAYOR / menor
       click CON un grado apretado -> "mayoriza" ese grado: DOMINANTE 7 <-> diatónico
       (mantené el acorde y clickeá para mayorizarlo; clickeá otra vez para volver.
        Mayorizar rompe la regla diatónica a propósito: acordes prestados tipo
        "Creep"; todo se limpia al cambiar de tonalidad o de modo)

   MENÚ DE CONFIG (100% con botones, sin el SW; AGRUPADO en páginas temáticas de
   2-3 filas — el OLED muestra la página entera con la fila elegida invertida):
       SONIDO   (TIMBRE / BODY / RANDOM)     ARMONIA (KEY / MODE / RIQUEZA)
       TOQUE    (OCTAVE / MONO / GLIDE)      AMBIENTE (SUSTAIN / DELAY / CHORUS)
       RITMO    (BPM / DRUMS on-off / PATRON)  ARP (on-off / PRESET)
       SYNC     (TREMOLO / CLK OUT)
     NAV- / NAV+ -> mueven la selección (cruza de página sola; timeout ~5 s)
     VOICING -> edita la fila: TAP sube/enciende, LARGO baja.
     DRUMS y ARP son toggles instantáneos (el patrón/preset se elige aparte).

   FQBN: esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi
   ============================================================================ */
#include <Arduino.h>
#include "config.h"
#include "state.h"
#include "harmony.h"
#include "synth.h"
#include "dsp.h"     // enum Timbre (BRASS/EPIANO/STR/SINE/TRI/ORGAN)
#include "input.h"
#include "midi.h"    // entrada MIDI USB + BLE (clock para sync, notas)
#include "oled.h"    // pantalla de estado SSD1306
#include "webcfg.h"  // editor web por USB (timbres/drums) + persistencia NVS
#include "evloop.h"  // looper de EVENTOS: 1 pista de acordes cuantizada (sin PSRAM)
#include "icons.h"   // pixel-art del OLED (IC_PAGE: icono por página del menú)

AppState app;
static bool chordActive = false;     // hay un acorde sonando ahora
// Octava CON LA QUE SE GRABÓ el loop (se captura al cerrar la toma): el playback
// la usa en vez de app.octaveOffset -> el control OCTAVE queda para la mano en
// vivo (subir la melodía en MONO no corre también los acordes del loop).
static int8_t loopOct = 0;
// Canal DRUMS: si además de sonar, los golpes se GRABAN al patrón (fila GRB).
static bool drumRec = false;
// Timbres POR CAPA del looper (capturados al grabar, como loopOct): cambiar
// TIMBRE en vivo no re-pinta lo que ya está grabado.
static uint8_t loopTimbre = 2;      // acordes del loop (T_STRINGS de fábrica)
static uint8_t melTimbre  = 2;      // melodía del overdub
static int     loopMelNote = -1;    // nota de melodía del loop sonando (-1 = ninguna)
static uint8_t lastMelNote = 60;    // última nota de melodía tocada (para el OFF)

// --- Reloj del looper de eventos: posición en PASOS de semicorchea, derivada
// del contador de muestras del audio (mismo reloj que la batería -> en fase).
// Cambiar el BPM re-escala la posición (el loop se estira/encoge con el tempo).
static float loopPos() {
  return (float)synthSamplePos() * ((float)app.bpm * 4.0f / 60.0f) / (float)SAMPLE_RATE;
}

// --- Estado editable del DELAY (controles SW+grado 5/6/7) ---
static const float DLY_FRAC[4] = { 0.25f, 0.5f, 0.75f, 1.0f };   // 1/16, 1/8, 1/8., 1/4
static const char* DLY_NAME[4] = { "1/16", "1/8", "1/8.", "1/4" };
// Profundidad = feedback + mezcla juntos (0 = off).
static const float DLY_DFB[4]   = { 0.0f, 0.25f, 0.40f, 0.60f };
static const float DLY_DMIX[4]  = { 0.0f, 0.15f, 0.30f, 0.45f };
static uint8_t dlyDiv = 1, dlyDepth = 2;                          // 1/8, medio

// Re-aplica el delay según el tempo (app.bpm), la división y la profundidad.
static void applyDelay() {
  float timeMs = (60000.0f / (float)app.bpm) * DLY_FRAC[dlyDiv];
  synthDelaySet(timeMs, DLY_DFB[dlyDepth], DLY_DMIX[dlyDepth]);
}

// --- TREMOLO sincronizado al BPM (SHIFT+VOICING largo cicla la subdivisión) ---
static const float TRM_MULT[4] = { 0.0f, 1.0f, 2.0f, 4.0f };   // ciclos por pulso: off,1/4,1/8,1/16
static const char* TRM_NAME[4] = { "off", "1/4", "1/8", "1/16" };
// Re-aplica el tremolo: rate = (BPM/60) * subdivisión, profundidad musical fija.
static void applyTremolo() {
  if (app.tremMode == 0) { synthSetTremolo(false, 0.0f, 0.0f); return; }
  float rate = ((float)app.bpm / 60.0f) * TRM_MULT[app.tremMode];
  synthSetTremolo(true, rate, 0.6f);
}

// --- MIDI OUT: manda por USB/BLE lo que suena (diff vs. el set anterior) ---
static uint8_t moutNotes[MAX_CHORD_NOTES + 1];
static uint8_t moutN = 0;
static void midiOut(const uint8_t* notes, uint8_t n) {
  uint8_t off[MAX_CHORD_NOTES + 1], on[MAX_CHORD_NOTES + 1];
  uint8_t nOff = 0, nOn = 0;
  for (uint8_t i = 0; i < moutN; i++) {        // las que ya no están -> off
    bool keep = false;
    for (uint8_t j = 0; j < n; j++) if (notes[j] == moutNotes[i]) { keep = true; break; }
    if (!keep) off[nOff++] = moutNotes[i];
  }
  for (uint8_t j = 0; j < n; j++) {            // las nuevas -> on
    bool had = false;
    for (uint8_t i = 0; i < moutN; i++) if (moutNotes[i] == notes[j]) { had = true; break; }
    if (!had) on[nOn++] = notes[j];
  }
  if (nOff || nOn) midiSendChord(off, nOff, on, nOn, 100);   // todo en un paquete
  for (uint8_t j = 0; j < n; j++) moutNotes[j] = notes[j];
  moutN = n;
}

static void playCurrent(bool fromLoop);   // (definida más abajo; la usa setMono)
static void applyArp();

// Pone/saca el modo MONO con todos sus efectos colaterales (lo usan la fila
// MONO y la fila CANAL): apaga strum/arp al entrar, limpia voces, y con el loop
// sonando re-dispara el acorde vigente (si no, quedaba mudo hasta su evento).
static void setMono(bool on) {
  if (app.mono == on) return;
  app.mono = on;
  if (on) {
    app.strum = false; synthSetStrum(false, 0);
    app.arpMode = 0;   applyArp();
  }
  synthSetMono(on);
  if (evloopState() == EVL_PLAY && chordActive) playCurrent(true);
  else chordActive = false;
}

// Aplica el slot de arp activo al motor (estilo/rate/gate). Llamar al togglear el
// arp, cambiar de slot o de BPM. Con app.arpMode=0 apaga el arp.
static void applyArp() {
  ArpCfg ac = synthArpCfgGet(app.arpSlot);
  synthSetArp(app.arpMode != 0, (float)app.bpm, dspArpRateSteps(ac.rate),
              ac.style, (float)ac.gate / 100.0f);
}

// Reconstruye y dispara el acorde actual a partir del estado global.
// fromLoop=true: lo dispara el LOOPER -> ignora el modo MONO (el loop siempre
// suena en acordes; MONO queda para la melodía que se toca EN VIVO por encima).
static void playCurrent(bool fromLoop = false) {
  // Octava efectiva: el loop usa la de su grabación; la mano, la del menú.
  const int8_t oct = fromLoop ? loopOct : app.octaveOffset;
  uint8_t notes[MAX_CHORD_NOTES];
  uint8_t n = harmonyChordJoy(app.degree, app.zone, app.tonic, app.mode, app.preset,
                              app.chordQual[app.degree % 7], app.joyMap, notes);
  if (app.mono && !fromLoop) {                         // modo mono: sólo la raíz (lead/bajo)
    uint8_t root = notes[0];
    harmonyApplyOctave(&root, 1, oct);
    synthMonoNote(root);
    lastMelNote = root;                                // para grabar el par ON/OFF en el looper
    midiOut(&root, 1);
    return;
  }
  // ARPEGIADOR: arma la secuencia (octavas + scale-run) desde el acorde en estado
  // fundamental y la manda al arp; no hay voicing/bajo (el arp toca nota a nota).
  if (app.arpMode) {
    ArpCfg ac = synthArpCfgGet(app.arpSlot);
    harmonyApplyOctave(notes, n, oct);
    uint8_t seq[ARP_MAX_STEPS];
    uint8_t sn = harmonyArpSequence(notes, n, ac.octaves, ac.scaleRun != 0,
                                    app.tonic, app.mode, seq, ARP_MAX_STEPS);
    synthArpPlay(seq, sn);
    return;
  }
  // Pipeline ÚNICO (compartido con #degree): chordJoy -> voice leading (modo) ->
  // voicing -> octava. Reconstruye desde el estado para no divergir del web.
  n = harmonyBuildVoiced(app.degree, app.zone, app.tonic, app.mode, app.preset,
                         app.chordQual[app.degree % 7], app.joyMap,
                         (VoiceLeadMode)app.voiceLeadMode, app.voicing, oct, notes);

  // El ACORDE: en vivo con el timbre activo; desde el loop, con el que se grabó.
  synthPlaySet(notes, n, fromLoop ? loopTimbre : (uint8_t)255);

  // BAJO: raíz del grado en registro grave, como VOZ DEDICADA (no se mezcla con
  // el acorde). Si hay un grado sostenido, su raíz es el bajo (slash/inversión).
  // Con subBass -> sine, salvo HBRASS E: usa su bajo armónico atenuado en DSP
  // para poner el cuerpo en 125–250 Hz. El resto conserva el comportamiento actual.
  int bd = (app.bassDegree >= 0) ? app.bassDegree : (int)app.degree;
  uint8_t bt[MAX_CHORD_NOTES];
  harmonyChord((uint8_t)bd, CZ_CENTER, app.tonic, app.mode, bt);
  int bass = (int)bt[0] + 12 * oct;
  while (bass > 47)      bass -= 12;                   // registro de bajo (~36..47)
  while (bass < MIDI_LO) bass += 12;
  uint8_t chordTimbre = fromLoop ? loopTimbre : synthTimbre();
  uint8_t bassTimbre = app.subBass
      ? (chordTimbre == T_HBRASS ? (uint8_t)T_HBRASS : (uint8_t)T_SINE)
      : chordTimbre;
  synthSetBass(bass, bassTimbre);

  uint8_t mo[MAX_CHORD_NOTES + 1];                      // MIDI out: acorde + bajo
  for (uint8_t i = 0; i < n; i++) mo[i] = notes[i];
  mo[n] = (uint8_t)bass;
  midiOut(mo, (uint8_t)(n + 1));
}

// ============================ MENÚ DE CONFIGURACIÓN =========================
// NAV+ / NAV- navegan esta lista; SW (tap) edita: on/off togglean directo, y los
// ítems de valor (TIMBRE/DELAY/TREMOLO/OCTAVE/KEY) entran a editar -> NAV± cambia
// el valor, SW sale. El menú aparece en el OLED y se va solo (~3 s).
// El orden del enum ES el orden de navegación: los ítems de cada página son
// contiguos y FX_PAGES los agrupa. NAV recorre la lista plana (cruza de página
// solo); VOICING edita la fila seleccionada (tap = +1/toggle, largo = -1).
// Páginas ORDENADAS POR FRECUENCIA DE USO en vivo: primero lo que se toca todo
// el tiempo (sonido, tonalidad, ritmo/arp), después los ajustes finos y al final
// sync/memoria (una vez por sesión).
enum FxItem {
  FX_TIMBRE = 0, FX_BODY, FX_RANDOM,        // SONIDO
  FX_KEY, FX_MODE, FX_PRESET,               // ARMONIA
  FX_LEAD, FX_JOYMAP,                       // ARMONIA-2 (conducción de voces + mapa del stick)
  FX_BPM, FX_DRUMS, FX_DRUMPAT,             // RITMO (DRUMS = on/off instantáneo)
  FX_ARP, FX_ARPSEL,                        // ARP   (ARP = on/off, ARPSEL = preset)
  FX_LREC, FX_LPLAY, FX_LCLR,               // LOOPER (eventos: rec / play-stop / clear)
  FX_CHAN, FX_DGRB,                         // CANAL (qué tocan los switches + grabar hits)
  FX_OCTAVE, FX_MONO, FX_GLIDE,             // TOQUE
  FX_SUSTAIN, FX_DELAY, FX_CHORUS,          // AMBIENTE
  FX_VSYNTH, FX_VSUB, FX_VDRUM,             // NIVELES (volúmenes synth / sub-bajo / batería)
  FX_TREMOLO, FX_CLOCK, FX_MET,             // SYNC (+ metrónomo)
  FX_SAVE, FX_LOAD,                         // MEMORIA (persistencia en el equipo)
  FX_COUNT
};
// Nombres largos (log serial) y CORTOS (filas del OLED, <=3 chars).
static const char* FX_NAME[FX_COUNT] =
  { "TIMBRE", "BODY", "RANDOM", "KEY", "MODE", "RIQUEZA", "CONDUCCION", "JOY MAP",
    "BPM", "DRUMS",
    "PATRON", "ARP", "ARP PRESET", "LOOP REC", "LOOP PLAY", "LOOP CLEAR", "CANAL",
    "GRABAR HITS", "OCTAVE", "MONO", "GLIDE", "SUSTAIN", "DELAY",
    "CHORUS", "VOL SYNTH", "VOL SUB", "VOL DRUMS",
    "TREMOLO", "CLK OUT", "METRONOMO", "GUARDAR", "CARGAR" };
static const char* FX_SHORT[FX_COUNT] =
  { "TIM", "BDY", "RND", "KEY", "MOD", "RIQ", "LED", "JOY", "BPM", "DRM", "PAT",
    "ARP", "PRE", "REC", "PLY", "CLR", "CHN", "GRB", "OCT", "MON", "GLI", "SUS", "DLY", "CHO",
    "vSY", "vSU", "vDR",
    "TRM", "CLK", "MET", "GRD", "CRG" };
// Páginas del menú (título + rango contiguo del enum).
struct FxPage { const char* title; uint8_t first, count; };
static const FxPage FX_PAGES[] = {
  { "SONIDO",   FX_TIMBRE,  3 },
  { "ARMONIA",  FX_KEY,     3 },
  { "ARMONIA",  FX_LEAD,    2 },   // 2da página: CONDUCCION (LEAD) + JOY MAP
  { "RITMO",    FX_BPM,     3 },
  { "ARP",      FX_ARP,     2 },
  { "LOOPER",   FX_LREC,    3 },
  { "CANAL",    FX_CHAN,    2 },
  { "TOQUE",    FX_OCTAVE,  3 },
  { "AMBIENTE", FX_SUSTAIN, 3 },
  { "NIVELES",  FX_VSYNTH,  3 },
  { "SYNC",     FX_TREMOLO, 3 },
  { "MEMORIA",  FX_SAVE,    2 },
};
static const int FX_NPAGES = sizeof(FX_PAGES) / sizeof(FX_PAGES[0]);
static int      fxSel = 0;         // ítem seleccionado del menú
static bool     menuActive = false;// el menú está visible (NAV/VOICING lo despiertan)
static uint32_t menuLastMs = 0;    // último toque (para el timeout)
static uint8_t  menuDelay = 2;     // 0=off, 1=1/16, 2=1/8, 3=1/8., 4=1/4 (boot = 1/8, ver applyDelay)

static const char* PRESET_NAME[HP_COUNT] = { "TRIADA", "7ma", "9na", "6/9" };
// Conducción de voces (VoiceLeadMode): etiquetas compactas para el OLED.
static const char* LEAD_NAME[VL_COUNT] = { "PAR", "SUAVE", "CORAL", "CONTRA" };
// Mapas de joystick (<=6 chars: la fila del OLED muestra "%.6s").
static const char* JOYMAP_NAME[JM_COUNT] = { "DEFLT", "HICHRD", "MINI" };
static uint32_t fxSavedMs  = 0x80000000u;   // últimos GUARDAR/CARGAR (feedback "ok!")
static uint32_t fxLoadedMs = 0x80000000u;

// Aplica al motor lo que la sesión de NVS restauró en `app` (toggles + tempo).
// Se usa al terminar el arranque y tras CARGAR desde el menú.
static void applySession() {
  synthSetChorus(app.chorus, 0.7f, 7.0f, 0.4f);
  synthSetBody(app.body, 0.9f);
  synthSetGlide(app.glide, 220.0f);
  synthSetSustain(app.sustain, 1500.0f);
  synthClockSet((float)app.bpm);              // reloj maestro = fuente única de tempo
  applyDelay();
  applyTremolo();
  if (app.drumsOn) synthDrumsSet(app.drumPat, (float)app.bpm);
  if (app.arpMode) applyArp();
  harmonyVoiceLeadReset();
}

// Escribe el valor actual del ítem `it` en `buf` (para el OLED).
static void fxValue(int it, char* buf) {
  switch (it) {
    case FX_TIMBRE:  strncpy(buf, synthTimbreName(synthTimbre()), 11); buf[11] = 0; break;
    case FX_KEY:     strncpy(buf, harmonyRootName(0, app.tonic, app.mode), 11); buf[11] = 0; break;
    case FX_CHORUS:  strcpy(buf, app.chorus  ? "on" : "off"); break;
    case FX_BODY:    strcpy(buf, app.body    ? "on" : "off"); break;
    case FX_DELAY:   strcpy(buf, menuDelay == 0 ? "off" : DLY_NAME[menuDelay - 1]); break;
    case FX_TREMOLO: strcpy(buf, TRM_NAME[app.tremMode]); break;
    case FX_GLIDE:   strcpy(buf, app.glide   ? "on" : "off"); break;
    case FX_SUSTAIN: strcpy(buf, app.sustain ? "on" : "off"); break;
    case FX_MONO:    strcpy(buf, app.mono    ? "on" : "off"); break;
    case FX_OCTAVE:  sprintf(buf, "%+d", app.octaveOffset); break;
    case FX_BPM:     sprintf(buf, "%u", app.bpm); break;
    case FX_MODE:    strcpy(buf, app.mode == MODE_IONIAN ? "MAJ" : "min"); break;
    case FX_PRESET:  strcpy(buf, PRESET_NAME[app.preset % HP_COUNT]); break;
    case FX_LEAD:    strcpy(buf, LEAD_NAME[app.voiceLeadMode % VL_COUNT]); break;
    case FX_JOYMAP:  strcpy(buf, JOYMAP_NAME[app.joyMap % JM_COUNT]); break;
    case FX_CLOCK:   strcpy(buf, app.clockOut ? "out" : "off"); break;
    case FX_DRUMS:   strcpy(buf, app.drumsOn ? "on" : "off"); break;
    case FX_DRUMPAT: strncpy(buf, synthPresetName(0, app.drumPat % DRUM_PATTERNS), 11);
                     buf[11] = 0; break;
    case FX_ARP:     strcpy(buf, app.arpMode ? "on" : "off"); break;
    case FX_ARPSEL:  strncpy(buf, synthPresetName(1, app.arpSlot), 11); buf[11] = 0; break;
    case FX_RANDOM:  strcpy(buf, "->"); break;
    case FX_LREC:                                    // "cN" grabando, "od" overdub
      if (evloopState() == EVL_REC) sprintf(buf, "c%u", evloopRecSteps(loopPos()) / 16 + 1);
      else if (evloopOverdub())     strcpy(buf, "od");
      else                          strcpy(buf, "->");
      break;
    case FX_LPLAY:                                   // "--" = todavía no hay loop
      if (evloopState() == EVL_PLAY)      strcpy(buf, "on");
      else if (evloopState() == EVL_STOP) strcpy(buf, "off");
      else                                strcpy(buf, "--");
      break;
    case FX_LCLR:                                    // muestra el largo en compases
      if (evloopLen()) sprintf(buf, "%uc>", evloopLen() / EVLOOP_BAR_STEPS);
      else             strcpy(buf, "->");
      break;
    case FX_CHAN:
      strcpy(buf, app.channel == CH_DRUMS   ? "DRUMS" :
                  app.channel == CH_DRUMPAT ? "PATRN" :
                  app.channel == CH_MELODY  ? "MELOD" : "ACORD");
      break;
    case FX_DGRB:    strcpy(buf, drumRec ? "on" : "off"); break;
    case FX_MET:     strcpy(buf, app.metro ? "on" : "off"); break;
    case FX_VSYNTH:  sprintf(buf, "%d%%", (int)(synthSynthGain() * 100.0f + 0.5f)); break;
    case FX_VSUB:    sprintf(buf, "%d%%", (int)(synthBassGain()  * 100.0f + 0.5f)); break;
    case FX_VDRUM:   sprintf(buf, "%d%%", (int)(synthDrumsGain() * 100.0f + 0.5f)); break;
    case FX_SAVE:    strcpy(buf, (millis() - fxSavedMs < 2000) ? "ok!" : "->"); break;
    case FX_LOAD:    strcpy(buf, (millis() - fxLoadedMs < 2000) ? "ok!" : "->"); break;
    default:         buf[0] = 0; break;
  }
}

// Actúa sobre el ítem `it`. dir=+1 adelante/encender, dir=-1 atrás.
static void fxApply(int it, int dir) {
  switch (it) {
    case FX_TIMBRE:
      synthSetTimbre((uint8_t)((synthTimbre() + dir + T_COUNT) % T_COUNT));
      // Con el loop sonando NO re-dispara: el acorde vigente es del loop y
      // conserva SU timbre; el nuevo se escucha en lo que toques en vivo.
      if (chordActive && evloopState() != EVL_PLAY) { synthReleaseAll(); playCurrent(); }
      break;
    case FX_KEY:
      app.tonic = (uint8_t)((app.tonic + dir + 12) % 12);
      for (int i = 0; i < 7; i++) app.chordQual[i] = CQ_DIATONIC;  // cambia el contexto
      harmonyVoiceLeadReset();
      if (chordActive) playCurrent();
      break;
    case FX_PRESET:
      app.preset = (uint8_t)((app.preset + dir + HP_COUNT) % HP_COUNT);
      if (chordActive) playCurrent();
      break;
    case FX_LEAD:
      // Conducción de voces: PAR/SUAVE/CORAL/CONTRA. Cambiar de algoritmo
      // reinicia el historial (no se mezcla entre modos).
      app.voiceLeadMode = (uint8_t)((app.voiceLeadMode + dir + VL_COUNT) % VL_COUNT);
      harmonyVoiceLeadReset();
      if (chordActive) playCurrent();
      break;
    case FX_JOYMAP:
      app.joyMap = (uint8_t)((app.joyMap + dir + JM_COUNT) % JM_COUNT);
      if (chordActive) playCurrent();
      break;
    case FX_CHORUS:
      app.chorus = !app.chorus;
      synthSetChorus(app.chorus, 0.7f, 7.0f, 0.4f);
      break;
    case FX_BODY:
      app.body = !app.body;
      synthSetBody(app.body, 0.9f);              // intensidad alta: que se ESCUCHE el toggle
      if (chordActive) playCurrent();            // re-disparo: arma el detune/transitorio por voz
      break;
    case FX_DELAY:
      menuDelay = (uint8_t)((menuDelay + dir + 5) % 5);
      if (menuDelay == 0) { dlyDepth = 0; synthDelaySet(250.0f, 0.0f, 0.0f); }
      else { dlyDiv = (uint8_t)(menuDelay - 1); dlyDepth = 2; applyDelay(); }
      break;
    case FX_TREMOLO:
      app.tremMode = (uint8_t)((app.tremMode + dir + 4) % 4);
      applyTremolo();
      break;
    case FX_VSYNTH: {                              // volumen de synth (voces): 0..100% (atenúa)
      float g = synthSynthGain() + dir * 0.1f;
      if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
      synthSetSynthGain(g);
      break;
    }
    case FX_VSUB: {                                // volumen del sub-bajo: 0..100% (atenúa)
      float g = synthBassGain() + dir * 0.1f;
      if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
      synthSetBassGain(g);
      break;
    }
    case FX_VDRUM: {                               // volumen de batería: 0..100%
      float g = synthDrumsGain() + dir * 0.1f;
      if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
      synthDrumsSetGain(g);
      break;
    }
    case FX_GLIDE:
      app.glide = !app.glide;
      synthSetGlide(app.glide, 220.0f);
      break;
    case FX_SUSTAIN:
      app.sustain = !app.sustain;
      synthSetSustain(app.sustain, 1500.0f);
      break;
    case FX_MONO:
      setMono(!app.mono);
      // La fila CANAL refleja el modo (MONO on == canal MELODÍA).
      app.channel = app.mono ? CH_MELODY : CH_CHORDS;
      break;
    case FX_CHAN:
      // Cicla ACORDES -> MELODÍA -> DRUMS (tocar cuerpos) -> PATRN (elegir
      // patrón). MELODÍA equivale a MONO on.
      app.channel = (uint8_t)((app.channel + dir + CH_COUNT) % CH_COUNT);
      setMono(app.channel == CH_MELODY);
      break;
    case FX_DGRB:
      drumRec = !drumRec;
      break;
    case FX_MET:
      app.metro = !app.metro;
      synthMetroSet(app.metro, (float)app.bpm);
      break;
    case FX_OCTAVE: {
      int o = (int)app.octaveOffset + dir;
      if (o < -2) o = -2; if (o > 2) o = 2;   // -2 = un buen sub-grave para el bajo
      app.octaveOffset = (int8_t)o;
      // Con el loop sonando NO re-dispara: el acorde vigente es del loop (tiene
      // su propia octava); el cambio se escucha en la próxima nota en vivo.
      if (chordActive && evloopState() != EVL_PLAY) playCurrent();
      break;
    }
    case FX_BPM: {
      int b = (int)app.bpm + dir * 5;
      if (b < 40) b = 40; if (b > 300) b = 300;
      app.bpm = (uint16_t)b;
      synthClockSet((float)app.bpm);              // reloj maestro primero (trem/drums/arp derivan de él)
      applyDelay();
      if (app.tremMode) applyTremolo();
      if (app.drumsOn)   synthDrumsSet(app.drumPat, (float)app.bpm);
      if (app.arpMode) applyArp();                // re-aplica el rate del arp al BPM nuevo
      if (app.metro)   synthMetroSet(true, (float)app.bpm);
      break;
    }
    case FX_MODE:
      app.mode = (app.mode == MODE_IONIAN) ? MODE_AEOLIAN : MODE_IONIAN;
      for (int i = 0; i < 7; i++) app.chordQual[i] = CQ_DIATONIC;  // cambia el contexto
      harmonyVoiceLeadReset();
      if (chordActive) playCurrent();
      break;
    case FX_CLOCK:
      app.clockOut = !app.clockOut;
      midiClockOutSet(app.clockOut);             // Start/Stop + arranca/para el clock
      break;
    case FX_DRUMS:
      // Toggle instantáneo: recuerda el patrón elegido (app.drumPat nunca es 0).
      app.drumsOn = !app.drumsOn;
      synthDrumsSet(app.drumsOn ? app.drumPat : 0, (float)app.bpm);
      break;
    case FX_DRUMPAT:
      // Elige el patrón 1..DRUM_PATTERNS-1 (el 0 "off" ya no es una posición:
      // apagar es la fila DRUMS). Si la batería suena, cambia en vivo.
      app.drumPat = (uint8_t)(((app.drumPat - 1) + dir + (DRUM_PATTERNS - 1)) % (DRUM_PATTERNS - 1) + 1);
      if (app.drumsOn) synthDrumsSet(app.drumPat, (float)app.bpm);
      break;
    case FX_ARP:
      app.arpMode = app.arpMode ? 0 : 1;           // toggle on/off (slot elegido en PRE)
      if (app.arpMode) {
        if (app.strum) { app.strum = false; synthSetStrum(false, 0); }   // excluyentes
        if (app.mono)  { app.mono = false; synthSetMono(false); }
      }
      applyArp();
      if (chordActive) { synthReleaseAll(); playCurrent(); }
      break;
    case FX_ARPSEL:
      app.arpSlot = (uint8_t)((app.arpSlot + dir + ARP_SLOTS) % ARP_SLOTS);
      if (app.arpMode) { applyArp(); if (chordActive) playCurrent(); }
      break;
    case FX_RANDOM:
      synthRandomizeSound();
      if (chordActive) { synthReleaseAll(); playCurrent(); }
      break;
    case FX_LREC: {
      // EMPTY -> arma la grabación (el 1er acorde define el paso 0);
      // REC -> cierra la toma (redondea a compás) y pasa a PLAY;
      // PLAY -> alterna el OVERDUB (grabar melodía/capas sobre el loop).
      bool wasRec = (evloopState() == EVL_REC);
      evloopRecToggle(loopPos());
      // Al cerrar la toma, el loop captura octava Y timbre vigentes: OCTAVE y
      // TIMBRE pasan a ser controles de la mano en vivo, sin tocar el loop.
      if (wasRec && evloopState() == EVL_PLAY) {
        loopOct = app.octaveOffset;
        loopTimbre = synthTimbre();
      }
      if (evloopOverdub()) melTimbre = synthTimbre();   // timbre de la capa nueva
      break;
    }
    case FX_LPLAY:
      evloopPlayToggle(loopPos());
      if (evloopState() == EVL_STOP && !app.hold) {   // STOP: corta acorde + melodía del loop
        chordActive = false; synthReleaseChord(); midiOut(nullptr, 0);
        if (loopMelNote >= 0) { synthNoteOff((uint8_t)loopMelNote); loopMelNote = -1; }
      }
      break;
    case FX_LCLR:
      evloopClear();
      if (!app.hold) { chordActive = false; synthReleaseChord(); midiOut(nullptr, 0); }
      if (loopMelNote >= 0) { synthNoteOff((uint8_t)loopMelNote); loopMelNote = -1; }
      break;
    case FX_SAVE:
      // Persiste TODO en el equipo: timbres, patrones, arps, nombres y la sesión.
      // COOLDOWN: es una acción de escritura PESADA a NVS. Sin este candado, el
      // auto-repeat del joystick (o un stick inclinado) la machacaría cada 150 ms
      // -> watchdog/reinicio. Máx. una vez por segundo. El "ok!" confirma que escribió.
      if (millis() - fxSavedMs > 1000) { webcfgSave(); fxSavedMs = millis(); }
      break;
    case FX_LOAD:
      // COOLDOWN igual que GUARDAR: una carga por segundo como máximo.
      if (millis() - fxLoadedMs > 1000) {
        fxLoadedMs = millis();
        if (webcfgLoad()) {
          synthSetTimbre((uint8_t)webcfgSessionTimbre(synthTimbre()));
          applySession();
          if (chordActive) { synthReleaseAll(); playCurrent(); }
        }
      }
      break;
  }
  char v[12]; fxValue(it, v);
  Serial.printf("[fx] %s = %s\n", FX_NAME[it], v);
}

// ============================ ARRANQUE (identidad patagónica) ===============
// ZOOM de 3 segundos (3 saltos por segundo): la Argentina entera (con Malvinas)
// -> Patagonia -> isla de Tierra del Fuego, en 9 frames pre-renderizados
// (boot_zoom.h; el rombo de Tolhuin viene horneado en los frames finales).
// Los últimos 4 zooms llevan el arpegio de Do menor (C3 Eb3 G3 Bb3, un escalón
// por salto) y al clavarse el último frame entra la 9na (D4) junto con el cartel
// "TOLHUIN mk1 v2026". Las graves se cortan, la 9na canta sola y termina.
// Timbre EPIANO. Partitura por millis() (el audio vive en el core 0, nunca se
// corta). CUALQUIER botón de grado saltea la intro. Al salir, setup() restaura
// STRINGS para tocar.
static void bootChime() {
  // Partitura del jingle: eventos ordenados por tiempo (vel 0 = note off).
  // Editar el motivo = editar SOLO esta tabla.
  struct Ev { uint16_t ms; uint8_t note, vel; };
  static const Ev SEQ[] = {
    { 1667, 48,  92 },                             // C3  } un escalón del
    { 2000, 51,  92 },                             // Eb3 } arpegio por cada
    { 2333, 55,  92 },                             // G3  } zoom final
    { 2667, 58,  92 },                             // Bb3 }
    { 3000, 62, 104 },                             // D4: la 9na, con el cartel
    { 3400, 48, 0 }, { 3400, 51, 0 },              // cortan las graves:
    { 3400, 55, 0 }, { 3400, 58, 0 },              //   la 9na canta sola
    { 4100, 62, 0 },                               // release (reverb estira el final)
  };
  const int      NEV = sizeof(SEQ) / sizeof(SEQ[0]);
  const uint32_t STEP_MS = 333, T_TITLE = 3000, T_END = 4400;

  synthSetTimbre(T_EPIANO);                        // timbre del jingle
  int      evIdx = 0;
  uint32_t t0 = millis(), lastSig = 0xFFFFFFFFu;
  for (;;) {
    uint32_t el = millis() - t0;

    // La partitura manda: disparar todos los eventos vencidos.
    while (evIdx < NEV && el >= SEQ[evIdx].ms) {
      const Ev& e = SEQ[evIdx++];
      if (e.vel) synthNoteOn(e.note, e.vel); else synthNoteOff(e.note);
    }

    // Redibujar SOLO si el frame cambió (display() bloquea ~25 ms de I2C) y
    // nunca justo antes (<30 ms) de un evento musical: primero la partitura.
    uint32_t sig = (el < T_TITLE) ? (0x100u | (el / STEP_MS)) : 0x200u;
    bool evSoon = (evIdx < NEV) && (SEQ[evIdx].ms - el < 30);
    if (sig != lastSig && !evSoon) {
      lastSig = sig;
      if (sig & 0x100u) oledBootZoom((uint8_t)(sig & 0xFFu));
      else              oledBootTitle();
    }

    // Un botón de grado saltea la intro (quedás listo para tocar al instante).
    inputUpdate();
    if (inputPressedDegree() >= 0) break;

    if (el >= T_END) break;
    delay(1);                                      // cede CPU (WDT / BLE / I2C)
  }
  synthReleaseAll();
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  // El sufijo de versión sirve para confirmar de un vistazo QUÉ firmware corre la
  // placa: si no ves "fw=tolhuin-3.2" al arrancar, el flasheo no tomó.
  Serial.println("\n[boot] TOLHUIN CHORD v2 - fw=tolhuin-4.0 (pantalla v2: zonas fijas + scope trigger; looper de eventos; timbre WAVE)");

  // Estado inicial: Do mayor, tríada, sin hold.
  app.tonic = 0;
  app.mode  = MODE_IONIAN;
  app.zone  = CZ_CENTER;
  app.voicing = VC_CLOSE;
  app.octaveOffset = 0;
  app.hold = false;
  app.strum = false;
  app.arpMode = 0;
  app.arpSlot = 0;
  app.bpm = 120;
  app.mono = false;
  app.glide = false;
  app.sustain = false;
  app.chorus = true;             // ensemble encendido (carácter HiChord, STRINGS ancho)
  app.body = false;
  app.subBass = true;            // sub-bajo sine dedicado (el "bass role")
  app.clockOut = false;
  app.drumsOn = false;
  app.drumPat = 1;               // patrón elegido (básico); DRUMS lo prende/apaga
  app.tremMode = 0;
  app.preset = HP_BASIC;
  app.joyMap = JM_DEFAULT;       // mapa original de TOLHUIN (fila JOY lo cambia)
  app.bassDegree = -1;
  for (int i = 0; i < 7; i++) app.chordQual[i] = CQ_DIATONIC;
  app.channel = CH_CHORDS;       // los switches tocan acordes (fila CANAL lo cambia)
  app.voiceLeadMode = VL_SMOOTH; // conducción histórica por defecto (fila LEAD)
  app.metro = false;             // metrónomo apagado (fila MET de SYNC)

  if (!synthInit()) {
    Serial.println("[boot] HALT: revisar cableado/pines I2S del PCM5102");
    while (true) { delay(1000); }
  }

  webcfgInit();                    // timbres/patrones/arps + SESIÓN de NVS -> app

  inputInit();
  midiInit();                      // USB MIDI + BLE MIDI ("TOLHUIN")
  oledInit();                      // pantalla lista para la animación de arranque

  bootChime();                     // jingle + animación (reemplaza el autotest seco)

  // El equipo arranca COMO QUEDÓ: timbre y toggles de la sesión guardada
  // (GUARDAR en el menú, o "Guardar en NVS" de la web). Sin sesión: STRINGS.
  synthSetTimbre((uint8_t)webcfgSessionTimbre(T_STRINGS));
  applySession();
  app.zone = inputColor();
  Serial.println("[boot] listo -> tocá un grado");
}

void loop() {
  inputUpdate();
  webcfgPoll();                    // comandos del editor web por USB (Serial)
  midiUpdate();                    // procesa MIDI USB/BLE entrante
  midiClockOutUpdate(app.bpm);     // si está activo, emite el clock (master de tempo)

  // Diagnóstico del motor: si algún bloque de audio se pasó del presupuesto
  // (~5.8 ms), avisar por serial (cada 5 s como mucho). Es el "ruido al soltar".
  {
    static uint32_t perfMs = 0, lateSeen = 0;
    uint32_t nowP = millis();
    if (nowP - perfMs >= 5000) {
      perfMs = nowP;
      uint32_t mx, late;
      synthPerfStats(&mx, &late, false);         // #perf reset controla la ventana
      if (late < lateSeen) lateSeen = 0;         // reset explícito desde la web/USB
      if (late != lateSeen) {
        Serial.printf("[perf] bloques tarde=%lu (+%lu)  peor=%lu us (presup. 5805)\n",
                      (unsigned long)late, (unsigned long)(late - lateSeen),
                      (unsigned long)mx);
        lateSeen = late;
      }
    }
  }

  // --- Sync IN del arp: con un clock MIDI externo vivo, el arp avanza fase-
  //     bloqueado a los ticks (no a su reloj interno). 12 ticks = corchea. ---
  {
    static bool     arpExt = false;
    static uint32_t arpStepPrev = 0xFFFFFFFFu;
    bool wantExt = app.arpMode && midiClockLive() && !app.clockOut;   // slave sólo si no somos master
    if (wantExt != arpExt) { arpExt = wantExt; synthArpExtSync(arpExt); arpStepPrev = 0xFFFFFFFFu; }
    if (arpExt) {
      uint32_t step = midiClockTickCount() / 12;   // stepsPerBeat=2 -> 24/2 = 12 ticks por paso
      if (step != arpStepPrev) { arpStepPrev = step; synthArpAdvance(); }
    }
  }

  // --- Sync IN del RELOJ MAESTRO (drums/trémolo/looper): con un clock MIDI
  //     externo vivo, la FASE del reloj se realinea a los ticks (96/compás 4/4)
  //     una vez por pulso -> el "1" del motor cae en el "1" externo, igual que el
  //     arp. Entre pulsos el reloj corre suave por su cuenta; el Start (0xFA)
  //     reinicia el contador de ticks, así el realineado lo lleva al "1". ---
  {
    static uint32_t clkBeatPrev = 0xFFFFFFFFu;
    if (midiClockLive() && !app.clockOut) {          // esclavamos sólo si no somos master
      uint32_t ticks = midiClockTickCount();
      uint32_t beat  = ticks / 24;                   // 24 ticks = 1 pulso
      if (beat != clkBeatPrev) { clkBeatPrev = beat; synthClockSyncTicks(ticks % 96); }
    } else {
      clkBeatPrev = 0xFFFFFFFFu;                      // sin clock externo: vuelve al reloj interno
    }
  }

  // MIDI clock externo: sincroniza el tempo del delay y del arp.
  if (midiClockChanged()) {
    int bpm = (int)(midiClockBpm() + 0.5f);
    if (bpm >= 40 && bpm <= 300) {
      app.bpm = (uint16_t)bpm;
      synthClockSet((float)app.bpm);            // reloj maestro = fuente única de tempo
      applyDelay();
      if (app.tremMode) applyTremolo();
      if (app.drumsOn) synthDrumsSet(app.drumPat, (float)app.bpm);
      if (app.arpMode) applyArp();
      if (app.metro)   synthMetroSet(true, (float)app.bpm);
    }
  }

  // --- LOOPER de eventos: en PLAY re-dispara los acordes grabados por el camino
  //     normal (playCurrent) -> suenan con el timbre/tonalidad/octava ACTUALES.
  //     Tocar en vivo por encima simplemente pisa el acorde del loop. ---
  if (evloopState() == EVL_PLAY) {
    EvLoopEvent evb[8];
    int k = evloopTick(loopPos(), evb, 8);
    for (int i = 0; i < k; i++) {
      switch (evb[i].type) {
        case EV_CHORD_ON:
          app.degree = evb[i].degree;
          app.zone = (ColorZone)evb[i].zone;
          app.bassDegree = evb[i].bassDegree;
          app.chordQual[app.degree % 7] = evb[i].qual;  // restaura la mayorización grabada
          chordActive = true;
          playCurrent(true);                            // acordes SIEMPRE (aunque MONO esté on)
          break;
        case EV_CHORD_OFF:
          if (!app.hold) {                              // OFF del loop (salvo HOLD en vivo)
            chordActive = false;
            synthReleaseChord();                        // conserva la melodía mono en vivo
            midiOut(nullptr, 0);
          }
          break;
        case EV_MEL_ON:                                 // melodía grabada: nota MIDI absoluta,
          if (loopMelNote >= 0) synthNoteOff((uint8_t)loopMelNote);   // legato del loop
          synthNoteOnT(evb[i].degree, 110, melTimbre);  // con el timbre de SU capa
          loopMelNote = evb[i].degree;
          break;
        case EV_MEL_OFF:
          if (loopMelNote >= 0) { synthNoteOff((uint8_t)loopMelNote); loopMelNote = -1; }
          break;
      }
    }
  }

  // timeout del menú de config: se va solo tras ~5 s sin tocar nada (margen para
  // navegar con NAV y después editar con VOICING sin que se cierre).
  // GRABANDO el looper el menú se queda: tocás la vuelta viendo el compás en la
  // fila REC y cerrás la toma con otro tap de VOICING (si no, desaparecía a
  // mitad de la grabación y no se entendía cómo pararla).
  if (evloopState() == EVL_REC && menuActive) menuLastMs = millis();
  if (menuActive && (millis() - menuLastMs) > 8000) { menuActive = false; inputSetMenuMode(false); }

  // --- SW del joystick (capa ARMÓNICA; sólo el click, el stick no interviene):
  //   click CON un grado apretado  -> mayoriza ESE grado (dom7 <-> diatónico)
  //   click SIN ningún grado apretado -> alterna modo MAYOR / menor
  //   El menú de config NO usa el SW: se navega con NAV y se edita con VOICING. ---
  if (!menuActive && inputJoySwPressed()) {
    if (inputHeldMask()) {                          // hay un acorde apretado -> mayorizar
      uint8_t g = app.degree % 7;
      app.chordQual[g] = (app.chordQual[g] == CQ_DOM7) ? CQ_DIATONIC : CQ_DOM7;
      if (chordActive) playCurrent();
      Serial.printf("[play] grado %u -> %s\n", app.degree,
                    app.chordQual[g] == CQ_DOM7 ? "DOM7" : "diatonico");
    } else {                                        // sin acorde apretado -> alternar modo
      app.mode = (app.mode == MODE_IONIAN) ? MODE_AEOLIAN : MODE_IONIAN;
      for (int i = 0; i < 7; i++) app.chordQual[i] = CQ_DIATONIC;   // cambia el contexto
      harmonyVoiceLeadReset();
      if (chordActive) playCurrent();
      Serial.printf("[play] MODO %s\n", app.mode == MODE_IONIAN ? "MAYOR" : "menor");
    }
  }

  // --- HOLD: TAP sostiene el acorde ; LARGO activa/desactiva STRUM. ---
  int he = inputHoldEvent();
  if (he == 1) {
    app.hold = !app.hold;
    if (!app.hold) { chordActive = false; synthReleaseAll(); }
    Serial.printf("[play] HOLD %s\n", app.hold ? "ON" : "OFF");
  } else if (he == 2) {
    app.strum = !app.strum;
    if (app.strum && app.arpMode) { app.arpMode = 0; applyArp(); }   // excluyentes
    synthSetStrum(app.strum, 30.0f);
    if (chordActive) { synthReleaseAll(); playCurrent(); }
    Serial.printf("[play] STRUM %s\n", app.strum ? "ON" : "OFF");
  }

  // --- VOICING (libre del menú): TAP cicla la inversión, LARGO togglea el
  //   arpegiador. (La edición del menú ahora la hace el joystick ←/→.) ---
  int ve = inputVoicingEvent();
  if (ve == 1) {
    app.voicing = (Voicing)((app.voicing + 1) % VC_COUNT);
    if (chordActive) playCurrent();
    Serial.printf("[play] voicing %u\n", app.voicing);
  } else if (ve == 2) {
    app.arpMode = app.arpMode ? 0 : 1;              // toggle on/off con el slot activo
    if (app.arpMode) {
      if (app.strum) { app.strum = false; synthSetStrum(false, 0); }   // excluyentes
      if (app.mono)  { app.mono = false; synthSetMono(false); }
    }
    applyArp();
    if (chordActive) { synthReleaseAll(); playCurrent(); }
    Serial.printf("[play] ARP %s (slot %u '%s')\n", app.arpMode ? "ON" : "OFF",
                  app.arpSlot, synthPresetName(1, app.arpSlot));
  }

  // --- MENÚ v3: NAV+ (derecha) entra/sale del menú ; NAV- (izquierda) togglea
  //   drums on/off. Dentro del menú, el joystick (↑/↓) mueve la selección y
  //   (←/→) edita el valor (auto-repeat). ---
  if (inputNavMenuToggle()) {                                 // NAV+ = entra/sale del menú
    menuActive = !menuActive;
    inputSetMenuMode(menuActive);
    menuLastMs = millis();
    Serial.printf("[menu] %s\n", menuActive ? "ABIERTO" : "cerrado");
  }
  if (inputNavDrumsToggle()) {                                // NAV- = drums on/off
    app.drumsOn = !app.drumsOn;
    synthDrumsSet(app.drumsOn ? app.drumPat : 0, (float)app.bpm);
    Serial.printf("[play] DRUMS %s\n", app.drumsOn ? "ON" : "OFF");
  }
  if (menuActive) {
    int nav = inputMenuNav();                                 // joystick ↑/↓ mueve la selección
    if (nav) {
      fxSel = ((fxSel + nav) % FX_COUNT + FX_COUNT) % FX_COUNT;
      menuLastMs = millis();
      Serial.printf("[menu] %s\n", FX_NAME[fxSel]);
    }
    int ed = inputMenuEdit();                                 // joystick ←/→ edita el valor
    if (ed) { fxApply(fxSel, ed > 0 ? +1 : -1); menuLastMs = millis(); }
  }

  // --- joystick: cambia el COLOR del acorde que ya suena. ---
  if (inputColorChanged()) {
    app.zone = inputColor();
    if (chordActive) {
      playCurrent();
      // El cambio de color con acorde sonando también entra a la toma.
      evloopRecord(loopPos(), EV_CHORD_ON, app.degree, (uint8_t)app.zone,
                   app.chordQual[app.degree % 7], app.bassDegree);
    }
    Serial.printf("[play] color %u\n", app.zone);
  }

  // --- grado: según el CANAL, toca acordes, cuerpos de batería o patrones. ---
  int d = inputPressedDegree();
  if (d >= 0 && app.channel == CH_DRUMS) {
    // Canal DRUMS: sw1=bombo sw2=caja sw3=hi-hat (4..7 reservados para open
    // hat/crash futuros). Con GRB on y el patrón sonando, el golpe se GRABA
    // cuantizado al paso más cercano (overdub estilo TR).
    if (d < 3) {
      synthDrumHit(d, drumRec);
      Serial.printf("[play] hit %s%s\n", d == 0 ? "bombo" : d == 1 ? "caja" : "hat",
                    drumRec ? " (grabado)" : "");
    }
  } else if (d >= 0 && app.channel == CH_DRUMPAT) {
    // Canal PATRN: switch 1..7 = patrón 1..7 (básico/4floor/síncopa/user1..4),
    // cambia en vivo y enciende la batería si estaba apagada.
    app.drumPat = (uint8_t)(d + 1);
    app.drumsOn = true;
    synthDrumsSet(app.drumPat, (float)app.bpm);
    Serial.printf("[play] drums -> patron %u '%s'\n", app.drumPat,
                  synthPresetName(0, app.drumPat));
  } else if (d >= 0) {
    // ¿hay OTRO grado sostenido? -> es el bajo (slash / inversión / pedal).
    uint8_t mask = inputHeldMask();
    app.bassDegree = -1;
    for (int b = 0; b < 7; b++) if (b != d && (mask & (1 << b))) { app.bassDegree = (int8_t)b; break; }
    app.degree = (uint8_t)d;
    app.zone   = inputColor();
    chordActive = true;
    playCurrent();
    // Si el looper está grabando (toma u overdub), esto entra cuantizado:
    // acorde en canal ACORDES, nota de melodía (MIDI absoluta) en MONO.
    if (app.mono) evloopRecord(loopPos(), EV_MEL_ON, lastMelNote, 0, 0, -1);
    else          evloopRecord(loopPos(), EV_CHORD_ON, app.degree, (uint8_t)app.zone,
                               app.chordQual[app.degree % 7], app.bassDegree);
    if (app.bassDegree >= 0) Serial.printf("[play] grado %u / bajo %d\n", app.degree, app.bassDegree);
    else                     Serial.printf("[play] grado %u\n", app.degree);
  }

  // --- soltar todos los grados: si NO está en HOLD, libera. ---
  //   SOLO aplica en los canales que tocan notas (ACORDES/MELODÍA): en DRUMS y
  //   PATRN los switches son golpes/selección — soltar uno NO debe cortar el
  //   acorde del loop (cada golpe de batería mataba el acorde al soltarse).
  //   En MONO con el loop sonando: soltar la melodía suelta SOLO la voz mono.
  if (inputAllReleased() && !app.hold &&
      app.channel != CH_DRUMS && app.channel != CH_DRUMPAT) {
    if (app.mono && evloopState() == EVL_PLAY) {
      synthMonoOff();
    } else {
      chordActive = false;
      synthReleaseAll();
      midiOut(nullptr, 0);     // note-off MIDI de todo
    }
    // El silencio también se graba (OFF de melodía en MONO, de acorde si no).
    if (app.mono) evloopRecord(loopPos(), EV_MEL_OFF, lastMelNote, 0, 0, -1);
    else          evloopRecord(loopPos(), EV_CHORD_OFF, 0, 0, 0, -1);
  }

  // --- OLED: menú de config (NAV/VOICING lo despiertan) > pantalla normal. ---
  static uint32_t uiSig = 0xFFFFFFFFu;
  static uint32_t lastFrame = 0;
  uint32_t nowMs = millis();

  // --- Menú de config visible: ítem + valor. ---
  if (menuActive) {
    if ((nowMs - lastFrame) >= 50) {
      lastFrame = nowMs;
      // Página que contiene el ítem seleccionado + valores de todas sus filas.
      int pg = 0;
      for (int i = 0; i < FX_NPAGES; i++)
        if (fxSel >= FX_PAGES[i].first && fxSel < FX_PAGES[i].first + FX_PAGES[i].count) pg = i;
      const FxPage& P = FX_PAGES[pg];
      const char* nm[3]; const char* vl[3];
      static char vbuf[3][12];
      for (int r = 0; r < P.count && r < 3; r++) {
        nm[r] = FX_SHORT[P.first + r];
        fxValue(P.first + r, vbuf[r]);
        vl[r] = vbuf[r];
      }
      // El orden de IC_PAGE (icons.h) sigue a FX_PAGES: pg indexa directo.
      oledFxMenuList(P.title, pg, FX_NPAGES, nm, vl, P.count, fxSel - P.first,
                     (pg < (int)(sizeof(IC_PAGE) / sizeof(IC_PAGE[0]))) ? IC_PAGE[pg] : nullptr);
    }
    uiSig = 0xFFFFFFFFu;                          // fuerza redibujo normal al salir del menú
    delay(1);
    return;
  }

  uint32_t sig = (uint32_t)app.degree
               | ((uint32_t)app.zone << 3)
               | ((uint32_t)app.tonic << 7)
               | ((uint32_t)app.mode << 11)
               | ((uint32_t)app.voicing << 12)
               | ((uint32_t)((app.octaveOffset + 2) & 7) << 14)
               | ((uint32_t)app.preset << 17)
               | ((uint32_t)(app.hold ? 1 : 0) << 19)
               | ((uint32_t)(app.glide ? 1 : 0) << 20)
               | ((uint32_t)(app.mono ? 1 : 0) << 21)
               | ((uint32_t)app.arpMode << 22)
               | ((uint32_t)((app.bassDegree + 1) & 7) << 24)
               | ((uint32_t)synthTimbre() << 27)
               | ((uint32_t)(chordActive ? 1 : 0) << 30)
               | ((uint32_t)(midiBleConnected() ? 1 : 0) << 31);
  // el BPM y los flags extra no entran en la firma de bits; los mezclo aparte
  sig ^= ((uint32_t)app.bpm * 2654435761u);
  if (app.sustain) sig ^= 0x5A5A5A5Au;
  if (app.chorus)  sig ^= 0x3C3C3C3Cu;
  if (app.body)    sig ^= 0x6699AACCu;
  sig ^= (uint32_t)app.tremMode * 0x9E3779B9u;
  sig ^= (uint32_t)(app.arpMode * (app.arpSlot + 1)) * 0x85EBCA6Bu;
  // Redibuja al cambiar algo mostrado, o a ~15 fps para animar el VU.
  if (sig != uiSig || (nowMs - lastFrame) >= 66) {
    uiSig = sig;
    lastFrame = nowMs;
    oledRender(app, synthTimbreName(synthTimbre()), midiBleConnected(),
               synthGetLevel(), loopPos(), chordActive);
  }

  delay(1);
}
