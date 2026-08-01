// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   harmony.cpp  -  Motor de armonía diatónica (sin hardware).
   Compilable en host (g++/clang++) y en Arduino/ESP32.

   REGLAS DURAS (jamás romper):
     1) Todas las notas son diatónicas a tonic+mode.
     2) Ningún acorde es dominante (3ra mayor + b7 simultáneas).
     3) No b9 (intervalo 13 sobre la raíz), no b2 (intervalo 1).
     4) Cantidad de notas en [1, MAX_CHORD_NOTES].
     5) Voice leading: registro 36-91, salto de bajo ≤ 11 semitonos.
   ============================================================================ */
#include "harmony.h"

#ifdef ARDUINO
#  include <Arduino.h>
#else
#  include <cstdlib>   // abs
#endif
#include <stdio.h>     // snprintf
#include <string.h>    // strncpy

// ---------------------------------------------------------------------------
// Escalas (intervalos desde la tónica en semitonos)
// ---------------------------------------------------------------------------
static const int SC_MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 };   // Jónico / Mayor
static const int SC_MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };   // Eólico / Menor nat.

static const int* pickScale(Mode mode) {
    return (mode == MODE_AEOLIAN) ? SC_MIN : SC_MAJ;
}

// ---------------------------------------------------------------------------
// Intervalo ascendente (semitonos) desde scale[a%7] hasta scale[b%7]
// ---------------------------------------------------------------------------
static int stepInterval(const int* sc, int a, int b) {
    int diff = (sc[b % 7] - sc[a % 7] + 12) % 12;
    return (diff == 0) ? 12 : diff;   // 0 → una octava arriba
}

// ---------------------------------------------------------------------------
// harmonyChord
// ---------------------------------------------------------------------------
uint8_t harmonyChord(uint8_t deg, ColorZone z,
                     uint8_t tonic, Mode mode,
                     uint8_t* notes)
{
    const int* sc = pickScale(mode);

    // Raíz en octava 3 (MIDI 48-59), justo bajo el Do central
    int rootPc   = (tonic + sc[deg % 7]) % 12;
    int rootMidi = rootPc + 48;

    // Intervalos diatónicos sobre la raíz.
    int iv3 = stepInterval(sc, deg, deg + 2);     // 3ra (3 o 4)
    int iv5 = stepInterval(sc, deg, deg + 4);     // 5ta (6 o 7)
    int abs3 = iv3;
    int abs5 = (iv5 > abs3) ? iv5 : iv3 + stepInterval(sc, deg + 2, deg + 4);

    int iv2 = (sc[(deg + 1) % 7] - sc[deg % 7] + 12) % 12; if (iv2 == 0) iv2 = 12;   // 2da
    int iv4 = (sc[(deg + 3) % 7] - sc[deg % 7] + 12) % 12; if (iv4 == 0) iv4 = 12;   // 4ta
    int iv6 = (sc[(deg + 5) % 7] - sc[deg % 7] + 12) % 12; if (iv6 == 0) iv6 = 12;   // 6ta
    int iv7 = (sc[(deg + 6) % 7] - sc[deg % 7] + 12) % 12; if (iv7 == 0) iv7 = 12;   // 7ma
    int iv9raw = (sc[(deg + 1) % 7] - sc[deg % 7] + 12) % 12;
    int iv9 = (iv9raw == 0) ? 14 : iv9raw + 12;   // 9na (2da + octava)

    bool esDom = (abs3 == 4) && (iv7 == 10);       // 3ra mayor + b7 = dominante

    uint8_t n = 0;
    notes[n++] = (uint8_t)rootMidi;

    // Agrega rootMidi+iv si no es b2/b9, no duplica clase de altura y hay lugar.
    auto add = [&](int iv) {
        if (iv <= 0 || iv == 1 || iv == 13) return;
        int pc = (rootMidi + iv) % 12;
        for (uint8_t i = 0; i < n; i++) if (notes[i] % 12 == pc) return;
        if (n < MAX_CHORD_NOTES) notes[n++] = (uint8_t)(rootMidi + iv);
    };

    // --- 8 colores distintos + centro (todos diatónicos, sin dominante/b9/b2) ---
    switch (z) {
        case CZ_DOWN_LEFT:                          // sus2: R 2 5
            add(iv2); add(abs5); break;
        case CZ_LEFT:                               // sus4: R 4 5
            add(iv4); add(abs5); break;
        case CZ_UP_LEFT:                            // 7sus4: R 4 5 7 (sin 3ra -> nunca dominante)
            add(iv4); add(abs5); add(iv7); break;
        default:                                    // base = tríada
            add(abs3); add(abs5);
            switch (z) {
                case CZ_UP:        add(iv9); break;             // add9
                case CZ_UP_RIGHT:  add(iv6); add(iv9); break;   // 6/9
                case CZ_DOWN:      add(iv6); break;             // 6
                case CZ_RIGHT:                                  // 7ma (en V -> 6ta)
                    if (!esDom) add(iv7); else add(iv6);
                    break;
                case CZ_DOWN_RIGHT:                              // 9na (en V -> 6/9)
                    if (!esDom) add(iv7); else add(iv6);
                    add(iv9); break;
                case CZ_CENTER:
                default: break;                                 // tríada pura
            }
            break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// harmonyChordPreset: triada + color (harmonyChord) + extensiones del preset.
// Deduplica clases de altura y respeta las reglas (no dominante, no b9/b2).
// La "fundamental duplicada" del preset BÁSICO se agrega en el sketch (tras el
// voice leading) como refuerzo de bajo.
// ---------------------------------------------------------------------------
uint8_t harmonyChordPreset(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                           uint8_t preset, uint8_t* notes) {
    uint8_t n = harmonyChord(deg, z, tonic, mode, notes);   // triada + color

    const int* sc = pickScale(mode);
    int rootMidi = (tonic + sc[deg % 7]) % 12 + 48;
    int abs3 = stepInterval(sc, deg, deg + 2);               // 3 o 4 semitonos

    // Intervalos diatónicos (compuestos sobre la raíz).
    int iv7 = (sc[(deg + 6) % 7] - sc[deg % 7] + 12) % 12; if (iv7 == 0) iv7 = 12;
    int iv6 = (sc[(deg + 5) % 7] - sc[deg % 7] + 12) % 12; if (iv6 == 0) iv6 = 12;
    int iv9raw = (sc[(deg + 1) % 7] - sc[deg % 7] + 12) % 12;
    int iv9 = (iv9raw == 0) ? 14 : iv9raw + 12;
    bool dom7 = (abs3 == 4) && (iv7 == 10);                  // 7ma crearía dominante

    // Agrega rootMidi+iv si su clase de altura no está y hay lugar (sin b9/b2).
    auto addIv = [&](int iv) {
        if (iv <= 0 || iv == 1 || iv == 13) return;
        int pc = (rootMidi + iv) % 12;
        for (uint8_t i = 0; i < n; i++) if (notes[i] % 12 == pc) return;
        if (n < MAX_CHORD_NOTES) notes[n++] = (uint8_t)(rootMidi + iv);
    };

    switch (preset) {
        case HP_SEVENTH: if (!dom7) addIv(iv7); break;
        case HP_NINTH:   if (!dom7) addIv(iv7); addIv(iv9); break;
        case HP_SIXNINE: addIv(iv6); addIv(iv9); break;
        case HP_BASIC:
        default:         break;   // la duplicación de fundamental va en el sketch
    }
    return n;
}

// ---------------------------------------------------------------------------
// harmonyChordQual: como harmonyChordPreset, pero con calidad forzada por grado.
// CQ_DOM7 arma un dominante 7 sobre la raíz del grado (rompe la regla diatónica,
// opt-in): R + 3ra MAYOR + 5ta justa + 7ma menor, y agrega la 9na en las zonas
// de color "hacia arriba". Es lo que hace "mayorizar" un grado (SW + stick arriba).
// ---------------------------------------------------------------------------
uint8_t harmonyChordQual(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                         uint8_t preset, uint8_t qual, uint8_t* notes) {
    if (qual != CQ_DOM7)
        return harmonyChordPreset(deg, z, tonic, mode, preset, notes);

    const int* sc = pickScale(mode);
    int rootMidi = (tonic + sc[deg % 7]) % 12 + 48;   // misma raíz que el grado

    uint8_t n = 0;
    notes[n++] = (uint8_t)rootMidi;         // raíz
    notes[n++] = (uint8_t)(rootMidi + 4);   // 3ra MAYOR (mayoriza)
    notes[n++] = (uint8_t)(rootMidi + 7);   // 5ta justa
    notes[n++] = (uint8_t)(rootMidi + 10);  // 7ma menor -> dominante
    // 9na (color "hacia arriba"), como tensión extra del dominante.
    if (z == CZ_UP || z == CZ_UP_RIGHT || z == CZ_DOWN_RIGHT)
        if (n < MAX_CHORD_NOTES) notes[n++] = (uint8_t)(rootMidi + 14);
    return n;
}

// ---------------------------------------------------------------------------
// harmonyChordJoy: acorde según el MAPA DE JOYSTICK activo.
// JM_DEFAULT (o calidad forzada) -> harmonyChordQual (reglas duras intactas).
// JM_HICHORD -> réplica del modo "Default" del HiChord (manual oficial): la
//   base es la tríada DIATÓNICA del grado y cada dirección la transforma
//   (flip mayor/menor, 7ma según calidad, sus4, oscurecer, dom7 blues, add9,
//   6/sus2, 5ta aumentada). Cromático opt-in.
// JM_MINI -> tipos de acorde FIJOS del minichord/Omnichord sobre la raíz
//   diatónica del grado (recetas del firmware del minichord, sin la duplicación
//   de octava: el refuerzo grave lo pone nuestro sub-bajo dedicado).
// ---------------------------------------------------------------------------
uint8_t harmonyChordJoy(uint8_t deg, ColorZone z, uint8_t tonic, Mode mode,
                        uint8_t preset, uint8_t qual, uint8_t joyMap,
                        uint8_t* notes) {
    if (joyMap == JM_DEFAULT || qual != CQ_DIATONIC)
        return harmonyChordQual(deg, z, tonic, mode, preset, qual, notes);

    const int* sc = pickScale(mode);
    int root = (tonic + sc[deg % 7]) % 12 + 48;

    uint8_t n = 0;
    // Agrega root+iv sin duplicar clase de altura (root entra primero: notes[0]).
    auto put = [&](int iv) {
        int pc = (root + iv) % 12;
        for (uint8_t i = 0; i < n; i++) if (notes[i] % 12 == pc) return;
        if (n < MAX_CHORD_NOTES) notes[n++] = (uint8_t)(root + iv);
    };

    if (joyMap == JM_MINI) {
        // Recetas cromáticas por zona (orden del enum ColorZone).
        static const int8_t R[9][4] = {
            { 0, 4, 7, -1 },   // CENTER    : mayor
            { 0, 3, 7, -1 },   // UP        : menor
            { 0, 4, 7,  9 },   // DOWN      : 6ta
            { 0, 3, 6, -1 },   // LEFT      : disminuido
            { 0, 4, 7, 10 },   // RIGHT     : 7 (dominante)
            { 0, 4, 7, 11 },   // UP_RIGHT  : maj7
            { 0, 4, 8, -1 },   // UP_LEFT   : aumentado
            { 0, 3, 7, 10 },   // DOWN_RIGHT: m7
            { 0, 3, 7,  9 },   // DOWN_LEFT : m6
        };
        const int8_t* r = R[(int)z % 9];
        for (int i = 0; i < 4; i++) if (r[i] >= 0) put(r[i]);
        return n;
    }

    // JM_HICHORD: tríada diatónica del grado como punto de partida.
    int i3 = stepInterval(sc, deg, deg + 2);              // 3ra: 3 o 4
    int i5 = i3 + stepInterval(sc, deg + 2, deg + 4);     // 5ta: 6, 7 u 8
    bool mayor = (i3 == 4);

    switch (z) {
        case CZ_UP:                                        // flip mayor <-> menor
            put(0); put(mayor ? 3 : 4); put(i5); break;
        case CZ_RIGHT:                                     // 7ma según calidad
            put(0); put(i3); put(i5); put(mayor ? 11 : 10); break;
        case CZ_DOWN:                                      // sus4
            put(0); put(5); put(7); break;
        case CZ_LEFT:                                      // oscurece: M->m, m->dim
            put(0); put(3); put(mayor ? 7 : 6); break;
        case CZ_UP_RIGHT:                                  // dom7 (b7 blues)
            put(0); put(4); put(7); put(10); break;
        case CZ_DOWN_RIGHT:                                // add9
            put(0); put(i3); put(i5); put(14); break;
        case CZ_DOWN_LEFT:                                 // M: add6 / m: sus2
            if (mayor) { put(0); put(4); put(7); put(9); }
            else       { put(0); put(2); put(7); }
            break;
        case CZ_UP_LEFT:                                   // 5ta aumentada
            put(0); put(i3); put(i5 + 1); break;
        case CZ_CENTER:
        default:                                           // tríada diatónica
            put(0); put(i3); put(i5); break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// harmonyArpSequence: material de alturas que recorrerá el arpegiador.
// ---------------------------------------------------------------------------
uint8_t harmonyArpSequence(const uint8_t* notes, uint8_t n, uint8_t octaves,
                           bool scaleRun, uint8_t tonic, Mode mode,
                           uint8_t* out, uint8_t cap) {
    if (n == 0 || cap == 0) return 0;
    if (octaves < 1) octaves = 1;
    if (octaves > 4) octaves = 4;

    // Nota más grave del acorde: ancla el registro de la secuencia.
    int lo = 127;
    for (uint8_t i = 0; i < n; i++) if (notes[i] < lo) lo = notes[i];

    int tmp[64]; int m = 0;

    if (scaleRun) {
        const int* sc = pickScale(mode);
        auto inScale = [&](int midi) {
            int pc = (((midi - (int)tonic) % 12) + 12) % 12;
            for (int i = 0; i < 7; i++) if (sc[i] == pc) return true;
            return false;
        };
        int p = lo;
        while (!inScale(p)) p++;                 // primer grado de la escala >= lo
        int count = octaves * 7;                 // 7 grados por octava
        for (int k = 0; k < count && m < 64; k++) {
            tmp[m++] = p;
            do { p++; } while (!inScale(p));      // siguiente grado hacia arriba
        }
    } else {
        for (int oct = 0; oct < octaves; oct++)
            for (uint8_t i = 0; i < n && m < 64; i++)
                tmp[m++] = (int)notes[i] + 12 * oct;
    }

    // Ordenar ascendente y deduplicar (clamp a un registro tocable).
    for (int i = 1; i < m; i++) {
        int key = tmp[i], j = i - 1;
        while (j >= 0 && tmp[j] > key) { tmp[j + 1] = tmp[j]; j--; }
        tmp[j + 1] = key;
    }
    uint8_t nn = 0;
    for (int i = 0; i < m && nn < cap; i++) {
        int v = tmp[i];
        if (v < 24) v = 24; if (v > 108) v = 108;
        if (nn > 0 && out[nn - 1] == (uint8_t)v) continue;   // sin duplicados
        out[nn++] = (uint8_t)v;
    }
    return nn;
}

// ---------------------------------------------------------------------------
// Voice leading por MÍNIMA DISTANCIA con retención de tonos comunes.
// Cada nota nueva se coloca en la octava más cercana a una voz del acorde
// anterior; las notas cuya clase de altura ya estaba sonando quedan EXACTAS
// (distancia 0) -> al agregar/quitar color, las voces existentes no se mueven
// y sólo entra/sale la nota nueva (conducción natural, sin re-disparar todo).
// ---------------------------------------------------------------------------
static uint8_t prevNotes[MAX_CHORD_NOTES];
static uint8_t prevN = 0;

void harmonyVoiceLeadReset() {
    prevN = 0;
}

// Octava de la clase de altura `pc` más cercana a `target` (sin float).
static int nearestPitch(int pc, int target) {
    int p = ((pc % 12) + 12) % 12;
    while (p < target - 6) p += 12;
    while (p > target + 6) p -= 12;
    return p;
}

uint8_t harmonyVoiceLead(uint8_t* notes, uint8_t n) {
    if (n == 0) return 0;

    int pc[MAX_CHORD_NOTES];
    for (uint8_t i = 0; i < n; i++) pc[i] = notes[i] % 12;

    int out[MAX_CHORD_NOTES];

    if (prevN == 0) {
        // Primer acorde: voicing cerrado ascendente alrededor de Do central.
        out[0] = nearestPitch(pc[0], 60);
        for (uint8_t i = 1; i < n; i++) {
            int c = nearestPitch(pc[i], out[i - 1]);
            while (c <= out[i - 1]) c += 12;
            out[i] = c;
        }
    } else {
        int sum = 0; for (uint8_t j = 0; j < prevN; j++) sum += prevNotes[j];
        int center = sum / prevN;
        bool used[MAX_CHORD_NOTES] = { false };
        for (uint8_t i = 0; i < n; i++) {
            // Buscar la voz previa (no usada) que minimiza el salto para esta clase.
            int bestJ = -1, bestPitch = 0, bestD = 1 << 30;
            for (uint8_t j = 0; j < prevN; j++) {
                if (used[j]) continue;
                int cand = nearestPitch(pc[i], prevNotes[j]);
                int d = cand - prevNotes[j]; if (d < 0) d = -d;
                if (d < bestD) { bestD = d; bestJ = j; bestPitch = cand; }
            }
            if (bestJ >= 0) { out[i] = bestPitch; used[bestJ] = true; }
            else            { out[i] = nearestPitch(pc[i], center); }  // más notas que voces previas
        }
    }

    // Recentrado suave: la nota más grave dentro de [50, 65].
    int lo = out[0]; for (uint8_t i = 1; i < n; i++) if (out[i] < lo) lo = out[i];
    while (lo < 50) { for (uint8_t i = 0; i < n; i++) out[i] += 12; lo += 12; }
    while (lo > 65) { for (uint8_t i = 0; i < n; i++) out[i] -= 12; lo -= 12; }

    // Clamp duro al registro 36..91 por octava y guardar para el próximo acorde.
    for (uint8_t i = 0; i < n; i++) {
        while (out[i] < 36) out[i] += 12;
        while (out[i] > 91) out[i] -= 12;
        notes[i]     = (uint8_t)out[i];
        prevNotes[i] = notes[i];
    }
    prevN = n;
    return n;
}

// ---------------------------------------------------------------------------
// Octava global: transpone todas las notas por `octave` octavas (12 semitonos).
// ---------------------------------------------------------------------------
uint8_t harmonyApplyOctave(uint8_t* notes, uint8_t n, int8_t octave) {
    int shift = 12 * (int)octave;
    for (uint8_t i = 0; i < n; i++) {
        int m = (int)notes[i] + shift;
        if (m < 0)   m = 0;
        if (m > 127) m = 127;
        notes[i] = (uint8_t)m;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Voicing / inversiones. Trabaja sobre las notas ordenadas ascendente y sube
// una octava algunas voces. Conserva las clases de altura (mismo acorde).
// ---------------------------------------------------------------------------
static void sortAsc(uint8_t* a, uint8_t n) {
    for (int i = 1; i < n; i++) {
        uint8_t key = a[i]; int j = i - 1;
        while (j >= 0 && a[j] > key) { a[j+1] = a[j]; j--; }
        a[j+1] = key;
    }
}

uint8_t harmonyApplyVoicing(uint8_t* notes, uint8_t n, Voicing v) {
    if (n < 2 || v == VC_CLOSE) return n;
    sortAsc(notes, n);                       // grave -> agudo
    switch (v) {
        case VC_INV1:                        // 1ra inversión: el bajo sube una 8va
            notes[0] += 12;
            break;
        case VC_INV2:                        // 2da inversión: las dos más graves suben
            notes[0] += 12;
            if (n >= 2) notes[1] += 12;
            break;
        case VC_OPEN:                        // abierta: la voz interna sube (amplitud)
            if (n >= 2) notes[1] += 12;
            break;
        default:
            break;
    }
    for (uint8_t i = 0; i < n; i++) if (notes[i] > 127) notes[i] -= 12;   // seguridad
    return n;
}

// ---------------------------------------------------------------------------
// Nombres para la pantalla (con enarmonía según la tonalidad)
// ---------------------------------------------------------------------------
static const char* NN_SHARP[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static const char* NN_FLAT [12] = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

// La tonalidad usa bemoles si su (relativo) mayor está del lado bemol del círculo.
static bool keyUsesFlats(uint8_t tonic, Mode mode) {
    int relMaj = ((mode == MODE_AEOLIAN) ? (tonic + 3) : tonic) % 12;
    switch (relMaj) {                       // F, Bb, Eb, Ab, Db, Gb -> bemoles
        case 1: case 3: case 5: case 6: case 8: case 10: return true;
        default: return false;
    }
}
static const char* noteName(int pc, uint8_t tonic, Mode mode) {
    pc = ((pc % 12) + 12) % 12;
    return keyUsesFlats(tonic, mode) ? NN_FLAT[pc] : NN_SHARP[pc];
}

const char* harmonyRootName(uint8_t degree, uint8_t tonic, Mode mode) {
    const int* sc = pickScale(mode);
    return noteName((tonic + sc[degree % 7]) % 12, tonic, mode);
}

const char* harmonyModeName(Mode mode) { return (mode == MODE_AEOLIAN) ? "min" : "MAJ"; }

// Nombra el acorde a partir de las notas REALES (zona + preset), por sus intervalos.
void harmonyChordSymbol(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                        uint8_t preset, char* out, int cap) {
    harmonyChordSymbolQual(degree, z, tonic, mode, preset, CQ_DIATONIC, out, cap);
}

// Nombra un acorde ya construido, por sus intervalos sobre notes[0].
static void nameChord(const uint8_t* notes, uint8_t n, uint8_t tonic, Mode mode,
                      char* out, int cap) {
    int rootPc = notes[0] % 12;
    bool has[12] = { false };
    for (uint8_t i = 0; i < n; i++) has[(((int)notes[i] % 12) - rootPc + 12) % 12] = true;

    const char* root = noteName(rootPc, tonic, mode);
    bool M3 = has[4], m3 = has[3], b5 = has[6];
    bool sev = has[10] || has[11];          // hay 7ma (b7 o maj7)
    bool maj7 = has[11];
    bool nine = has[2], four = has[5], six = has[9];

    // Acordes suspendidos (sin 3ra).
    if (!M3 && !m3) {
        if (four && sev)  snprintf(out, cap, "%s7sus4", root);
        else if (four)    snprintf(out, cap, "%ssus4", root);
        else if (nine)    snprintf(out, cap, "%ssus2", root);
        else              snprintf(out, cap, "%s5", root);
        return;
    }
    // 5ta aumentada sin 5ta justa (mapas HICHORD/MINI): aug / m#5.
    if (!sev && !has[7] && has[8]) {
        snprintf(out, cap, "%s%s", root, M3 ? "aug" : "m#5");
        return;
    }

    if (sev) {                               // con 7ma
        if (b5) { snprintf(out, cap, "%sm7b5", root); return; }   // semidisminuido
        if (nine) snprintf(out, cap, "%s%s", root, M3 ? (maj7 ? "maj9" : "9") : "m9");
        else      snprintf(out, cap, "%s%s", root, M3 ? (maj7 ? "maj7" : "7") : "m7");
        return;
    }
    const char* base = M3 ? "" : (b5 ? "dim" : "m");
    if (six)       { snprintf(out, cap, "%s%s%s", root, base, nine ? "6/9" : "6"); return; }
    if (nine)      { snprintf(out, cap, "%s%sadd9", root, base); return; }
    snprintf(out, cap, "%s%s", root, base);  // tríada
}

// Igual, pero contemplando la calidad forzada por grado (CQ_DOM7 -> "D7"...).
void harmonyChordSymbolQual(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                            uint8_t preset, uint8_t qual, char* out, int cap) {
    uint8_t notes[MAX_CHORD_NOTES];
    uint8_t n = harmonyChordQual(degree, z, tonic, mode, preset, qual, notes);
    nameChord(notes, n, tonic, mode, out, cap);
}

// Igual, pero contemplando además el mapa de joystick activo.
void harmonyChordSymbolJoy(uint8_t degree, ColorZone z, uint8_t tonic, Mode mode,
                           uint8_t preset, uint8_t qual, uint8_t joyMap,
                           char* out, int cap) {
    uint8_t notes[MAX_CHORD_NOTES];
    uint8_t n = harmonyChordJoy(degree, z, tonic, mode, preset, qual, joyMap, notes);
    nameChord(notes, n, tonic, mode, out, cap);
}
