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

// ===========================================================================
// CONDUCCIÓN DE VOCES (voice leading) — algoritmos seleccionables.
//
// Concepto SEPARADO del voicing/inversión (`Voicing`, harmonyApplyVoicing): la
// conducción decide cómo se reparten las voces en el registro (y su relación con
// el acorde anterior); el voicing sólo reordena octavas después. Los 4 modos:
//   VL_SMOOTH   : histórico (mínima distancia + tonos comunes). SIN regresiones.
//   VL_PARALLEL : bloques/posición fija (ignora el acorde previo), estilo HiChord.
//   VL_CHORALE  : 4 voces ordenadas, sin cruces, registros corales; búsqueda
//                 acotada (permutación de clases sobre las voces) con costos.
//   VL_CONTRARY : misma infraestructura que CHORALE pero premia el movimiento
//                 contrario entre las voces exteriores.
//
// El historial de voces (`prevNotes`) NUNCA se mezcla entre algoritmos: al
// detectar un cambio de modo se reinicia. Todo es entero y determinista; el
// cálculo ocurre sólo al construir/disparar el acorde (jamás por muestra).
// ===========================================================================
static uint8_t prevNotes[MAX_CHORD_NOTES];   // voces del acorde anterior (ascendente en CHORALE)
static uint8_t prevN = 0;
static VoiceLeadMode prevMode = VL_SMOOTH;   // algoritmo con el que se llenó el historial

// Contador de candidatos (permutaciones) evaluados por la búsqueda coral. Existe
// SÓLO en host (bench/tests, que lo declaran `extern`): en firmware (ARDUINO) el
// símbolo y su incremento DESAPARECEN por completo -> cero telemetría/printf en
// el camino de audio. Por llamada a vlChorale nunca supera 7!=5040.
#ifndef ARDUINO
long gVlCandidatesEvaluated = 0;
#endif

// Cota factorial de la búsqueda coral: vlChorale acota a lo sumo 7!=5040
// candidatos SÓLO si MAX_CHORD_NOTES<=8 (con n>7 fija el bajo y permuta n-1). Si
// alguien sube MAX_CHORD_NOTES, esto FALLA en compilación en vez de producir una
// explosión factorial silenciosa (8! = 40320, 9! = 362880, ...).
static_assert(MAX_CHORD_NOTES <= 8,
              "vlChorale: MAX_CHORD_NOTES>8 rompe la cota de 5040 candidatos");

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

// --- VL_SMOOTH: cuerpo histórico, intacto (mínima distancia + tonos comunes). ---
static uint8_t vlSmooth(uint8_t* notes, uint8_t n) {
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

// --- VL_PARALLEL: bloques/posición fija. No usa el acorde anterior. ---------
// Fundamental como voz más grave, clases del acorde ordenadas ascendentemente en
// posición cerrada, centrado aprox. en MIDI 48..76. Ignora (y limpia) el
// historial: volver a este modo no hereda estado. Resultado determinista y
// función pura de (clases, rootPc).
static uint8_t vlParallel(uint8_t* notes, uint8_t n, int rootPc) {
    if (n == 0) { prevN = 0; return 0; }
    rootPc = ((rootPc % 12) + 12) % 12;

    // Clases de altura del acorde (la fundamental primero, sin duplicar).
    int pcs[MAX_CHORD_NOTES]; int m = 0;
    pcs[m++] = rootPc;
    for (uint8_t i = 0; i < n; i++) {
        int pc = notes[i] % 12; bool dup = false;
        for (int j = 0; j < m; j++) if (pcs[j] == pc) { dup = true; break; }
        if (!dup && m < MAX_CHORD_NOTES) pcs[m++] = pc;
    }

    // Apila ascendente cerrado desde la fundamental (registro grave estable).
    int out[MAX_CHORD_NOTES];
    out[0] = rootPc + 48;                     // fundamental en 48..59 (voz más grave)
    for (int i = 1; i < m; i++) {
        int c = nearestPitch(pcs[i], out[i - 1]);
        while (c <= out[i - 1]) c += 12;      // cerrado y estrictamente ascendente
        out[i] = c;
    }

    // Centrado aprox. en 48..76 (por octavas completas, conserva el bloque).
    int sum = 0; for (int i = 0; i < m; i++) sum += out[i];
    int mean = sum / m;
    while (mean > 76) { for (int i = 0; i < m; i++) out[i] -= 12; mean -= 12; }
    while (mean < 48) { for (int i = 0; i < m; i++) out[i] += 12; mean += 12; }

    // Clamp duro 36..91 (antes de la octava global).
    for (int i = 0; i < m; i++) {
        while (out[i] < 36) out[i] += 12;
        while (out[i] > 91) out[i] -= 12;
        notes[i] = (uint8_t)out[i];
    }
    prevN = 0;                                // ignora/limpia el historial
    return (uint8_t)m;
}

// --- Costos de CHORALE/CONTRARY (constantes nombradas, ajustables). ---------
static const int32_t VL_COST_MOVE        = 1;    // por semitono de movimiento
static const int32_t VL_COST_JUMP5       = 18;   // salto  > 5 st
static const int32_t VL_COST_JUMP7       = 45;   // salto  > 7 st (acumulativo)
static const int32_t VL_COST_JUMP12      = 100;  // salto  >12 st (acumulativo)
static const int32_t VL_COST_CROSS       = 250;  // cruce de voces (evitar realización que cruzaría)
static const int32_t VL_COST_DROPCOMMON  = 70;   // abandonar un tono común conservable
static const int32_t VL_COST_PAR5        = 90;   // quinta paralela entre voces adyacentes
static const int32_t VL_COST_PAR8        = 110;  // octava/unísono paralelo adyacente
static const int32_t VL_COST_OOR         = 20;   // voz fuera de su registro preferido
static const int32_t VL_COST_BASSNOROOT  = 12;   // bajo distinto de la fundamental
static const int32_t VL_BONUS_CONTRARY_CHORALE  = -10;  // mov. contrario exterior (CHORALE)
static const int32_t VL_BONUS_CONTRARY_CONTRARY = -45;  // mov. contrario exterior (CONTRARY)
static const int32_t VL_COST_PAR_OUTER_CONTRARY =  35;  // mov. paralelo exterior (CONTRARY)
// --- Separación coral (evita aperturas excesivas; roles bajo/medias/soprano) ---
static const int32_t VL_COST_UPPERGAP    = 35;   // separación superior >12, por st excedente
static const int32_t VL_COST_BASSGAP     = 12;   // bajo->2da voz >12 (permite hasta 19), por st
static const int32_t VL_COST_GAP19       = 180;  // cualquier separación adyacente >19 (extra)
static const int32_t VL_COST_SPAN_TRIAD  = 80;   // span total >24 en tríadas, por st excedente
static const int32_t VL_COST_SPAN_BIG    = 60;   // span total >31 con 4+ voces, por st excedente

// Registro preferido de la voz `k` de `n`. Los centros se reparten alrededor de
// Do central (60) con un SPAN objetivo compacto que crece con la cantidad de
// voces (~6 st por voz, tope 31): así una tríada NO se distribuye sobre los
// registros de un coral a 4 voces dejando un hueco artificial (bajo–voz media–
// soprano compactos), y con 4 voces reproduce bajo–tenor–alto–soprano. El
// registro tolerado por voz es el centro ±12 (para la penalización VL_COST_OOR).
static void voiceRegister(int k, int n, int* center, int* lo, int* hi) {
    if (n <= 1) { *center = 62; *lo = 50; *hi = 74; return; }
    int S = 6 * (n - 1) + 6;             // span objetivo del acorde (st)
    if (S > 31) S = 31;                  // cota (coincide con el límite de span 4+)
    int c = 60 - S / 2 + (S * k) / (n - 1);
    *center = c;
    *lo     = c - 12;
    *hi     = c + 12;
}

// next_permutation entero sobre a[lo..n-1] (orden lexicográfico ascendente).
static bool nextPermRange(int* a, int lo, int n) {
    if (n - lo < 2) return false;
    int i = n - 2;
    while (i >= lo && a[i] >= a[i + 1]) i--;
    if (i < lo) return false;
    int j = n - 1;
    while (a[j] <= a[i]) j--;
    int t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return true;
}

// --- VL_CHORALE / VL_CONTRARY: búsqueda exhaustiva ACOTADA por permutación. ---
// CONTRARY es CHORALE con pesos distintos (mismo motor, un solo `bool contrary`),
// no un segundo algoritmo. Soporta 1..MAX_CHORD_NOTES voces. Detalle explícito:
//
//  1) GENERACIÓN DE CANDIDATOS (clase de altura × octava): se toman las clases de
//     altura del acorde (sin duplicar) y se prueban TODAS las permutaciones de
//     clases-sobre-voces (bajo..soprano). En cada permutación, la octava de cada
//     voz se fija realizando su clase cerca del centro de registro de esa voz
//     (nearestPitch) y luego forzando orden ascendente estricto. COTA de la
//     búsqueda (garantizada por el static_assert de arriba):
//       · n≤7 -> se permutan las n clases -> a lo sumo n! (7! = 5040);
//       · n=8 -> se FIJA la fundamental al bajo y se permutan las 7 restantes
//                -> a lo sumo 7! = 5040;
//       · por lo tanto NUNCA supera 5040 candidatos por acorde.
//     next_permutation es iterativo (sin recursión). Todos los bucles internos
//     iteran sobre m (≤ MAX_CHORD_NOTES ≤ 8), sobre prevN (≤ 8) o sobre 12 clases
//     de altura: cotas fijas derivadas de MAX_CHORD_NOTES, sin asignación dinámica
//     ni recursión. Buffers locales del peor camino (todos en stack): pcs/perm/p/
//     best int[8] + crossed bool[8] + prevHasPc bool[12] + prevPitchOfPc int[12]
//     ≈ 200 B, más los frames del caller.
//  2) CORRESPONDENCIA voz previa <-> nueva (para el costo de movimiento): si la
//     clase de la voz nueva ya sonaba (tono común), su objetivo es la MISMA
//     octava previa de esa clase (premia retenerla); si no, el objetivo es la
//     voz previa más cercana en altura. Así la identidad la fija la clase, no el
//     índice de voz (agregar/quitar una extensión no mueve a las demás).
//  3) CAMBIO DE CANTIDAD DE VOCES: el costo de movimiento admite prevN≠m (las
//     voces nuevas se miden contra la previa más cercana; las que sobran del
//     acorde previo simplemente no aportan). PERO las comparaciones POR PAR de
//     voces (paralelas y movimiento exterior) SÓLO se hacen si prevN==m: con
//     distinta cantidad no se puede afirmar la identidad de un par (regla: no
//     comparar un par cuya identidad no se preservó razonablemente).
//  4) CRUCES: se detectan en la realización (una voz cuyo nearest cae en/bajo la
//     voz inferior obliga a subir una octava -> `crossed`), penalizados fuerte
//     (VL_COST_CROSS) para que jamás se elija una realización que cruzaría.
//  5) 5tas/8vas PARALELAS: se comparan el intervalo (voz k+1 − voz k) ANTES y
//     AHORA. Es paralelo sólo si AMBAS voces se mueven en la MISMA dirección no
//     nula (movimiento oblicuo —una voz quieta— NO es paralelo, ni el contrario).
//     Penaliza si el intervalo es 5ta (mod 12 == 7) en ambos estados (VL_COST_PAR5)
//     u 8va/unísono (mod 12 == 0) en ambos (VL_COST_PAR8).
//  6) DESEMPATE ESTABLE: las permutaciones se recorren en orden lexicográfico y
//     sólo se reemplaza el mejor por (costo, luego movimiento total, luego
//     comparación lexicográfica de las notas) estrictamente menor -> determinista.
static uint8_t vlChorale(uint8_t* notes, uint8_t n, int rootPc, bool contrary) {
    if (n == 0) { prevN = 0; return 0; }
    rootPc = ((rootPc % 12) + 12) % 12;

    // Clases de altura del acorde (sin duplicar), en orden de entrada.
    int pcs[MAX_CHORD_NOTES]; int m = 0;
    for (uint8_t i = 0; i < n; i++) {
        int pc = notes[i] % 12; bool dup = false;
        for (int j = 0; j < m; j++) if (pcs[j] == pc) { dup = true; break; }
        if (!dup) pcs[m++] = pc;
    }

    // Caso trivial: una sola voz.
    if (m == 1) {
        int c = nearestPitch(pcs[0], 60);
        while (c < 36) c += 12; while (c > 91) c -= 12;
        notes[0] = (uint8_t)c;
        prevNotes[0] = notes[0]; prevN = 1;
        return 1;
    }

    // Datos del acorde anterior (para tonos comunes / movimiento).
    const bool havePrev = (prevN > 0);
    int prevPitchOfPc[12]; bool prevHasPc[12];
    for (int i = 0; i < 12; i++) { prevHasPc[i] = false; prevPitchOfPc[i] = 0; }
    if (havePrev)
        for (uint8_t j = 0; j < prevN; j++) {
            int pc = prevNotes[j] % 12;
            if (!prevHasPc[pc]) { prevHasPc[pc] = true; prevPitchOfPc[pc] = prevNotes[j]; }
        }

    // Con muchas voces se fija la fundamental al bajo para acotar la búsqueda a 7!.
    int lo = 0;
    int perm[MAX_CHORD_NOTES];
    for (int i = 0; i < m; i++) perm[i] = i;
    if (m > 7) {
        int ri = 0; for (int i = 0; i < m; i++) if (pcs[i] == rootPc) { ri = i; break; }
        int t = perm[0]; perm[0] = ri; perm[ri] = t;   // fundamental en la voz 0
        lo = 1;
        // ordenar la cola para que next_permutation las recorra todas
        for (int a = lo; a < m; a++) for (int b = a + 1; b < m; b++)
            if (perm[b] < perm[a]) { int u = perm[a]; perm[a] = perm[b]; perm[b] = u; }
    }

    int32_t bestCost = 0x7fffffff, bestMotion = 0x7fffffff;
    int     best[MAX_CHORD_NOTES]; bool haveBest = false;

    do {
#ifndef ARDUINO
        gVlCandidatesEvaluated++;                 // cota de rendimiento (sólo host)
#endif
        int p[MAX_CHORD_NOTES]; bool crossed[MAX_CHORD_NOTES];
        // Realización: cada voz cerca de su centro, ascendente estricto.
        for (int k = 0; k < m; k++) {
            int center, rlo, rhi; voiceRegister(k, m, &center, &rlo, &rhi);
            int c = nearestPitch(pcs[perm[k]], center);
            crossed[k] = false;
            if (k > 0) while (c <= p[k - 1]) { c += 12; crossed[k] = true; }
            p[k] = c;
        }
        // Ajuste por octavas completas al registro 36..91 (conserva intervalos).
        int mx = p[m - 1], mn = p[0];
        while (mx > 91) { for (int k = 0; k < m; k++) p[k] -= 12; mx -= 12; mn -= 12; }
        while (mn < 36) { for (int k = 0; k < m; k++) p[k] += 12; mn += 12; mx += 12; }

        // ----- Costo -----
        int32_t cost = 0, motion = 0;
        for (int k = 0; k < m; k++) {
            int center, rlo, rhi; voiceRegister(k, m, &center, &rlo, &rhi);
            if (p[k] < rlo || p[k] > rhi) cost += VL_COST_OOR;
            if (crossed[k]) cost += VL_COST_CROSS;
            if (havePrev) {
                int pcK = pcs[perm[k]];
                int target;
                if (prevHasPc[pcK]) target = prevPitchOfPc[pcK];   // tono común: su octava previa
                else {                                             // si no, la voz previa más cercana
                    int bd = 1 << 30; target = p[k];
                    for (uint8_t j = 0; j < prevN; j++) {
                        int d = p[k] - prevNotes[j]; if (d < 0) d = -d;
                        if (d < bd) { bd = d; target = prevNotes[j]; }
                    }
                }
                int d = p[k] - target; if (d < 0) d = -d;
                motion += d;
                cost += d * VL_COST_MOVE;
                if (d > 5)  cost += VL_COST_JUMP5;
                if (d > 7)  cost += VL_COST_JUMP7;
                if (d > 12) cost += VL_COST_JUMP12;
            }
        }
        // Bajo sin fundamental.
        if ((pcs[perm[0]] % 12) != rootPc) cost += VL_COST_BASSNOROOT;

        // SEPARACIÓN CORAL (roles): el bajo tolera hasta 19 st con la 2da voz;
        // las voces superiores adyacentes se prefieren <=12. Toda separación >19
        // se penaliza fuerte, y el span total se acota (24 tríadas / 31 con 4+).
        // Son COSTOS (no filtros duros): siempre hay candidato -> fallback seguro.
        for (int k = 1; k < m; k++) {
            int gap = p[k] - p[k - 1];               // >0 (orden ascendente)
            if (k == 1) { if (gap > 12) cost += VL_COST_BASSGAP  * (gap - 12); }  // bajo->2da
            else        { if (gap > 12) cost += VL_COST_UPPERGAP * (gap - 12); }  // superiores
            if (gap > 19) cost += VL_COST_GAP19;
        }
        {
            int span = p[m - 1] - p[0];
            if (m <= 3) { if (span > 24) cost += VL_COST_SPAN_TRIAD * (span - 24); }
            else        { if (span > 31) cost += VL_COST_SPAN_BIG   * (span - 31); }
        }

        // Tonos comunes conservables no retenidos exactamente.
        if (havePrev)
            for (int pc = 0; pc < 12; pc++) {
                if (!prevHasPc[pc]) continue;
                bool inCur = false; for (int k = 0; k < m; k++) if (pcs[perm[k]] == pc) { inCur = true; break; }
                if (!inCur) continue;                          // no es común (no está en el actual)
                bool held = false; for (int k = 0; k < m; k++) if (p[k] == prevPitchOfPc[pc]) { held = true; break; }
                if (!held) cost += VL_COST_DROPCOMMON;
            }

        // 5tas/8vas PARALELAS entre voces adyacentes. Sólo con prevN==m (misma
        // cantidad -> identidad de voces por índice razonablemente preservada;
        // ver punto 3). Es paralelo cuando AMBAS voces se mueven en la MISMA
        // dirección no nula (oblicuo/contrario NO cuentan). Se compara el
        // intervalo módulo 12 ANTES y AHORA: 7 = 5ta, 0 = 8va/unísono.
        if (havePrev && (int)prevN == m)
            for (int k = 0; k + 1 < m; k++) {
                int prevInt = (int)prevNotes[k + 1] - (int)prevNotes[k];
                int curInt  = p[k + 1] - p[k];
                int mvLo = p[k]     - (int)prevNotes[k];
                int mvHi = p[k + 1] - (int)prevNotes[k + 1];
                bool sameDir = (mvLo > 0 && mvHi > 0) || (mvLo < 0 && mvHi < 0);
                if (sameDir) {                                  // similar (paralelo), no oblicuo
                    int pm = ((prevInt % 12) + 12) % 12, cm = ((curInt % 12) + 12) % 12;
                    if (pm == 0 && cm == 0)      cost += VL_COST_PAR8;
                    else if (pm == 7 && cm == 7) cost += VL_COST_PAR5;
                }
            }

        // Movimiento exterior (bajo vs soprano). Contrario -> bonus; en CONTRARY,
        // el paralelo exterior se penaliza. Sólo con prevN==m (identidad de las
        // voces exteriores preservada) y con ambas voces moviéndose (no oblicuo).
        if (havePrev && (int)prevN == m && m >= 2) {
            int db = p[0]     - (int)prevNotes[0];
            int ds = p[m - 1] - (int)prevNotes[m - 1];
            if (db != 0 && ds != 0) {
                bool contraMove = ((db > 0) != (ds > 0));
                if (contraMove) cost += contrary ? VL_BONUS_CONTRARY_CONTRARY : VL_BONUS_CONTRARY_CHORALE;
                else if (contrary) cost += VL_COST_PAR_OUTER_CONTRARY;
            }
        }

        // Selección determinista: menor costo; empate -> menor movimiento; luego
        // comparación lexicográfica de las notas.
        bool take = !haveBest || cost < bestCost;
        if (!take && cost == bestCost) {
            if (motion < bestMotion) take = true;
            else if (motion == bestMotion)
                for (int k = 0; k < m; k++) { if (p[k] < best[k]) { take = true; break; } if (p[k] > best[k]) break; }
        }
        if (take) {
            bestCost = cost; bestMotion = motion; haveBest = true;
            for (int k = 0; k < m; k++) best[k] = p[k];
        }
    } while (nextPermRange(perm, lo, m));

    for (int k = 0; k < m; k++) {
        notes[k]     = (uint8_t)best[k];
        prevNotes[k] = notes[k];
    }
    prevN = (uint8_t)m;
    return (uint8_t)m;
}

// Wrapper de compatibilidad: VL_SMOOTH exacto.
uint8_t harmonyVoiceLead(uint8_t* notes, uint8_t n) {
    return harmonyVoiceLeadMode(notes, n, n ? (uint8_t)(notes[0] % 12) : 0, VL_SMOOTH);
}

uint8_t harmonyVoiceLeadMode(uint8_t* notes, uint8_t n, uint8_t rootPc,
                             VoiceLeadMode mode) {
    if (mode >= VL_COUNT) mode = VL_SMOOTH;
    // El historial jamás se mezcla entre algoritmos: cambiar de modo lo reinicia.
    if (mode != prevMode) { prevN = 0; prevMode = mode; }
    switch (mode) {
        case VL_PARALLEL: return vlParallel(notes, n, rootPc);
        case VL_CHORALE:  return vlChorale(notes, n, rootPc, false);
        case VL_CONTRARY: return vlChorale(notes, n, rootPc, true);
        case VL_SMOOTH:
        default:          return vlSmooth(notes, n);
    }
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
// Pipeline ÚNICO de acorde vozado (fuente de verdad compartida firmware/#degree).
// ---------------------------------------------------------------------------
uint8_t harmonyBuildVoiced(uint8_t deg, ColorZone zone, uint8_t tonic, Mode mode,
                           uint8_t preset, uint8_t qual, uint8_t joyMap,
                           VoiceLeadMode lead, Voicing voicing, int8_t octave,
                           uint8_t* notes) {
    uint8_t n = harmonyChordJoy(deg, zone, tonic, mode, preset, qual, joyMap, notes);
    uint8_t rootPc = n ? (uint8_t)(notes[0] % 12) : 0;   // ANTES de reordenar
    n = harmonyVoiceLeadMode(notes, n, rootPc, lead);
    n = harmonyApplyVoicing(notes, n, voicing);
    n = harmonyApplyOctave(notes, n, octave);
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
        if (four && sev)  snprintf(out, cap, "%s7sus", root);   // 7sus4 -> 7sus (el 4 se sobreentiende)
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
