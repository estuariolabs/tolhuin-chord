// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   test_harmony.cpp  -  Test de host del motor de armonia (sin hardware).
   harmony.cpp es C++ puro: se compila en la PC y se verifican las reglas duras.

   Compilar/correr (lo hace test/run_tests.ps1):
     g++ -std=c++17 -I.. test_harmony.cpp ../harmony.cpp -o test_harmony
     ./test_harmony      (devuelve 0 si todo pasa, 1 si hay fallas)

   Reglas verificadas:
     1) cantidad de notas en [1, MAX_CHORD_NOTES]
     2) TODAS las notas son diatonicas a la tonalidad/modo
     3) ningun acorde es dominante (no 3ra mayor + b7 a la vez)
     4) no se agregan b9 ni b2
     5) voice leading: registro acotado y saltos chicos en una progresion
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "harmony.h"   // trae state.h (ColorZone, Voicing, Mode)
#include "config.h"    // MAX_CHORD_NOTES

static int checks = 0, failures = 0;
#define CHECK(cond, msg, ...) do { checks++; if(!(cond)) { failures++; \
  printf("FAIL: " msg "\n", ##__VA_ARGS__); } } while(0)

// Escalas (deben coincidir con harmony.cpp).
static const int SC_MAJ[7] = {0, 2, 4, 5, 7, 9, 11};
static const int SC_MIN[7] = {0, 2, 3, 5, 7, 8, 10};

static bool inKey(int midi, int tonic, Mode mode) {
  const int* sc = (mode == MODE_AEOLIAN) ? SC_MIN : SC_MAJ;
  int pc = ((midi - tonic) % 12 + 12) % 12;   // clase de altura relativa a la tonica
  for (int i = 0; i < 7; i++) if (sc[i] == pc) return true;
  return false;
}

int main() {
  Mode      modes[2]  = { MODE_IONIAN, MODE_AEOLIAN };
  int       tonics[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 }; // todas las tonalidades
  ColorZone zones[9]  = { CZ_CENTER, CZ_UP, CZ_DOWN, CZ_LEFT, CZ_RIGHT,
                          CZ_UP_RIGHT, CZ_UP_LEFT, CZ_DOWN_RIGHT, CZ_DOWN_LEFT };

  for (int mi = 0; mi < 2; mi++) {
    Mode mode = modes[mi];
    for (int ti = 0; ti < 12; ti++) {
      int tonic = tonics[ti];
      for (int deg = 0; deg < 7; deg++) {
        for (int zi = 0; zi < 9; zi++) {
          ColorZone z = zones[zi];
          uint8_t notes[8];
          uint8_t n = harmonyChord((uint8_t)deg, z, (uint8_t)tonic, mode, notes);

          // 1) cantidad
          CHECK(n >= 1 && n <= MAX_CHORD_NOTES,
                "n=%d fuera de rango (deg %d zona %d modo %d)", n, deg, zi, mi);

          // 2) diatonicas
          for (int i = 0; i < n; i++)
            CHECK(inKey(notes[i], tonic, mode),
                  "nota fuera de escala: %d (deg %d zona %d modo %d tonic %d)",
                  notes[i], deg, zi, mi, tonic);

          // 3) y 4) intervalos sobre la raiz (out[0] = raiz antes del voicing)
          int root = notes[0];
          bool maj3 = false, b7 = false, b9 = false, m2 = false;
          for (int i = 0; i < n; i++) {
            int iv = (int)notes[i] - root;
            if (iv == 4)  maj3 = true;   // 3ra mayor
            if (iv == 10) b7   = true;   // 7ma menor (b7)
            if (iv == 13) b9   = true;   // 9na menor (b9)
            if (iv == 1)  m2   = true;   // 2da menor (b2)
          }
          CHECK(!(maj3 && b7), "DOMINANTE (deg %d zona %d modo %d)", deg, zi, mi);
          CHECK(!b9, "b9 presente (deg %d zona %d modo %d)", deg, zi, mi);
          CHECK(!m2, "b2 presente (deg %d zona %d modo %d)", deg, zi, mi);
        }
      }
    }
  }

  // 5) Voice leading mayor: I-IV-V-vi (x2), CZ_CENTER, Do mayor.
  {
    harmonyVoiceLeadReset();
    int prog[4] = { 0, 3, 4, 5 };
    int prevLo = -1;
    for (int step = 0; step < 8; step++) {
      int deg = prog[step % 4];
      uint8_t notes[8];
      uint8_t n = harmonyChord((uint8_t)deg, CZ_CENTER, 0, MODE_IONIAN, notes);
      n = harmonyVoiceLead(notes, n);

      int lo = 127;
      for (int i = 0; i < n; i++) {
        CHECK(notes[i] >= 36 && notes[i] <= 91, "VL-maj registro fuera de rango: %d", notes[i]);
        if (notes[i] < lo) lo = notes[i];
      }
      if (prevLo >= 0) {
        int jump = lo - prevLo; if (jump < 0) jump = -jump;
        CHECK(jump <= 11, "VL-maj salto bajo grande: %d", jump);
      }
      prevLo = lo;
    }
  }

  // 6) Voice leading menor: i-iv-III-VII (x2), CZ_UP (con 9na), La menor.
  {
    harmonyVoiceLeadReset();
    int prog[4] = { 0, 3, 2, 6 };   // i, iv, III, VII en eolico
    int prevLo = -1;
    for (int step = 0; step < 8; step++) {
      int deg = prog[step % 4];
      uint8_t notes[8];
      uint8_t n = harmonyChord((uint8_t)deg, CZ_UP, 9, MODE_AEOLIAN, notes);  // tonic=9 (La)
      n = harmonyVoiceLead(notes, n);

      int lo = 127;
      for (int i = 0; i < n; i++) {
        CHECK(notes[i] >= 36 && notes[i] <= 91, "VL-min registro fuera de rango: %d", notes[i]);
        if (notes[i] < lo) lo = notes[i];
      }
      if (prevLo >= 0) {
        int jump = lo - prevLo; if (jump < 0) jump = -jump;
        CHECK(jump <= 11, "VL-min salto bajo grande: %d", jump);
      }
      prevLo = lo;
    }
  }

  // 7) Ninguna extensión de zona convierte a V en dominante (caso crítico).
  {
    ColorZone allZones[9] = { CZ_CENTER, CZ_UP, CZ_DOWN, CZ_LEFT, CZ_RIGHT,
                              CZ_UP_RIGHT, CZ_UP_LEFT, CZ_DOWN_RIGHT, CZ_DOWN_LEFT };
    for (int ti = 0; ti < 12; ti++) {
      for (int zi = 0; zi < 9; zi++) {
        uint8_t notes[8];
        uint8_t n = harmonyChord(4, allZones[zi], (uint8_t)ti, MODE_IONIAN, notes);
        int root = notes[0];
        bool hasMaj3 = false, hasB7 = false;
        for (int i = 0; i < n; i++) {
          int iv = (int)notes[i] - root;
          if (iv == 4)  hasMaj3 = true;
          if (iv == 10) hasB7   = true;
        }
        CHECK(!(hasMaj3 && hasB7),
              "V dominante (zona %d tonic %d)", zi, ti);
      }
    }
  }

  // 8) Octava global: desplazamiento de fundamental y de todas las notas ±12.
  {
    for (int oct = -1; oct <= 2; oct++) {
      uint8_t base[8], shifted[8];
      uint8_t n = harmonyChord(0, CZ_CENTER, 0, MODE_IONIAN, base);   // C mayor
      for (int i = 0; i < n; i++) shifted[i] = base[i];
      harmonyApplyOctave(shifted, n, (int8_t)oct);
      for (int i = 0; i < n; i++) {
        int expect = (int)base[i] + 12 * oct;
        if (expect < 0) expect = 0; if (expect > 127) expect = 127;
        CHECK(shifted[i] == expect,
              "octava %d: nota %d -> %d (esperado %d)", oct, base[i], shifted[i], expect);
      }
      // fundamental desplazada exactamente 12*oct (dentro de rango)
      CHECK((int)shifted[0] == (int)base[0] + 12 * oct,
            "octava %d: fundamental %d -> %d", oct, base[0], shifted[0]);
    }
  }

  // 9) Voicing / inversiones: conservan las clases de altura; INV sube el bajo.
  {
    auto pcMask = [](const uint8_t* a, int m) {
      int mask = 0; for (int i = 0; i < m; i++) mask |= (1 << (a[i] % 12)); return mask;
    };
    Voicing vs[4] = { VC_CLOSE, VC_INV1, VC_INV2, VC_OPEN };
    for (int vi = 0; vi < 4; vi++) {
      uint8_t base[8], voi[8];
      uint8_t n = harmonyChord(0, CZ_CENTER, 0, MODE_IONIAN, base);   // C mayor (tríada)
      n = harmonyVoiceLead(base, n);
      for (int i = 0; i < n; i++) voi[i] = base[i];
      uint8_t m = harmonyApplyVoicing(voi, n, vs[vi]);

      CHECK(m == n, "voicing %d: cambió la cantidad de notas (%d->%d)", vi, n, m);
      CHECK(pcMask(voi, m) == pcMask(base, n),
            "voicing %d: cambió las clases de altura del acorde", vi);
      for (int i = 0; i < m; i++)
        CHECK(voi[i] <= 127, "voicing %d: nota fuera de MIDI (%d)", vi, voi[i]);

      // INV1/INV2 deben elevar la nota más grave respecto a la fundamental.
      if (vs[vi] == VC_INV1 || vs[vi] == VC_INV2) {
        int loBase = 127; for (int i = 0; i < n; i++) if (base[i] < loBase) loBase = base[i];
        int loVoi  = 127; for (int i = 0; i < m; i++) if (voi[i]  < loVoi)  loVoi  = voi[i];
        CHECK(loVoi > loBase, "voicing %d: no subió el bajo (%d -> %d)", vi, loBase, loVoi);
      }
    }
  }

  // 10) Voice leading: al AGREGAR color sobre el mismo acorde, las voces que ya
  //     sonaban quedan EXACTAS (tonos comunes) y sólo entra la nota nueva.
  {
    harmonyVoiceLeadReset();
    uint8_t tri[8];
    uint8_t nt = harmonyChord(0, CZ_CENTER, 0, MODE_IONIAN, tri);   // I tríada
    nt = harmonyVoiceLead(tri, nt);

    uint8_t add9[8];
    uint8_t na = harmonyChord(0, CZ_UP, 0, MODE_IONIAN, add9);      // I add9 (mismo grado)
    na = harmonyVoiceLead(add9, na);

    CHECK(na == nt + 1, "VL color: deberia entrar exactamente 1 nota (%d -> %d)", nt, na);
    // cada nota de la tríada debe seguir presente con el MISMO MIDI
    for (int i = 0; i < nt; i++) {
      bool kept = false;
      for (int j = 0; j < na; j++) if (add9[j] == tri[i]) { kept = true; break; }
      CHECK(kept, "VL color: la voz %d (%d) se movio al agregar add9", i, tri[i]);
    }
  }

  // 11) Presets de armonía: respetan las reglas duras (diatónico, no dominante,
  //     sin b9/b2) en todos los grados/tonos/modos; y los ricos suman notas.
  {
    for (int mi = 0; mi < 2; mi++) {
      Mode mode = modes[mi];
      for (int ti = 0; ti < 12; ti++) {
        int tonic = tonics[ti];
        for (int deg = 0; deg < 7; deg++) {
          for (uint8_t p = 0; p < HP_COUNT; p++) {
            uint8_t notes[8];
            uint8_t n = harmonyChordPreset((uint8_t)deg, CZ_CENTER, (uint8_t)tonic, mode, p, notes);
            CHECK(n >= 1 && n <= MAX_CHORD_NOTES, "preset %u: n=%d fuera de rango", p, n);
            int root = notes[0];
            bool maj3=false, b7=false;
            for (int i = 0; i < n; i++) {
              CHECK(inKey(notes[i], tonic, mode), "preset %u: nota no diatónica %d", p, notes[i]);
              int iv = (int)notes[i] - root;
              if (iv == 4)  maj3 = true;
              if (iv == 10) b7   = true;
              CHECK(iv != 1 && iv != 13, "preset %u: b2/b9 (deg %d)", p, deg);
            }
            CHECK(!(maj3 && b7), "preset %u: DOMINANTE (deg %d tonic %d modo %d)", p, deg, tonic, mi);
            // clases de altura distintas (sin duplicados dentro del acorde)
            for (int i = 0; i < n; i++) for (int j = i+1; j < n; j++)
              CHECK(notes[i] % 12 != notes[j] % 12, "preset %u: pc duplicada", p);
          }
        }
      }
    }
    // los presets ricos suman notas respecto al básico (en un acorde no-dominante)
    uint8_t a[8], b[8], c[8];
    uint8_t nB = harmonyChordPreset(0, CZ_CENTER, 0, MODE_IONIAN, HP_BASIC,   a); // I
    uint8_t n7 = harmonyChordPreset(0, CZ_CENTER, 0, MODE_IONIAN, HP_SEVENTH, b);
    uint8_t n9 = harmonyChordPreset(0, CZ_CENTER, 0, MODE_IONIAN, HP_NINTH,   c);
    CHECK(n7 > nB, "preset 7as no sumó nota (%d vs %d)", n7, nB);
    CHECK(n9 > n7, "preset 9as no sumó nota (%d vs %d)", n9, n7);
  }

  // 12) Símbolos de acorde para la pantalla.
  {
    char s[20];
    auto sym = [&](uint8_t deg, ColorZone z, uint8_t tonic, Mode mode, uint8_t preset) {
      harmonyChordSymbol(deg, z, tonic, mode, preset, s, sizeof(s)); return (const char*)s; };
    // Do mayor, color por zona (preset básico).
    CHECK(!strcmp(sym(0, CZ_CENTER, 0, MODE_IONIAN, HP_BASIC), "C"),     "sym I = %s", s);
    CHECK(!strcmp(sym(1, CZ_CENTER, 0, MODE_IONIAN, HP_BASIC), "Dm"),    "sym ii = %s", s);
    CHECK(!strcmp(sym(0, CZ_RIGHT,  0, MODE_IONIAN, HP_BASIC), "Cmaj7"), "sym I7 = %s", s);
    CHECK(!strcmp(sym(1, CZ_RIGHT,  0, MODE_IONIAN, HP_BASIC), "Dm7"),   "sym ii7 = %s", s);
    CHECK(!strcmp(sym(4, CZ_RIGHT,  0, MODE_IONIAN, HP_BASIC), "G6"),    "sym V(dom->6) = %s", s);
    CHECK(!strcmp(sym(0, CZ_UP,     0, MODE_IONIAN, HP_BASIC), "Cadd9"), "sym I add9 = %s", s);
    CHECK(!strcmp(sym(1, CZ_LEFT,   0, MODE_IONIAN, HP_BASIC), "Dsus4"), "sym ii sus4 = %s", s);
    // Preset 7as: el nombre debe mostrar la 7ma (color centro).
    CHECK(!strcmp(sym(0, CZ_CENTER, 0, MODE_IONIAN, HP_SEVENTH), "Cmaj7"), "sym I 7as = %s", s);
    CHECK(!strcmp(sym(1, CZ_CENTER, 0, MODE_IONIAN, HP_NINTH),   "Dm9"),   "sym ii 9as = %s", s);
    // Enarmonía: en Do menor el bIII es Eb (no D#).
    CHECK(!strcmp(sym(2, CZ_CENTER, 0, MODE_AEOLIAN, HP_BASIC), "Eb"), "enarmonia Cm bIII = %s", s);
    // Re menor (relativo de Fa) usa bemoles: bVI = Bb.
    CHECK(!strcmp(sym(5, CZ_CENTER, 2, MODE_AEOLIAN, HP_BASIC), "Bb"), "enarmonia Dm bVI = %s", s);
  }

  // 12b) Mapas de joystick (fila JOY): DEFAULT delega; HICHORD/MINI son
  // cromáticos OPT-IN pero mantienen invariantes: raíz diatónica del grado,
  // clases de altura sin duplicar, sin b2/b9, cantidad en rango.
  {
    uint8_t a[8], b[8];
    for (int mi = 0; mi < 2; mi++)
      for (int t = 0; t < 12; t++)
        for (int d = 0; d < 7; d++)
          for (int zi = 0; zi < 9; zi++) {
            // DEFAULT == harmonyChordQual, byte a byte.
            uint8_t n1 = harmonyChordQual((uint8_t)d, zones[zi], (uint8_t)t, modes[mi],
                                          HP_BASIC, CQ_DIATONIC, a);
            uint8_t n2 = harmonyChordJoy((uint8_t)d, zones[zi], (uint8_t)t, modes[mi],
                                         HP_BASIC, CQ_DIATONIC, JM_DEFAULT, b);
            CHECK(n1 == n2 && memcmp(a, b, n1) == 0,
                  "[JOY] DEFAULT no delega (deg %d zona %d)", d, zi);

            for (int jm = JM_HICHORD; jm <= JM_MINI; jm++) {
              uint8_t n = harmonyChordJoy((uint8_t)d, zones[zi], (uint8_t)t, modes[mi],
                                          HP_BASIC, CQ_DIATONIC, (uint8_t)jm, b);
              CHECK(n >= 3 && n <= MAX_CHORD_NOTES, "[JOY] mapa %d: n=%d", jm, n);
              CHECK(inKey(b[0], t, modes[mi]),
                    "[JOY] mapa %d: raíz no diatónica %d (deg %d)", jm, b[0], d);
              for (int i = 0; i < n; i++) {
                int iv = (int)b[i] - (int)b[0];
                CHECK(iv != 1 && iv != 13, "[JOY] mapa %d: b2/b9 (deg %d zona %d)", jm, d, zi);
                for (int j = i + 1; j < n; j++)
                  CHECK(b[i] % 12 != b[j] % 12, "[JOY] mapa %d: pc duplicada", jm);
              }
            }
          }

    // Tabla puntual en Do mayor. MINI = tipos fijos del minichord/Omnichord.
    auto eq = [&](uint8_t n, std::initializer_list<int> exp) {
      if (n != (uint8_t)exp.size()) return false;
      int i = 0; for (int v : exp) if (b[i++] != v) return false; return true; };
    uint8_t n;
    n = harmonyChordJoy(0, CZ_CENTER,     0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, b);
    CHECK(eq(n, {48,52,55}),    "[JOY] MINI centro != C");
    n = harmonyChordJoy(0, CZ_UP,         0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, b);
    CHECK(eq(n, {48,51,55}),    "[JOY] MINI up != Cm");
    n = harmonyChordJoy(0, CZ_RIGHT,      0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, b);
    CHECK(eq(n, {48,52,55,58}), "[JOY] MINI right != C7 (dominante opt-in)");
    n = harmonyChordJoy(0, CZ_UP_LEFT,    0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, b);
    CHECK(eq(n, {48,52,56}),    "[JOY] MINI up-left != Caug");
    n = harmonyChordJoy(0, CZ_DOWN_LEFT,  0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, b);
    CHECK(eq(n, {48,51,55,57}), "[JOY] MINI down-left != Cm6");

    // HICHORD = transformaciones del modo Default sobre la tríada del grado.
    n = harmonyChordJoy(0, CZ_UP,         0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {48,51,55}),    "[JOY] HICHORD I up != Cm (flip)");
    n = harmonyChordJoy(1, CZ_UP,         0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {50,54,57}),    "[JOY] HICHORD ii up != D (flip)");
    n = harmonyChordJoy(1, CZ_LEFT,       0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {50,53,56}),    "[JOY] HICHORD ii left != Ddim (oscurece)");
    n = harmonyChordJoy(0, CZ_RIGHT,      0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {48,52,55,59}), "[JOY] HICHORD I right != Cmaj7");
    n = harmonyChordJoy(1, CZ_RIGHT,      0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {50,53,57,60}), "[JOY] HICHORD ii right != Dm7");
    n = harmonyChordJoy(0, CZ_DOWN,       0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {48,53,55}),    "[JOY] HICHORD I down != Csus4");
    n = harmonyChordJoy(0, CZ_UP_RIGHT,   0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {48,52,55,58}), "[JOY] HICHORD I up-right != C7 (blues)");
    n = harmonyChordJoy(6, CZ_RIGHT,      0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {59,62,65,69}), "[JOY] HICHORD vii right != Bm7b5");
    n = harmonyChordJoy(1, CZ_DOWN_LEFT,  0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, b);
    CHECK(eq(n, {50,52,57}),    "[JOY] HICHORD ii down-left != Dsus2");

    // CQ_DOM7 (mayorizar por grado) sigue mandando con cualquier mapa.
    uint8_t nd = harmonyChordQual(4, CZ_CENTER, 0, MODE_IONIAN, HP_BASIC, CQ_DOM7, a);
    n = harmonyChordJoy(4, CZ_CENTER, 0, MODE_IONIAN, HP_BASIC, CQ_DOM7, JM_MINI, b);
    CHECK(nd == n && memcmp(a, b, n) == 0, "[JOY] CQ_DOM7 no manda sobre el mapa");

    // Símbolos con mapa (aug/m#5 incluidos).
    char s[20];
    harmonyChordSymbolJoy(0, CZ_UP_LEFT,   0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, s, sizeof(s));
    CHECK(!strcmp(s, "Caug"), "[JOY] sym MINI aug = %s", s);
    harmonyChordSymbolJoy(0, CZ_DOWN_LEFT, 0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, s, sizeof(s));
    CHECK(!strcmp(s, "Cm6"),  "[JOY] sym MINI m6 = %s", s);
    harmonyChordSymbolJoy(0, CZ_RIGHT,     0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_MINI, s, sizeof(s));
    CHECK(!strcmp(s, "C7"),   "[JOY] sym MINI 7 = %s", s);
    harmonyChordSymbolJoy(1, CZ_LEFT,      0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, s, sizeof(s));
    CHECK(!strcmp(s, "Ddim"), "[JOY] sym HICHORD dim = %s", s);
    harmonyChordSymbolJoy(5, CZ_UP_LEFT,   0, MODE_IONIAN, HP_BASIC, CQ_DIATONIC, JM_HICHORD, s, sizeof(s));
    CHECK(!strcmp(s, "Am#5"), "[JOY] sym HICHORD m#5 = %s", s);
    printf("  joymaps    DEFAULT delega + HICHORD/MINI verificados (tabla C mayor)\n");
  }

  // 13) Secuencia del arpegiador (octavas + scale-run).
  {
    uint8_t seq[32];
    uint8_t chord[3] = { 60, 64, 67 };   // C4 E4 G4 (Do mayor)

    // 1 octava, sin scale-run -> las notas del acorde, ascendentes.
    uint8_t n1 = harmonyArpSequence(chord, 3, 1, false, 0, MODE_IONIAN, seq, 32);
    CHECK(n1 == 3 && seq[0]==60 && seq[1]==64 && seq[2]==67, "[ARPSEQ] 1oct chord mal (n=%d)", n1);

    // 2 octavas, sin scale-run -> acorde + su copia una octava arriba, ascendente.
    uint8_t n2 = harmonyArpSequence(chord, 3, 2, false, 0, MODE_IONIAN, seq, 32);
    CHECK(n2 == 6 && seq[3]==72 && seq[5]==79, "[ARPSEQ] 2oct chord mal (n=%d)", n2);
    for (int i = 1; i < n2; i++) CHECK(seq[i] > seq[i-1], "[ARPSEQ] no es ascendente");

    // scale-run 1 octava en Do mayor -> los 7 grados desde C4: C D E F G A B.
    uint8_t ns = harmonyArpSequence(chord, 3, 1, true, 0, MODE_IONIAN, seq, 32);
    uint8_t expSc[7] = { 60, 62, 64, 65, 67, 69, 71 };
    bool okSc = (ns == 7);
    for (int i = 0; i < 7 && okSc; i++) if (seq[i] != expSc[i]) okSc = false;
    CHECK(okSc, "[ARPSEQ] scale-run C mayor mal (n=%d s0=%d s1=%d)", ns, seq[0], seq[1]);

    // scale-run en Do menor (Eb, Ab, Bb) -> C D Eb F G Ab Bb.
    uint8_t nm = harmonyArpSequence(chord, 3, 1, true, 0, MODE_AEOLIAN, seq, 32);
    CHECK(nm == 7 && seq[2] == 63 && seq[5] == 68 && seq[6] == 70,
          "[ARPSEQ] scale-run C menor mal (Eb=%d Ab=%d Bb=%d)", seq[2], seq[5], seq[6]);

    // Todas las notas de scale-run son diatónicas (regla dura del proyecto).
    const int SCmin[7] = { 0, 2, 3, 5, 7, 8, 10 };
    for (int i = 0; i < nm; i++) {
      int pc = seq[i] % 12; bool diat = false;
      for (int d = 0; d < 7; d++) if (SCmin[d] == pc) diat = true;
      CHECK(diat, "[ARPSEQ] nota no diatónica en scale-run: %d", seq[i]);
    }
    printf("  arpseq     1oct=%d 2oct=%d scaleMaj=%d scaleMin=%d (diatónico)\n", n1, n2, ns, nm);
  }

  printf("\n%d checks, %d fallas\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
