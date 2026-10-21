// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

#pragma once
#include "state.h"   // ColorZone, Voicing, Mode
#include "config.h"  // MAX_CHORD_NOTES

// Presets de riqueza armónica (los cicla el control SW+grado).
enum HarmonyPreset {
    HP_BASIC = 0,   // tríada (+ fundamental duplicada, en el sketch)
    HP_SEVENTH,     // + 7ma diatónica (no dominante)
    HP_NINTH,       // + 7ma + 9na
    HP_SIXNINE,     // + 6ta + 9na (sin 7ma, etéreo)
    HP_COUNT
};

// Igual que harmonyChord pero suma las extensiones del preset (encima del color),
// deduplicando clases de altura y respetando las reglas duras. Devuelve n.
uint8_t harmonyChordPreset(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                           uint8_t preset, uint8_t* notes);

// Como harmonyChordPreset, pero `qual` puede FORZAR la calidad del grado
// (edición armónica opt-in): con CQ_DOM7 arma un dominante 7 (R 3M 5J b7,
// + 9na en zonas "up") ignorando la regla diatónica y el preset. Con
// CQ_DIATONIC delega en harmonyChordPreset. Devuelve n.
uint8_t harmonyChordQual(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                         uint8_t preset, uint8_t qual, uint8_t* notes);

// Punto de entrada con MAPA DE JOYSTICK (fila JOY del menú). Con JM_DEFAULT (o
// con `qual` forzado) delega en harmonyChordQual; con JM_HICHORD/JM_MINI arma el
// acorde según la tabla de ese instrumento (cromático opt-in, ver state.h).
// La raíz SIEMPRE es la diatónica del grado. Devuelve n.
uint8_t harmonyChordJoy(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                        uint8_t preset, uint8_t qual, uint8_t joyMap,
                        uint8_t* notes);

// Construye el acorde diatónico para el grado `deg` (0-6) en la tonalidad
// `tonic` (0-11) y `mode`. La zona de color agrega extensiones seguras.
// Devuelve la cantidad de notas escritas en `notes` (1..MAX_CHORD_NOTES).
// REGLAS DURAS garantizadas:
//   - todas las notas son diatónicas
//   - no dominante (no 3ra mayor + b7 simultáneas)
//   - sin b9 (intervalo 13 sobre la raíz), sin b2 (intervalo 1)
uint8_t harmonyChord(uint8_t deg, ColorZone z,
                     uint8_t tonic, Mode mode,
                     uint8_t* notes);

// Secuencia de alturas del ARPEGIADOR (pura, testeable). A partir del acorde
// `notes[0..n-1]`, produce la lista ASCENDENTE de MIDI que el arp recorrerá:
//  - `octaves` (1..4): replica el material hacia arriba esa cantidad de octavas.
//  - `scaleRun=false`: usa las notas del acorde (con sus octavas).
//  - `scaleRun=true`: usa la ESCALA diatónica (tonic+mode) completa desde la nota
//    más grave del acorde hacia arriba (corredor de escala, notas de paso).
// Escribe hasta `cap` notas en `out` (ascendentes, sin duplicados). Devuelve n.
uint8_t harmonyArpSequence(const uint8_t* notes, uint8_t n, uint8_t octaves,
                           bool scaleRun, uint8_t tonic, Mode mode,
                           uint8_t* out, uint8_t cap);

// Reinicia el historial de voice-leading (llamar al cambiar tonalidad).
void harmonyVoiceLeadReset();

// Aplica voice-leading a `notes[0..n-1]` in-place: mantiene las voces
// dentro del registro MIDI_LO..MIDI_HI y minimiza el salto del bajo.
// Devuelve la cantidad de notas (puede ser igual a n o menor en borde).
uint8_t harmonyVoiceLead(uint8_t* notes, uint8_t n);

// Transpone todas las notas por `octave` octavas (12 semitonos c/u), in-place.
// Clamp a MIDI 0..127. Devuelve n. (Octava global -1..+2 del AppState.)
uint8_t harmonyApplyOctave(uint8_t* notes, uint8_t n, int8_t octave);

// Reordena las voces según `v` (inversiones / abierta), in-place. Conserva las
// clases de altura del acorde; sólo cambia octavas de algunas voces. Devuelve n.
// Aplicar DESPUÉS de harmonyVoiceLead y ANTES de harmonyApplyOctave.
uint8_t harmonyApplyVoicing(uint8_t* notes, uint8_t n, Voicing v);

// --- Nombres para la pantalla (con enarmonía según la tonalidad) ---
const char* harmonyRootName(uint8_t degree, uint8_t tonic, Mode mode);  // "C","Eb","F#"...
const char* harmonyModeName(Mode mode);                                 // "MAJ" / "min"
// Símbolo del acorde (ej. "Dm7","Cadd9","Gsus4","Am9","Cmaj9") según zona Y preset.
void        harmonyChordSymbol(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                               uint8_t preset, char* out, int cap);
// Igual, pero contempla la calidad forzada por grado (CQ_DOM7 -> "D7", "A7"...).
void        harmonyChordSymbolQual(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                                   uint8_t preset, uint8_t qual, char* out, int cap);
// Igual, pero contemplando además el mapa de joystick activo (JM_*).
void        harmonyChordSymbolJoy(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                                  uint8_t preset, uint8_t qual, uint8_t joyMap,
                                  char* out, int cap);
