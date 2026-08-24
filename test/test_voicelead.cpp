/* ============================================================================
   test_voicelead.cpp  -  Test de host de la CONDUCCIÓN DE VOCES seleccionable.
   Verifica los 4 algoritmos (VL_PARALLEL/SMOOTH/CHORALE/CONTRARY) contra los
   requisitos de la especificación. harmony.cpp es C++ puro (se compila en PC).

   Compilar/correr (lo hace test/run_tests.ps1):
     g++ -std=c++17 -I.. test_voicelead.cpp ../harmony.cpp -o test_voicelead
     ./test_voicelead
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "harmony.h"   // trae state.h (VoiceLeadMode, ColorZone, Mode)
#include "config.h"    // MAX_CHORD_NOTES

// Contador de candidatos evaluados por la búsqueda coral (definido en harmony.cpp
// como global no-static; cota de rendimiento verificable, sólo para host).
extern long gVlCandidatesEvaluated;

static int checks = 0, failures = 0;
#define CHECK(cond, msg, ...) do { checks++; if(!(cond)) { failures++; \
  printf("FAIL: " msg "\n", ##__VA_ARGS__); } } while(0)

// Tablas y métricas: RUIDO opcional. La suite normal queda silenciosa (sólo el
// resumen y los FAIL). Compilar con -DVL_VERBOSE para ver tablas/métricas/bench.
#ifdef VL_VERBOSE
  #define VPRINT(...) printf(__VA_ARGS__)
#else
  #define VPRINT(...) ((void)0)
#endif

static const int SC_MAJ[7] = {0, 2, 4, 5, 7, 9, 11};
static const int SC_MIN[7] = {0, 2, 3, 5, 7, 8, 10};

static bool inKey(int midi, int tonic, Mode mode) {
  const int* sc = (mode == MODE_AEOLIAN) ? SC_MIN : SC_MAJ;
  int pc = ((midi - tonic) % 12 + 12) % 12;
  for (int i = 0; i < 7; i++) if (sc[i] == pc) return true;
  return false;
}

// Nombre de nota con octava (C4 = MIDI 60) para las tablas del reporte.
static const char* NN[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static void midiName(int m, char* out) {
  sprintf(out, "%s%d", NN[((m % 12) + 12) % 12], m / 12 - 1);
}

// Máscara de clases de altura (multiconjunto -> conjunto; los acordes de entrada
// tienen clases distintas, así que la igualdad de conjuntos valida el contenido).
static int pcMask(const uint8_t* a, int n) {
  int mask = 0; for (int i = 0; i < n; i++) mask |= (1 << (a[i] % 12)); return mask;
}

// Arma el acorde diatónico (tríada + preset) del grado y devuelve rootPc aparte.
static uint8_t buildChord(int deg, uint8_t tonic, Mode mode, uint8_t preset,
                          uint8_t* notes, int* rootPc) {
  uint8_t n = harmonyChordPreset((uint8_t)deg, CZ_CENTER, tonic, mode, preset, notes);
  *rootPc = notes[0] % 12;
  return n;
}

// Corre una progresión completa por un algoritmo; opcionalmente vuelca las notas.
struct StepOut { uint8_t notes[MAX_CHORD_NOTES]; uint8_t n; };
static void runProg(const int* degs, int steps, uint8_t tonic, Mode mode,
                    uint8_t preset, VoiceLeadMode mode_vl, StepOut* out) {
  harmonyVoiceLeadReset();
  // Fuerza el arranque limpio del algoritmo (el cambio de modo reinicia historial).
  for (int s = 0; s < steps; s++) {
    uint8_t notes[MAX_CHORD_NOTES]; int rootPc;
    uint8_t n = buildChord(degs[s], tonic, mode, preset, notes, &rootPc);
    n = harmonyVoiceLeadMode(notes, n, (uint8_t)rootPc, mode_vl);
    out[s].n = n;
    for (int i = 0; i < n; i++) out[s].notes[i] = notes[i];
  }
}

// ¿Estrictamente ascendente? (sin cruces / sin unísonos).
static bool ascending(const StepOut& s) {
  for (int i = 1; i < s.n; i++) if (s.notes[i] <= s.notes[i - 1]) return false;
  return true;
}

int main() {
  // Progresiones de referencia.
  int I_V_vi_IV[4] = { 0, 4, 5, 3 };
  int ii_V_I[3]    = { 1, 4, 0 };
  int i_VI_III_VII[4] = { 0, 5, 2, 6 };   // Am (eólico)

  // -------------------------------------------------------------------------
  // 1) VL_SMOOTH == algoritmo histórico: el wrapper harmonyVoiceLead() y el
  //    modo VL_SMOOTH producen EXACTAMENTE las mismas notas.
  // -------------------------------------------------------------------------
  {
    harmonyVoiceLeadReset();
    StepOut viaMode[4];
    runProg(I_V_vi_IV, 4, 0, MODE_IONIAN, HP_BASIC, VL_SMOOTH, viaMode);

    // Camino histórico exacto: harmonyChord + harmonyVoiceLead() (wrapper).
    harmonyVoiceLeadReset();
    for (int s = 0; s < 4; s++) {
      uint8_t notes[MAX_CHORD_NOTES];
      uint8_t n = harmonyChordPreset((uint8_t)I_V_vi_IV[s], CZ_CENTER, 0, MODE_IONIAN, HP_BASIC, notes);
      n = harmonyVoiceLead(notes, n);
      CHECK(n == viaMode[s].n, "[SMOOTH] n distinto en paso %d (%d vs %d)", s, n, viaMode[s].n);
      for (int i = 0; i < n; i++)
        CHECK(notes[i] == viaMode[s].notes[i],
              "[SMOOTH] wrapper != VL_SMOOTH paso %d voz %d (%d vs %d)",
              s, i, notes[i], viaMode[s].notes[i]);
    }
  }

  // -------------------------------------------------------------------------
  // 2) VL_PARALLEL no usa el acorde anterior: el mismo acorde da idéntico
  //    resultado sin importar qué se tocó antes.
  // -------------------------------------------------------------------------
  {
    // Contexto A: I -> V.
    harmonyVoiceLeadReset();
    uint8_t a[MAX_CHORD_NOTES]; int rp;
    uint8_t na = buildChord(0, 0, MODE_IONIAN, HP_BASIC, a, &rp);
    harmonyVoiceLeadMode(a, na, (uint8_t)rp, VL_PARALLEL);   // I como previo
    uint8_t v1[MAX_CHORD_NOTES]; int rpv;
    uint8_t nv1 = buildChord(4, 0, MODE_IONIAN, HP_BASIC, v1, &rpv);
    nv1 = harmonyVoiceLeadMode(v1, nv1, (uint8_t)rpv, VL_PARALLEL);

    // Contexto B: vi -> V (distinto previo, incluso otro algoritmo antes).
    harmonyVoiceLeadReset();
    uint8_t b[MAX_CHORD_NOTES]; int rb;
    uint8_t nb = buildChord(5, 0, MODE_IONIAN, HP_NINTH, b, &rb);
    harmonyVoiceLeadMode(b, nb, (uint8_t)rb, VL_CHORALE);    // otro modo/previo
    uint8_t v2[MAX_CHORD_NOTES]; int rpv2;
    uint8_t nv2 = buildChord(4, 0, MODE_IONIAN, HP_BASIC, v2, &rpv2);
    nv2 = harmonyVoiceLeadMode(v2, nv2, (uint8_t)rpv2, VL_PARALLEL);

    CHECK(nv1 == nv2, "[PARALLEL] n depende del previo (%d vs %d)", nv1, nv2);
    for (int i = 0; i < nv1; i++)
      CHECK(v1[i] == v2[i], "[PARALLEL] voz %d depende del previo (%d vs %d)", i, v1[i], v2[i]);
  }

  // -------------------------------------------------------------------------
  // 3) VL_PARALLEL mantiene la fundamental como voz más grave (antes de voicing).
  // -------------------------------------------------------------------------
  {
    for (int mi = 0; mi < 2; mi++)
      for (int t = 0; t < 12; t++)
        for (int d = 0; d < 7; d++) {
          harmonyVoiceLeadReset();
          uint8_t notes[MAX_CHORD_NOTES]; int rootPc;
          uint8_t n = buildChord(d, (uint8_t)t, mi ? MODE_AEOLIAN : MODE_IONIAN, HP_NINTH, notes, &rootPc);
          n = harmonyVoiceLeadMode(notes, n, (uint8_t)rootPc, VL_PARALLEL);
          int lo = notes[0]; for (int i = 1; i < n; i++) if (notes[i] < lo) lo = notes[i];
          CHECK(lo == notes[0], "[PARALLEL] la fundamental no es la más grave (t%d d%d)", t, d);
          CHECK((lo % 12) == rootPc, "[PARALLEL] la voz grave no es la fundamental (t%d d%d)", t, d);
        }
  }

  // -------------------------------------------------------------------------
  // 4+5+7) CHORALE/CONTRARY: rango 36..91, sin cruces, en I–V–vi–IV y ii–V–I,
  //        en las 12 tonalidades y en mayor/menor. Todo diatónico.
  // -------------------------------------------------------------------------
  {
    VoiceLeadMode cho[2] = { VL_CHORALE, VL_CONTRARY };
    for (int cm = 0; cm < 2; cm++)
      for (int mi = 0; mi < 2; mi++) {
        Mode mode = mi ? MODE_AEOLIAN : MODE_IONIAN;
        for (int t = 0; t < 12; t++) {
          StepOut o4[4], o3[3];
          runProg(I_V_vi_IV, 4, (uint8_t)t, mode, HP_BASIC, cho[cm], o4);
          runProg(ii_V_I,    3, (uint8_t)t, mode, HP_SEVENTH, cho[cm], o3);
          for (int s = 0; s < 4; s++) {
            CHECK(ascending(o4[s]), "[CHO%d] cruce en I-V-vi-IV t%d s%d modo%d", cm, t, s, mi);
            for (int i = 0; i < o4[s].n; i++) {
              CHECK(o4[s].notes[i] >= 36 && o4[s].notes[i] <= 91,
                    "[CHO%d] fuera de 36..91: %d (t%d s%d)", cm, o4[s].notes[i], t, s);
              CHECK(inKey(o4[s].notes[i], t, mode),
                    "[CHO%d] no diatónica %d (t%d s%d modo%d)", cm, o4[s].notes[i], t, s, mi);
            }
          }
          for (int s = 0; s < 3; s++) {
            CHECK(ascending(o3[s]), "[CHO%d] cruce en ii-V-I t%d s%d modo%d", cm, t, s, mi);
            for (int i = 0; i < o3[s].n; i++)
              CHECK(o3[s].notes[i] >= 36 && o3[s].notes[i] <= 91,
                    "[CHO%d] fuera de rango ii-V-I: %d", cm, o3[s].notes[i]);
          }
        }
      }
    // Progresión menor Am i–VI–III–VII en las dos infra (registro + diatónico).
    for (int cm = 0; cm < 2; cm++) {
      StepOut o[4];
      runProg(i_VI_III_VII, 4, 9, MODE_AEOLIAN, HP_BASIC, cho[cm], o);
      for (int s = 0; s < 4; s++) {
        CHECK(ascending(o[s]), "[CHO%d] cruce en Am s%d", cm, s);
        for (int i = 0; i < o[s].n; i++) {
          CHECK(o[s].notes[i] >= 36 && o[s].notes[i] <= 91, "[CHO%d] Am fuera de rango: %d", cm, o[s].notes[i]);
          CHECK(inKey(o[s].notes[i], 9, MODE_AEOLIAN), "[CHO%d] Am no diatónica: %d", cm, o[s].notes[i]);
        }
      }
    }
  }

  // -------------------------------------------------------------------------
  // 6) CHORALE conserva tonos comunes: I (C E G) -> vi (A C E) mantiene C y E
  //    en su octava exacta.
  // -------------------------------------------------------------------------
  {
    harmonyVoiceLeadReset();
    uint8_t I[MAX_CHORD_NOTES]; int rI;
    uint8_t nI = buildChord(0, 0, MODE_IONIAN, HP_BASIC, I, &rI);
    nI = harmonyVoiceLeadMode(I, nI, (uint8_t)rI, VL_CHORALE);
    uint8_t vi[MAX_CHORD_NOTES]; int rvi;
    uint8_t nvi = buildChord(5, 0, MODE_IONIAN, HP_BASIC, vi, &rvi);
    nvi = harmonyVoiceLeadMode(vi, nvi, (uint8_t)rvi, VL_CHORALE);
    // clases comunes I∩vi = {C(0), E(4)}. Deben seguir sonando al MISMO MIDI.
    for (int pc = 0; pc < 12; pc++) {
      bool inI = false, inVI = false; int midiI = -1;
      for (int i = 0; i < nI; i++) if (I[i] % 12 == pc) { inI = true; midiI = I[i]; }
      for (int i = 0; i < nvi; i++) if (vi[i] % 12 == pc) inVI = true;
      if (inI && inVI) {
        bool held = false; for (int i = 0; i < nvi; i++) if (vi[i] == midiI) held = true;
        CHECK(held, "[CHORALE] tono común pc=%d no conservado (era %d)", pc, midiI);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 8) Agregar 7ma no corrompe la identidad de las demás voces: CHORALE en la
  //    tríada I y luego I7 (misma raíz) conserva las clases comunes en su MIDI.
  // -------------------------------------------------------------------------
  {
    harmonyVoiceLeadReset();
    uint8_t tri[MAX_CHORD_NOTES]; int rt;
    uint8_t nt = buildChord(0, 0, MODE_IONIAN, HP_BASIC, tri, &rt);
    nt = harmonyVoiceLeadMode(tri, nt, (uint8_t)rt, VL_CHORALE);
    uint8_t se[MAX_CHORD_NOTES]; int rs;
    uint8_t ns = buildChord(0, 0, MODE_IONIAN, HP_SEVENTH, se, &rs);
    ns = harmonyVoiceLeadMode(se, ns, (uint8_t)rs, VL_CHORALE);
    CHECK(ns >= nt, "[CHORALE] la 7ma no sumó voz (%d -> %d)", nt, ns);
    for (int i = 0; i < nt; i++) {
      int pc = tri[i] % 12;
      bool stillThere = false;
      for (int j = 0; j < ns; j++) if (se[j] == tri[i]) stillThere = true;
      // Todas las clases de la tríada están en I7; deben mantenerse en su MIDI.
      (void)pc;
      CHECK(stillThere, "[CHORALE] la voz %d (%d) se movió al sumar 7ma", i, tri[i]);
    }
  }

  // -------------------------------------------------------------------------
  // 9) Quintas/octavas paralelas: en I–IV–V–I (C) no debe haber 5tas u 8vas
  //    paralelas entre voces adyacentes en pasos consecutivos (el costo las
  //    penaliza). Comparación: mismo desplazamiento con signo + intervalo 5/8.
  // -------------------------------------------------------------------------
  {
    int prog[4] = { 0, 3, 4, 0 };
    StepOut o[4];
    runProg(prog, 4, 0, MODE_IONIAN, HP_BASIC, VL_CHORALE, o);
    for (int s = 1; s < 4; s++) {
      if (o[s].n != o[s - 1].n) continue;
      for (int k = 0; k + 1 < o[s].n; k++) {
        int prevInt = o[s - 1].notes[k + 1] - o[s - 1].notes[k];
        int curInt  = o[s].notes[k + 1]     - o[s].notes[k];
        int mvLo = o[s].notes[k]     - o[s - 1].notes[k];
        int mvHi = o[s].notes[k + 1] - o[s - 1].notes[k + 1];
        bool parallel = (mvLo != 0 && mvHi != 0 && mvLo == mvHi);
        int pm = ((prevInt % 12) + 12) % 12, cm = ((curInt % 12) + 12) % 12;
        bool par5 = parallel && pm == 7 && cm == 7;
        bool par8 = parallel && pm == 0 && cm == 0;
        CHECK(!par5, "[CHORALE] 5ta paralela adyacente en paso %d voces %d-%d", s, k, k + 1);
        CHECK(!par8, "[CHORALE] 8va paralela adyacente en paso %d voces %d-%d", s, k, k + 1);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 10) CONTRARY elige MÁS movimiento contrario exterior que CHORALE/SMOOTH en
  //     una progresión preparada (bajo ascendente), sin salir de registro.
  // -------------------------------------------------------------------------
  {
    int prog[6] = { 0, 1, 2, 3, 4, 0 };   // bajo tiende a subir por grados
    auto outerContrary = [&](VoiceLeadMode m) {
      StepOut o[6];
      runProg(prog, 6, 0, MODE_IONIAN, HP_BASIC, m, o);
      int cnt = 0;
      for (int s = 1; s < 6; s++) {
        int db = o[s].notes[0] - o[s - 1].notes[0];
        int ds = o[s].notes[o[s].n - 1] - o[s - 1].notes[o[s].n - 1];
        if (db != 0 && ds != 0 && ((db > 0) != (ds > 0))) cnt++;
        for (int i = 0; i < o[s].n; i++)
          CHECK(o[s].notes[i] >= 36 && o[s].notes[i] <= 91, "[CONTRARY] fuera de registro %d", o[s].notes[i]);
      }
      return cnt;
    };
    int cCon = outerContrary(VL_CONTRARY);
    int cCho = outerContrary(VL_CHORALE);
    int cSmo = outerContrary(VL_SMOOTH);
    VPRINT("  contrary   exteriores: CONTRARY=%d CHORALE=%d SMOOTH=%d\n", cCon, cCho, cSmo);
    CHECK(cCon >= cCho, "[CONTRARY] no supera a CHORALE (%d < %d)", cCon, cCho);
    CHECK(cCon >= cSmo, "[CONTRARY] no supera a SMOOTH (%d < %d)", cCon, cSmo);
    CHECK(cCon > 0, "[CONTRARY] no logró ningún movimiento contrario exterior");
  }

  // -------------------------------------------------------------------------
  // 11) Cambiar de algoritmo reinicia el historial: tras CHORALE en un acorde
  //     cualquiera, el primer acorde en SMOOTH se coloca como "primer acorde"
  //     (independiente de la historia de CHORALE).
  // -------------------------------------------------------------------------
  {
    // Referencia: SMOOTH como PRIMER acorde de X.
    harmonyVoiceLeadReset();
    uint8_t ref[MAX_CHORD_NOTES]; int rr;
    uint8_t nref = buildChord(2, 0, MODE_IONIAN, HP_BASIC, ref, &rr);
    nref = harmonyVoiceLeadMode(ref, nref, (uint8_t)rr, VL_SMOOTH);

    // Ahora: CHORALE en otro acorde Y, luego SMOOTH en X (cambio de modo).
    harmonyVoiceLeadReset();
    uint8_t y[MAX_CHORD_NOTES]; int ry;
    uint8_t ny = buildChord(5, 0, MODE_IONIAN, HP_NINTH, y, &ry);
    harmonyVoiceLeadMode(y, ny, (uint8_t)ry, VL_CHORALE);
    uint8_t x[MAX_CHORD_NOTES]; int rx;
    uint8_t nx = buildChord(2, 0, MODE_IONIAN, HP_BASIC, x, &rx);
    nx = harmonyVoiceLeadMode(x, nx, (uint8_t)rx, VL_SMOOTH);

    CHECK(nx == nref, "[RESET] n distinto tras cambio de modo (%d vs %d)", nx, nref);
    for (int i = 0; i < nx; i++)
      CHECK(x[i] == ref[i], "[RESET] el historial NO se reinició al cambiar de algoritmo (voz %d: %d vs %d)",
            i, x[i], ref[i]);
  }

  // -------------------------------------------------------------------------
  // 12) Modo fuera de rango cae a VL_SMOOTH (equivalente a lo persistido inválido).
  // -------------------------------------------------------------------------
  {
    harmonyVoiceLeadReset();
    uint8_t a[MAX_CHORD_NOTES]; int ra;
    uint8_t na = buildChord(0, 0, MODE_IONIAN, HP_BASIC, a, &ra);
    na = harmonyVoiceLeadMode(a, na, (uint8_t)ra, (VoiceLeadMode)99);   // inválido
    harmonyVoiceLeadReset();
    uint8_t b[MAX_CHORD_NOTES]; int rb;
    uint8_t nb = buildChord(0, 0, MODE_IONIAN, HP_BASIC, b, &rb);
    nb = harmonyVoiceLeadMode(b, nb, (uint8_t)rb, VL_SMOOTH);
    CHECK(na == nb, "[FALLBACK] modo inválido no cae a SMOOTH (n)");
    for (int i = 0; i < na; i++)
      CHECK(a[i] == b[i], "[FALLBACK] modo inválido != SMOOTH voz %d (%d vs %d)", i, a[i], b[i]);
  }

  // -------------------------------------------------------------------------
  // 13) REGRESIÓN EXACTA de SMOOTH: salidas históricas congeladas como valores
  //     literales (si SMOOTH cambiara, este test falla). Capturadas del motor.
  // -------------------------------------------------------------------------
  {
    struct RegP { const char* name; const int* d; int n; uint8_t tonic; Mode mode;
                  int exp[4][MAX_CHORD_NOTES]; int expN[4]; };
    RegP regs[3] = {
      { "C I-V-vi-IV", I_V_vi_IV, 4, 0, MODE_IONIAN,
        { {60,64,67},{67,59,62},{69,60,64},{65,69,60} }, {3,3,3,3} },
      { "C ii-V-I", ii_V_I, 3, 0, MODE_IONIAN,
        { {62,65,69},{67,71,62},{72,64,67} }, {3,3,3} },
      { "Am i-VI-III-VII", i_VI_III_VII, 4, 9, MODE_AEOLIAN,
        { {57,60,64},{65,57,60},{60,64,55},{55,59,62} }, {3,3,3,3} },
    };
    for (int p = 0; p < 3; p++) {
      StepOut o[4];
      runProg(regs[p].d, regs[p].n, regs[p].tonic, regs[p].mode, HP_BASIC, VL_SMOOTH, o);
      for (int s = 0; s < regs[p].n; s++) {
        CHECK(o[s].n == regs[p].expN[s], "[SMOOTH-REG] %s paso %d n=%d!=%d", regs[p].name, s, o[s].n, regs[p].expN[s]);
        for (int i = 0; i < regs[p].expN[s]; i++)
          CHECK(o[s].notes[i] == regs[p].exp[s][i],
                "[SMOOTH-REG] %s paso %d voz %d = %d (esperado %d)",
                regs[p].name, s, i, o[s].notes[i], regs[p].exp[s][i]);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 14) DETERMINISMO: misma entrada + mismo estado -> misma salida (los 4 modos).
  //     Desempate reproducible: dos corridas idénticas dan bytes idénticos.
  // -------------------------------------------------------------------------
  {
    VoiceLeadMode all[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    int prog[4] = { 0, 3, 4, 5 };
    for (int a = 0; a < 4; a++) {
      StepOut r1[4], r2[4];
      runProg(prog, 4, 7, MODE_IONIAN, HP_NINTH, all[a], r1);   // Sol mayor, 9nas
      runProg(prog, 4, 7, MODE_IONIAN, HP_NINTH, all[a], r2);
      for (int s = 0; s < 4; s++) {
        CHECK(r1[s].n == r2[s].n, "[DET] modo %d paso %d n distinto", a, s);
        for (int i = 0; i < r1[s].n; i++)
          CHECK(r1[s].notes[i] == r2[s].notes[i], "[DET] modo %d no determinista (paso %d voz %d)", a, s, i);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 15) Cambio de cantidad de voces 3->4->5->3 (tríada->7ma->9na->tríada) en
  //     CHORALE/CONTRARY: invariantes (rango, sin cruces) y tonos comunes de la
  //     tríada base retenidos al volver. No debe corromperse ni colgarse.
  // -------------------------------------------------------------------------
  {
    uint8_t presets[4] = { HP_BASIC, HP_SEVENTH, HP_NINTH, HP_BASIC };
    int expCount[4] = { 3, 4, 5, 3 };
    VoiceLeadMode cho[2] = { VL_CHORALE, VL_CONTRARY };
    for (int cm = 0; cm < 2; cm++) {
      harmonyVoiceLeadReset();
      StepOut first{}, last{};
      for (int s = 0; s < 4; s++) {
        uint8_t notes[MAX_CHORD_NOTES]; int rp;
        uint8_t nn = buildChord(0, 0, MODE_IONIAN, presets[s], notes, &rp);   // siempre grado I
        nn = harmonyVoiceLeadMode(notes, nn, (uint8_t)rp, cho[cm]);
        CHECK(nn == expCount[s], "[3454] modo%d paso%d n=%d!=%d", cm, s, nn, expCount[s]);
        StepOut o; o.n = nn; for (int i = 0; i < nn; i++) o.notes[i] = notes[i];
        CHECK(ascending(o), "[3454] modo%d paso%d cruce", cm, s);
        for (int i = 0; i < nn; i++)
          CHECK(o.notes[i] >= 36 && o.notes[i] <= 91, "[3454] modo%d fuera de rango %d", cm, o.notes[i]);
        if (s == 0) first = o;
        if (s == 3) last  = o;
      }
      // Al volver a la tríada, sus clases comunes con la 9na anterior siguen
      // presentes y el registro es válido (no exigimos igualdad exacta con la
      // primera tríada porque el historial cambió, pero sí que suene la tríada).
      CHECK(last.n == first.n, "[3454] modo%d la tríada final cambió de tamaño", cm);
    }
  }

  // -------------------------------------------------------------------------
  // 16) Movimiento OBLICUO no es paralelo: si una voz queda quieta, el par NO se
  //     cuenta como 5ta/8va paralela. Se bloquea el predicado (misma dirección
  //     no nula) para documentar la definición usada por el motor.
  // -------------------------------------------------------------------------
  {
    auto isParallel = [](int prevLo, int prevHi, int curLo, int curHi) {
      int prevInt = prevHi - prevLo, curInt = curHi - curLo;
      int mvLo = curLo - prevLo, mvHi = curHi - prevHi;
      bool sameDir = (mvLo > 0 && mvHi > 0) || (mvLo < 0 && mvHi < 0);
      if (!sameDir) return 0;
      int pm = ((prevInt % 12) + 12) % 12, cm = ((curInt % 12) + 12) % 12;
      if (pm == 0 && cm == 0) return 8;
      if (pm == 7 && cm == 7) return 5;
      return 0;
    };
    // Oblicuo: bajo quieto (C3=48), voz sup baja de G3(55) a G3? mueve sola.
    CHECK(isParallel(48, 55, 48, 62) == 0, "[OBLIQUE] voz grave quieta contada como paralela");
    CHECK(isParallel(48, 55, 62, 62) == 0, "[OBLIQUE] voz aguda quieta (mvHi=0) contada como paralela");
    // Contrario: 5tas que se abren en direcciones opuestas -> no paralelo.
    CHECK(isParallel(48, 55, 46, 57) == 0, "[OBLIQUE] contrario contado como paralelo");
    // Paralelo real: ambas suben 2 st, 5ta->5ta -> detecta 5 (5ta paralela).
    CHECK(isParallel(48, 55, 50, 57) == 5, "[OBLIQUE] 5ta paralela real no detectada");
    // Paralelo real de 8va: ambas bajan 1 st, 8va->8va -> detecta 8.
    CHECK(isParallel(48, 60, 47, 59) == 8, "[OBLIQUE] 8va paralela real no detectada");
    // Misma dirección, distinta cantidad, 5ta->5ta compuesta -> paralelo.
    CHECK(isParallel(48, 55, 50, 69) == 5, "[OBLIQUE] 5ta paralela (compuesta, misma dir) no detectada");
  }

  // -------------------------------------------------------------------------
  // 17) Progresión CROMÁTICA por JM_HICHORD (opt-in, no diatónica): los modos
  //     mantienen invariantes duros (rango 36-91, sin cruces) y son deterministas
  //     aun con acordes fuera de escala (dom7, sus, aug, add9...).
  // -------------------------------------------------------------------------
  {
    // Zonas del HiChord que producen calidades cromáticas sobre varios grados.
    ColorZone zc[5] = { CZ_UP_RIGHT, CZ_RIGHT, CZ_DOWN_LEFT, CZ_UP_LEFT, CZ_DOWN_RIGHT };
    int dg[5]       = { 0, 4, 1, 5, 2 };
    VoiceLeadMode all[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    for (int a = 0; a < 4; a++) {
      StepOut r1[5], r2[5];
      harmonyVoiceLeadReset();
      for (int s = 0; s < 5; s++) {
        uint8_t notes[MAX_CHORD_NOTES];
        uint8_t nn = harmonyChordJoy((uint8_t)dg[s], zc[s], 0, MODE_IONIAN,
                                     HP_BASIC, CQ_DIATONIC, JM_HICHORD, notes);
        uint8_t rp = notes[0] % 12;
        nn = harmonyVoiceLeadMode(notes, nn, rp, all[a]);
        r1[s].n = nn; for (int i = 0; i < nn; i++) r1[s].notes[i] = notes[i];
        // "Sin cruces" es garantía de CHORALE/CONTRARY (no de SMOOTH, cuyo orden
        // es por-voz, ni obligatorio en PARALLEL más allá de su apilado).
        if (all[a] == VL_CHORALE || all[a] == VL_CONTRARY)
          CHECK(ascending(r1[s]), "[CHROM] modo%d paso%d cruce", a, s);
        for (int i = 0; i < nn; i++)
          CHECK(r1[s].notes[i] >= 36 && r1[s].notes[i] <= 91, "[CHROM] modo%d fuera de rango %d", a, r1[s].notes[i]);
      }
      // Determinista también con material cromático.
      harmonyVoiceLeadReset();
      for (int s = 0; s < 5; s++) {
        uint8_t notes[MAX_CHORD_NOTES];
        uint8_t nn = harmonyChordJoy((uint8_t)dg[s], zc[s], 0, MODE_IONIAN,
                                     HP_BASIC, CQ_DIATONIC, JM_HICHORD, notes);
        uint8_t rp = notes[0] % 12;
        nn = harmonyVoiceLeadMode(notes, nn, rp, all[a]);
        r2[s].n = nn; for (int i = 0; i < nn; i++) r2[s].notes[i] = notes[i];
        CHECK(r1[s].n == r2[s].n, "[CHROM] modo%d paso%d n no determinista", a, s);
        for (int i = 0; i < nn; i++)
          CHECK(r1[s].notes[i] == r2[s].notes[i], "[CHROM] modo%d no determinista paso%d voz%d", a, s, i);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 18) CONTENIDO ARMÓNICO: el conjunto de clases de altura de salida coincide
  //     EXACTAMENTE con el del acorde de entrada (no se pierde 3ra/5ta/7ma/9na
  //     ni aparecen clases ajenas). rootPc sólo guía el bajo, no el contenido.
  //     Tríadas (HP_BASIC), tétradas (HP_SEVENTH) y 5 notas (HP_NINTH).
  // -------------------------------------------------------------------------
  {
    uint8_t presets[3] = { HP_BASIC, HP_SEVENTH, HP_NINTH };
    VoiceLeadMode all[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    for (int pi = 0; pi < 3; pi++)
      for (int a = 0; a < 4; a++)
        for (int mi = 0; mi < 2; mi++)
          for (int t = 0; t < 12; t++)
            for (int d = 0; d < 7; d++) {
              harmonyVoiceLeadReset();
              uint8_t in[MAX_CHORD_NOTES]; int rp;
              uint8_t ni = buildChord(d, (uint8_t)t, mi ? MODE_AEOLIAN : MODE_IONIAN, presets[pi], in, &rp);
              int inMask = pcMask(in, ni);
              uint8_t out[MAX_CHORD_NOTES]; for (int i = 0; i < ni; i++) out[i] = in[i];
              uint8_t no = harmonyVoiceLeadMode(out, ni, (uint8_t)rp, all[a]);
              CHECK(pcMask(out, no) == inMask,
                    "[CONTENT] modo%d preset%d t%d d%d: clases de salida != entrada", a, pi, t, d);
              // El bajo es una clase del acorde (rootPc no inventa contenido).
              CHECK((inMask >> (out[0] % 12)) & 1,
                    "[CONTENT] modo%d preset%d t%d d%d: bajo con clase ajena", a, pi, t, d);
            }

    // sus2/sus4/add6 (por zona) + JM_HICHORD (cromático). El contenido de clases
    // debe preservarse EXACTO (no se agregan/quitan clases; el original no se
    // modifica al comparar: se copia a `out`).
    ColorZone susZ[3] = { CZ_DOWN_LEFT, CZ_LEFT, CZ_DOWN };   // sus2, sus4, add6
    ColorZone joyZ[5] = { CZ_UP_RIGHT, CZ_RIGHT, CZ_DOWN_LEFT, CZ_UP_LEFT, CZ_DOWN_RIGHT };
    for (int a = 0; a < 4; a++)
      for (int mi = 0; mi < 2; mi++)
        for (int t = 0; t < 12; t++)
          for (int d = 0; d < 7; d++) {
            Mode mode = mi ? MODE_AEOLIAN : MODE_IONIAN;
            // --- sus/add6 diatónicos ---
            for (int z = 0; z < 3; z++) {
              harmonyVoiceLeadReset();
              uint8_t in[MAX_CHORD_NOTES];
              uint8_t ni = harmonyChordPreset((uint8_t)d, susZ[z], (uint8_t)t, mode, HP_BASIC, in);
              int inMask = pcMask(in, ni); uint8_t rp = in[0] % 12;
              uint8_t out[MAX_CHORD_NOTES]; for (int i = 0; i < ni; i++) out[i] = in[i];
              uint8_t no = harmonyVoiceLeadMode(out, ni, rp, all[a]);
              CHECK(pcMask(out, no) == inMask, "[CONTENT-sus] modo%d z%d t%d d%d clases !=", a, z, t, d);
            }
            // --- cromáticos JM_HICHORD ---
            for (int z = 0; z < 5; z++) {
              harmonyVoiceLeadReset();
              uint8_t in[MAX_CHORD_NOTES];
              uint8_t ni = harmonyChordJoy((uint8_t)d, joyZ[z], (uint8_t)t, mode, HP_BASIC, CQ_DIATONIC, JM_HICHORD, in);
              int inMask = pcMask(in, ni); uint8_t rp = in[0] % 12;
              uint8_t out[MAX_CHORD_NOTES]; for (int i = 0; i < ni; i++) out[i] = in[i];
              uint8_t no = harmonyVoiceLeadMode(out, ni, rp, all[a]);
              CHECK(pcMask(out, no) == inMask, "[CONTENT-joy] modo%d z%d t%d d%d clases !=", a, z, t, d);
              CHECK((inMask >> (out[0] % 12)) & 1, "[CONTENT-joy] modo%d bajo clase ajena", a);
            }
          }
  }

  // -------------------------------------------------------------------------
  // 19) SEPARACIÓN CORAL (E1..E4). Corpus de progresiones triádicas.
  // -------------------------------------------------------------------------
  {
    // E1: disposición de FÁBRICA. El primer C mayor sin historial es el ÚNICO
    // caso CHORALE/CONTRARY congelado como golden (precaución: no congelar todo).
    // Debe tener raíz al bajo (existe solución compacta equivalente), sin hueco
    // superior >12 y span acotado. CHORALE y CONTRARY coinciden sin historial.
    const int FACTORY_C[3] = { 48, 55, 64 };   // C3 G3 E4
    VoiceLeadMode fm[2] = { VL_CHORALE, VL_CONTRARY };
    for (int f = 0; f < 2; f++) {
      harmonyVoiceLeadReset();
      uint8_t c[MAX_CHORD_NOTES]; int rc;
      uint8_t nc = buildChord(0, 0, MODE_IONIAN, HP_BASIC, c, &rc);
      nc = harmonyVoiceLeadMode(c, nc, (uint8_t)rc, fm[f]);
      CHECK(nc == 3, "[E1] n!=3");
      for (int i = 0; i < 3; i++)
        CHECK(c[i] == FACTORY_C[i], "[E1] fábrica modo%d voz%d = %d (esperado %d)", f, i, c[i], FACTORY_C[i]);
      CHECK(c[0] % 12 == (uint8_t)rc, "[E1] primer acorde: raíz no está en el bajo (%d)", c[0]);
      for (int k = 2; k < nc; k++)
        CHECK(c[k] - c[k - 1] <= 12, "[E1] separación superior >12 en el primer acorde (%d-%d)", c[k-1], c[k]);
      CHECK(c[nc - 1] - c[0] <= 24, "[E1] span del primer acorde >24 (%d)", c[nc-1]-c[0]);
    }

    // E2/E3/E4 sobre un corpus (todas las tonalidades, mayor y menor).
    static const int P1[]={0,4,5,3}, P2[]={1,4,0}, P3[]={0,5,3,4},
                     P4[]={0,3,4,0}, P5[]={5,3,0,4}, P6[]={0,3,6,2,5,1,4,0};
    struct CP { const int* d; int len; };
    CP corpus[6] = {{P1,4},{P2,3},{P3,4},{P4,4},{P5,4},{P6,8}};
    VoiceLeadMode cho[2] = { VL_CHORALE, VL_CONTRARY };
    for (int cm = 0; cm < 2; cm++)
      for (int mi = 0; mi < 2; mi++)
        for (int t = 0; t < 12; t++)
          for (int pi = 0; pi < 6; pi++) {
            StepOut o[8];
            runProg(corpus[pi].d, corpus[pi].len, (uint8_t)t, mi ? MODE_AEOLIAN : MODE_IONIAN, HP_BASIC, cho[cm], o);
            for (int s = 0; s < corpus[pi].len; s++) {
              CHECK(ascending(o[s]), "[E4] cruce modo%d t%d prog%d paso%d", cm, t, pi, s);
              int span = o[s].notes[o[s].n - 1] - o[s].notes[0];
              CHECK(span <= 24, "[E2] tríada span>24 (%d) modo%d t%d prog%d paso%d", span, cm, t, pi, s);
              for (int k = 1; k < o[s].n; k++)
                CHECK(o[s].notes[k] - o[s].notes[k - 1] <= 19,
                      "[E3] hueco adyacente >19 (%d) modo%d t%d prog%d paso%d", o[s].notes[k]-o[s].notes[k-1], cm, t, pi, s);
              // E4: bajo = mínima, soprano = máxima (identificables).
              CHECK(o[s].notes[0] == o[s].notes[0] && o[s].notes[o[s].n-1] >= o[s].notes[0],
                    "[E4] bajo/soprano no identificables");
            }
          }
  }

  // -------------------------------------------------------------------------
  // 20) PARALLEL congelado (E8): valores literales independientes del historial.
  // -------------------------------------------------------------------------
  {
    struct FR { const int* d; int n; uint8_t tonic; Mode mode; int exp[8][3]; };
    FR fr[3] = {
      { I_V_vi_IV, 4, 0, MODE_IONIAN,    { {48,52,55},{55,59,62},{57,60,64},{53,57,60} } },
      { ii_V_I,    3, 0, MODE_IONIAN,    { {50,53,57},{55,59,62},{48,52,55} } },
      { i_VI_III_VII, 4, 9, MODE_AEOLIAN,{ {57,60,64},{53,57,60},{48,52,55},{55,59,62} } },
    };
    for (int p = 0; p < 3; p++) {
      StepOut o[8];
      runProg(fr[p].d, fr[p].n, fr[p].tonic, fr[p].mode, HP_BASIC, VL_PARALLEL, o);
      for (int s = 0; s < fr[p].n; s++) {
        CHECK(o[s].n == 3, "[PAR-REG] n!=3 prog%d paso%d", p, s);
        for (int i = 0; i < 3; i++)
          CHECK(o[s].notes[i] == fr[p].exp[s][i],
                "[PAR-REG] prog%d paso%d voz%d = %d (esperado %d)", p, s, i, o[s].notes[i], fr[p].exp[s][i]);
      }
    }
  }

  // -------------------------------------------------------------------------
  // 21) MÉTRICAS DE CORPUS (D): CHORALE vs CONTRARY sobre 6 progresiones × 12
  //     tonalidades × mayor/menor. Se MIDEN TRANSICIONES (no el primer acorde).
  //     El movimiento contrario exterior y el movimiento por voz SÓLO se computan
  //     en transiciones de n CONSTANTE (identidad de voces confiable). La
  //     diferencia CHORALE vs CONTRARY se decide por nota MIDI (copias ordenadas,
  //     ambos modos entregan voces ascendentes) no por orden interno del array.
  //     Corpus triádico -> todas las transiciones son sameN; el bloque 21b agrega
  //     un caso con cambio de n para ejercitar la exclusión.
  // -------------------------------------------------------------------------
  auto sortedEq = [](const StepOut& x, const StepOut& y) {
    if (x.n != y.n) return false;
    uint8_t A[MAX_CHORD_NOTES], B[MAX_CHORD_NOTES];
    for (int i = 0; i < x.n; i++) { A[i] = x.notes[i]; B[i] = y.notes[i]; }
    for (int i = 1; i < x.n; i++) { uint8_t k=A[i]; int j=i-1; while(j>=0&&A[j]>k){A[j+1]=A[j];j--;} A[j+1]=k; }
    for (int i = 1; i < y.n; i++) { uint8_t k=B[i]; int j=i-1; while(j>=0&&B[j]>k){B[j+1]=B[j];j--;} B[j+1]=k; }
    for (int i = 0; i < x.n; i++) if (A[i] != B[i]) return false;
    return true;
  };
  {
    static const int P1[]={0,4,5,3}, P2[]={1,4,0}, P3[]={0,5,3,4},
                     P4[]={0,3,4,0}, P5[]={5,3,0,4}, P6[]={0,3,6,2,5,1,4,0};
    struct CP { const char* name; const int* d; int len; };
    CP corpus[6] = {{"I-V-vi-IV",P1,4},{"ii-V-I",P2,3},{"I-vi-IV-V",P3,4},
                    {"I-IV-V-I",P4,4},{"vi-IV-I-V",P5,4},{"I-IV-vii-iii-vi-ii-V-I",P6,8}};
    struct Acc { long transSame, transDiffN, outer; long sumMotion, moves; int maxJump; int maxGap; long sumSpan; int maxSpan; long chords; };
    Acc a[2]; for (int i = 0; i < 2; i++) a[i] = {0,0,0,0,0,0,0,0,0,0};
    VoiceLeadMode cho[2] = { VL_CHORALE, VL_CONTRARY };
    long diffTrans = 0, compTrans = 0;   // transiciones comparables / diferentes

    for (int mi = 0; mi < 2; mi++)
      for (int t = 0; t < 12; t++)
        for (int pi = 0; pi < 6; pi++) {
          StepOut o[2][8];
          for (int cm = 0; cm < 2; cm++)
            runProg(corpus[pi].d, corpus[pi].len, (uint8_t)t, mi ? MODE_AEOLIAN : MODE_IONIAN, HP_BASIC, cho[cm], o[cm]);
          for (int cm = 0; cm < 2; cm++)
            for (int s = 0; s < corpus[pi].len; s++) {
              StepOut& cur = o[cm][s];
              a[cm].chords++;
              int span = cur.notes[cur.n - 1] - cur.notes[0];
              a[cm].sumSpan += span; if (span > a[cm].maxSpan) a[cm].maxSpan = span;
              for (int k = 1; k < cur.n; k++) { int g = cur.notes[k]-cur.notes[k-1]; if (g > a[cm].maxGap) a[cm].maxGap = g; }
              if (s >= 1) {                                   // TRANSICIÓN (no el 1er acorde)
                StepOut& prev = o[cm][s - 1];
                if (cur.n == prev.n) {
                  a[cm].transSame++;
                  int db = cur.notes[0] - prev.notes[0];
                  int ds = cur.notes[cur.n-1] - prev.notes[prev.n-1];
                  if (db != 0 && ds != 0 && ((db > 0) != (ds > 0))) a[cm].outer++;
                  for (int k = 0; k < cur.n; k++) {
                    int mv = cur.notes[k] - prev.notes[k]; if (mv < 0) mv = -mv;
                    a[cm].sumMotion += mv; a[cm].moves++;
                    if (mv > a[cm].maxJump) a[cm].maxJump = mv;
                  }
                } else a[cm].transDiffN++;                    // n cambió: sin contrario/motion
              }
            }
          // Diferencia CHORALE vs CONTRARY POR TRANSICIÓN (>=1 nota MIDI distinta,
          // copias ordenadas -> inmune al orden interno del array).
          for (int s = 1; s < corpus[pi].len; s++) {
            compTrans++;
            if (!sortedEq(o[0][s], o[1][s])) diffTrans++;
          }
        }

    const char* nm[2] = { "CHORALE", "CONTRARY" };
    VPRINT("\n=== Métricas de corpus (6 progresiones × 12 tonos × may/men) ===\n");
    for (int cm = 0; cm < 2; cm++) {
      VPRINT("  %-8s transSameN=%ld transDiffN=%ld | outerContra=%ld/%ld=%.1f%%  movMedio/voz=%.2f st"
             "  maxJump=%d  maxGap=%d  spanMedio=%.1f  spanMax=%d\n",
             nm[cm], a[cm].transSame, a[cm].transDiffN,
             a[cm].outer, a[cm].transSame, 100.0 * a[cm].outer / (a[cm].transSame ? a[cm].transSame : 1),
             (double)a[cm].sumMotion / (a[cm].moves ? a[cm].moves : 1),
             a[cm].maxJump, a[cm].maxGap,
             (double)a[cm].sumSpan / (a[cm].chords ? a[cm].chords : 1), a[cm].maxSpan);
    }
    double pctTrans = 100.0 * diffTrans / (compTrans ? compTrans : 1);
    VPRINT("  CONTRARY difiere de CHORALE: %ld/%ld transiciones = %.1f%%\n", diffTrans, compTrans, pctTrans);
    double ocC = 100.0 * a[0].outer / (a[0].transSame ? a[0].transSame : 1);
    double ocK = 100.0 * a[1].outer / (a[1].transSame ? a[1].transSame : 1);

    CHECK(ocK > ocC, "[CORPUS] CONTRARY no supera a CHORALE en contrario exterior (%.1f vs %.1f)", ocK, ocC);
    CHECK(pctTrans >= 20.0, "[CORPUS] CONTRARY difiere de CHORALE en <20%% de transiciones (%.1f%%)", pctTrans);
    CHECK(a[1].maxJump <= 12, "[CORPUS] CONTRARY maxJump>12 (%d) — revisar fallback", a[1].maxJump);
    CHECK(a[0].maxGap <= 19 && a[1].maxGap <= 19, "[CORPUS] hueco adyacente >19");
    CHECK(a[0].transDiffN == 0 && a[1].transDiffN == 0, "[CORPUS] corpus triádico no debería tener cambios de n");
  }

  // -------------------------------------------------------------------------
  // 21b) Cambio de n en métricas: I(3)->I7(4)->I9(5)->vi(3) en 12 tonos. Verifica
  //      que las transiciones con n distinto NO cuenten como contrario exterior y
  //      se contabilicen aparte (numerador/denominador informados).
  // -------------------------------------------------------------------------
  {
    uint8_t presets[4] = { HP_BASIC, HP_SEVENTH, HP_NINTH, HP_BASIC };
    int degs[4] = { 0, 0, 0, 5 };
    long transSame = 0, transDiffN = 0, outer = 0;
    for (int cm = 0; cm < 2; cm++) {
      VoiceLeadMode m = cm ? VL_CONTRARY : VL_CHORALE;
      for (int t = 0; t < 12; t++) {
        harmonyVoiceLeadReset();
        StepOut prev{}; bool have = false;
        for (int s = 0; s < 4; s++) {
          uint8_t notes[MAX_CHORD_NOTES]; int rp;
          uint8_t nn = buildChord(degs[s], (uint8_t)t, MODE_IONIAN, presets[s], notes, &rp);
          nn = harmonyVoiceLeadMode(notes, nn, (uint8_t)rp, m);
          StepOut cur; cur.n = nn; for (int i = 0; i < nn; i++) cur.notes[i] = notes[i];
          if (have) {
            if (cur.n == prev.n) {
              transSame++;
              int db = cur.notes[0]-prev.notes[0], ds = cur.notes[cur.n-1]-prev.notes[prev.n-1];
              if (db != 0 && ds != 0 && ((db>0)!=(ds>0))) outer++;
            } else transDiffN++;
          }
          prev = cur; have = true;
        }
      }
    }
    VPRINT("  [21b] cambio de n: transSameN=%ld transDiffN=%ld outerContra(sameN)=%ld\n",
           transSame, transDiffN, outer);
    CHECK(transDiffN > 0, "[21b] no se ejercitó ninguna transición con cambio de n");
    CHECK(outer <= transSame, "[21b] contrario exterior contado fuera de sameN");
  }

  // -------------------------------------------------------------------------
  // 22) CASOS LÍMITE: n=1, n=2, disminuido, 4/5 clases, y determinismo del
  //     corpus en ORDEN INVERSO (sin dependencia accidental del estado del test).
  // -------------------------------------------------------------------------
  {
    VoiceLeadMode all[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    // n=1 (una sola voz) y n=2 (díada C-G) manuales.
    for (int a = 0; a < 4; a++) {
      harmonyVoiceLeadReset();
      uint8_t one[1] = { 60 };
      uint8_t n1 = harmonyVoiceLeadMode(one, 1, 0, all[a]);
      CHECK(n1 == 1 && one[0] >= 36 && one[0] <= 91 && one[0] % 12 == 0, "[EDGE] n=1 modo%d = %d", a, one[0]);

      harmonyVoiceLeadReset();
      uint8_t two[2] = { 60, 67 };                 // C, G
      uint8_t n2 = harmonyVoiceLeadMode(two, 2, 0, all[a]);
      CHECK(n2 == 2, "[EDGE] n=2 modo%d n=%d", a, n2);
      CHECK(pcMask(two, n2) == ((1<<0)|(1<<7)), "[EDGE] n=2 modo%d clases !=", a);
      for (int i = 0; i < n2; i++) CHECK(two[i] >= 36 && two[i] <= 91, "[EDGE] n=2 fuera de rango %d", two[i]);
      if (all[a] == VL_CHORALE || all[a] == VL_CONTRARY) CHECK(two[1] > two[0], "[EDGE] n=2 no ascendente modo%d", a);
    }
    // Acorde DISMINUIDO (vii° de Do mayor = B D F) en los 4 modos.
    for (int a = 0; a < 4; a++) {
      harmonyVoiceLeadReset();
      uint8_t dim[MAX_CHORD_NOTES]; uint8_t nd = harmonyChord(6, CZ_CENTER, 0, MODE_IONIAN, dim);  // B D F
      int inMask = pcMask(dim, nd); uint8_t rp = dim[0] % 12;
      uint8_t out[MAX_CHORD_NOTES]; for (int i = 0; i < nd; i++) out[i] = dim[i];
      uint8_t no = harmonyVoiceLeadMode(out, nd, rp, all[a]);
      CHECK(pcMask(out, no) == inMask, "[EDGE] dim modo%d clases !=", a);
      for (int i = 0; i < no; i++) CHECK(out[i] >= 36 && out[i] <= 91, "[EDGE] dim fuera de rango %d", out[i]);
      if (all[a] == VL_CHORALE || all[a] == VL_CONTRARY) {
        StepOut so; so.n = no; for (int i = 0; i < no; i++) so.notes[i] = out[i];
        CHECK(ascending(so), "[EDGE] dim cruce modo%d", a);
      }
    }
    // 4 y 5 clases (tétrada/pentada) en CHORALE/CONTRARY, tonos extremos (B, Db).
    int extt[2] = { 11, 1 };   // B (11) y C#/Db (1): extremos del círculo
    uint8_t pr45[2] = { HP_SEVENTH, HP_NINTH };
    for (int cm = 2; cm < 4; cm++)
      for (int e = 0; e < 2; e++)
        for (int pi = 0; pi < 2; pi++)
          for (int d = 0; d < 7; d++) {
            harmonyVoiceLeadReset();
            uint8_t in[MAX_CHORD_NOTES]; int rp;
            uint8_t ni = buildChord(d, (uint8_t)extt[e], MODE_IONIAN, pr45[pi], in, &rp);
            uint8_t no = harmonyVoiceLeadMode(in, ni, (uint8_t)rp, all[cm]);
            StepOut so; so.n = no; for (int i = 0; i < no; i++) so.notes[i] = in[i];
            CHECK(ascending(so), "[EDGE] %d clases cruce modo%d t%d d%d", ni, cm, extt[e], d);
            for (int i = 0; i < no; i++) CHECK(in[i] >= 36 && in[i] <= 91, "[EDGE] rango t%d %d", extt[e], in[i]);
          }

    // ORDEN INVERSO: el corpus produce las MISMAS salidas recorrido al revés
    // (runProg resetea el historial -> sin dependencia del estado del test).
    static const int Q1[]={0,4,5,3}, Q2[]={1,4,0}, Q3[]={0,3,4,0};
    struct CP { const int* d; int len; }; CP cq[3] = {{Q1,4},{Q2,3},{Q3,4}};
    for (int a = 2; a < 4; a++)
      for (int mi = 0; mi < 2; mi++)
        for (int pi = 0; pi < 3; pi++) {
          for (int t = 0; t < 12; t++) {                 // forward
            StepOut fwd[8]; runProg(cq[pi].d, cq[pi].len, (uint8_t)t, mi?MODE_AEOLIAN:MODE_IONIAN, HP_BASIC, all[a], fwd);
            // recomputar el mismo (t) tras haber corrido otros: debe coincidir
            StepOut again[8]; runProg(cq[pi].d, cq[pi].len, (uint8_t)(11 - t), mi?MODE_AEOLIAN:MODE_IONIAN, HP_BASIC, all[a], again);
            StepOut back[8];  runProg(cq[pi].d, cq[pi].len, (uint8_t)t, mi?MODE_AEOLIAN:MODE_IONIAN, HP_BASIC, all[a], back);
            for (int s = 0; s < cq[pi].len; s++) {
              CHECK(fwd[s].n == back[s].n, "[EDGE-REV] n cambió por orden (t%d)", t);
              for (int i = 0; i < fwd[s].n; i++)
                CHECK(fwd[s].notes[i] == back[s].notes[i], "[EDGE-REV] salida depende del orden del test (t%d s%d)", t, s);
            }
          }
        }
  }

  // -------------------------------------------------------------------------
  // 23) RENDIMIENTO: la búsqueda coral es ESTRICTAMENTE ACOTADA. Por llamada se
  //     evalúan a lo sumo 7! = 5040 candidatos (n<=7 -> n!; n=8 fija el bajo y
  //     permuta 7). Contador host `gVlCandidatesEvaluated`.
  // -------------------------------------------------------------------------
  {
    // n=7 (7 clases distintas): peor caso sin fijar bajo -> exactamente 7!.
    uint8_t c7[7] = { 60, 62, 64, 65, 67, 69, 71 };   // C D E F G A B
    harmonyVoiceLeadReset();
    gVlCandidatesEvaluated = 0;
    harmonyVoiceLeadMode(c7, 7, 0, VL_CHORALE);
    long cand7 = gVlCandidatesEvaluated;
    CHECK(cand7 == 5040, "[PERF] n=7 evaluó %ld candidatos (esperado 7!=5040)", cand7);

    // n=8 (8 clases distintas): fija la fundamental -> 7! = 5040.
    uint8_t c8[8] = { 60, 61, 62, 63, 64, 65, 66, 67 };
    harmonyVoiceLeadReset();
    gVlCandidatesEvaluated = 0;
    harmonyVoiceLeadMode(c8, 8, 0, VL_CONTRARY);
    long cand8 = gVlCandidatesEvaluated;
    CHECK(cand8 <= 5040, "[PERF] n=8 evaluó %ld candidatos (>5040)", cand8);

    // Cota por-llamada sobre TODO el corpus: ninguna supera 5040.
    static const int P1[]={0,4,5,3}, P6[]={0,3,6,2,5,1,4,0};
    struct CP { const int* d; int len; }; CP cc[2] = {{P1,4},{P6,8}};
    long worst = 0;
    for (int a = 2; a < 4; a++)
      for (int mi = 0; mi < 2; mi++)
        for (int t = 0; t < 12; t++)
          for (int pi = 0; pi < 2; pi++) {
            harmonyVoiceLeadReset();
            VoiceLeadMode m = (a == 2) ? VL_CHORALE : VL_CONTRARY;
            for (int s = 0; s < cc[pi].len; s++) {
              uint8_t notes[MAX_CHORD_NOTES]; int rp;
              uint8_t nn = buildChord(cc[pi].d[s], (uint8_t)t, mi?MODE_AEOLIAN:MODE_IONIAN, HP_BASIC, notes, &rp);
              gVlCandidatesEvaluated = 0;
              harmonyVoiceLeadMode(notes, nn, (uint8_t)rp, m);
              if (gVlCandidatesEvaluated > worst) worst = gVlCandidatesEvaluated;
              CHECK(gVlCandidatesEvaluated <= 5040, "[PERF] llamada evaluó %ld (>5040)", gVlCandidatesEvaluated);
            }
          }
    VPRINT("\n=== Rendimiento (cota de búsqueda) ===\n");
    VPRINT("  candidatos: n=7 -> %ld (=7!), n=8 -> %ld (bajo fijo, <=7!), peor del corpus triádico -> %ld\n",
           cand7, cand8, worst);
  }

  // -------------------------------------------------------------------------
  // 24) INTEGRACIÓN: pipeline único, normalización y migración de sesión.
  // -------------------------------------------------------------------------
  {
    // Orden del enum (lo asumen menú/webcfg/persistencia).
    CHECK(VL_PARALLEL == 0 && VL_SMOOTH == 1 && VL_CHORALE == 2 && VL_CONTRARY == 3 && VL_COUNT == 4,
          "[INT] orden del enum VoiceLeadMode cambió");

    // harmonyVoiceLeadNorm: los 4 válidos pasan; inválidos -> VL_SMOOTH (clamp).
    for (int v = 0; v < VL_COUNT; v++) CHECK(harmonyVoiceLeadNorm(v) == v, "[INT] norm(%d)!=%d", v, v);
    CHECK(harmonyVoiceLeadNorm(VL_COUNT) == VL_SMOOTH, "[INT] norm(4)!=SMOOTH");
    CHECK(harmonyVoiceLeadNorm(99)  == VL_SMOOTH, "[INT] norm(99)!=SMOOTH");
    CHECK(harmonyVoiceLeadNorm(255) == VL_SMOOTH, "[INT] norm(255)!=SMOOTH");
    CHECK(harmonyVoiceLeadNorm(-1)  == VL_SMOOTH, "[INT] norm(-1)!=SMOOTH");

    // EQUIVALENCIA del pipeline único (harmonyBuildVoiced) con la secuencia
    // manual chordJoy -> voiceLeadMode(rootPc antes de reordenar) -> voicing ->
    // octava. Esto es lo que garantiza que el firmware físico y #degree den las
    // MISMAS notas para el mismo estado (antes #degree divergía). Espacio acotado.
    int tset[3] = { 0, 5, 11 };
    ColorZone zset[3] = { CZ_CENTER, CZ_UP, CZ_RIGHT };
    uint8_t pset[2] = { HP_BASIC, HP_NINTH };
    Voicing vset[4] = { VC_CLOSE, VC_INV1, VC_INV2, VC_OPEN };
    VoiceLeadMode lset[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    for (int mi = 0; mi < 2; mi++)
     for (int ti = 0; ti < 3; ti++)
      for (int d = 0; d < 7; d++)
       for (int zi = 0; zi < 3; zi++)
        for (int jm = 0; jm < JM_COUNT; jm++)
         for (int q = 0; q < 2; q++)
          for (int pi = 0; pi < 2; pi++)
           for (int vi = 0; vi < 4; vi++)
            for (int li = 0; li < 4; li++) {
              Mode mode = mi ? MODE_AEOLIAN : MODE_IONIAN;
              uint8_t qual = q ? CQ_DOM7 : CQ_DIATONIC;
              // Helper único.
              harmonyVoiceLeadReset();
              uint8_t A[MAX_CHORD_NOTES];
              uint8_t nA = harmonyBuildVoiced((uint8_t)d, zset[zi], (uint8_t)tset[ti], mode,
                                              pset[pi], qual, (uint8_t)jm, lset[li], vset[vi], 0, A);
              // Secuencia manual equivalente.
              harmonyVoiceLeadReset();
              uint8_t B[MAX_CHORD_NOTES];
              uint8_t nB = harmonyChordJoy((uint8_t)d, zset[zi], (uint8_t)tset[ti], mode,
                                           pset[pi], qual, (uint8_t)jm, B);
              nB = harmonyVoiceLeadMode(B, nB, (uint8_t)(nB ? B[0] % 12 : 0), lset[li]);
              nB = harmonyApplyVoicing(B, nB, vset[vi]);
              nB = harmonyApplyOctave(B, nB, 0);
              CHECK(nA == nB, "[INT] build!=manual n (d%d z%d jm%d q%d p%d v%d l%d)", d, zi, jm, q, pi, vi, li);
              for (int k = 0; k < nA; k++)
                CHECK(A[k] == B[k], "[INT] build!=manual voz%d (d%d jm%d l%d)", k, d, jm, li);
            }

    // MIGRACIÓN de sesión (espejo EXACTO de la regla de webcfg::nvsLoad, que usa
    // la función de producción harmonyVoiceLeadNorm): v!=4 (campo ausente / struct
    // anterior) -> VL_SMOOTH; v==4 -> clamp del byte. NO se usa módulo.
    auto sessionLead = [](int ver, int raw) {
      return harmonyVoiceLeadNorm(ver == 4 ? raw : (int)VL_SMOOTH);
    };
    // Estructura ANTERIOR (v3, sin el campo): cae a VL_SMOOTH aunque el byte de
    // padding valga cualquier cosa.
    CHECK(sessionLead(3, 0)   == VL_SMOOTH, "[INT] v3 no migró a SMOOTH");
    CHECK(sessionLead(3, 3)   == VL_SMOOTH, "[INT] v3 reinterpretó byte viejo");
    CHECK(sessionLead(3, 200) == VL_SMOOTH, "[INT] v3 byte basura no migró");
    // v4: round-trip de los 4 valores válidos.
    for (int v = 0; v < VL_COUNT; v++) CHECK(sessionLead(4, v) == v, "[INT] v4 round-trip %d", v);
    // v4 con valor corrupto -> VL_SMOOTH (no módulo: 99%4=3=CONTRA sería INCORRECTO).
    CHECK(sessionLead(4, 99)  == VL_SMOOTH, "[INT] v4 corrupto(99) no migró a SMOOTH");
    CHECK(sessionLead(4, 255) == VL_SMOOTH, "[INT] v4 corrupto(255) no migró a SMOOTH");
  }

  // -------------------------------------------------------------------------
  // Tabla comparativa (para el reporte): notas MIDI por algoritmo.
  // -------------------------------------------------------------------------
  {
    // Cada paso lleva su grado y su preset (para progresiones de séptimas y de
    // cantidad de voces variable).
    struct Step { int deg; uint8_t preset; };
    struct Prog { const char* name; Step st[8]; int n; uint8_t tonic; Mode mode; };
    Prog progs[5] = {
      { "C  I-V-vi-IV", {{0,HP_BASIC},{4,HP_BASIC},{5,HP_BASIC},{3,HP_BASIC}}, 4, 0, MODE_IONIAN },
      { "C  ii-V-I",    {{1,HP_BASIC},{4,HP_BASIC},{0,HP_BASIC}}, 3, 0, MODE_IONIAN },
      { "Am i-VI-III-VII", {{0,HP_BASIC},{5,HP_BASIC},{2,HP_BASIC},{6,HP_BASIC}}, 4, 9, MODE_AEOLIAN },
      { "C  ii7-vi7-IVmaj7-Imaj7 (séptimas)",
        {{1,HP_SEVENTH},{5,HP_SEVENTH},{3,HP_SEVENTH},{0,HP_SEVENTH}}, 4, 0, MODE_IONIAN },
      { "C  I(3)->I7(4)->I9(5)->vi(3) (cambio de n)",
        {{0,HP_BASIC},{0,HP_SEVENTH},{0,HP_NINTH},{5,HP_BASIC}}, 4, 0, MODE_IONIAN },
    };
    const char* algn[4] = { "PARALLEL", "SMOOTH", "CHORALE", "CONTRARY" };
    VoiceLeadMode algs[4] = { VL_PARALLEL, VL_SMOOTH, VL_CHORALE, VL_CONTRARY };
    VPRINT("\n=== Tabla comparativa (MIDI + nombres de nota) ===\n");
    for (int p = 0; p < 5; p++) {
      VPRINT("\n%s\n", progs[p].name);
      for (int a = 0; a < 4; a++) {
        StepOut o[8];
        harmonyVoiceLeadReset();
        for (int s = 0; s < progs[p].n; s++) {
          uint8_t notes[MAX_CHORD_NOTES]; int rp;
          uint8_t nn = buildChord(progs[p].st[s].deg, progs[p].tonic, progs[p].mode, progs[p].st[s].preset, notes, &rp);
          nn = harmonyVoiceLeadMode(notes, nn, (uint8_t)rp, algs[a]);
          o[s].n = nn; for (int i = 0; i < nn; i++) o[s].notes[i] = notes[i];
        }
        VPRINT("  %-9s", algn[a]);
        for (int s = 0; s < progs[p].n; s++) {
          VPRINT(" [");
          for (int i = 0; i < o[s].n; i++) VPRINT("%s%d", i ? " " : "", o[s].notes[i]);
          VPRINT("]");
        }
        VPRINT("\n           ");
        for (int s = 0; s < progs[p].n; s++) {   // misma fila en nombres de nota
          VPRINT(" [");
          for (int i = 0; i < o[s].n; i++) { char nb[8]; midiName(o[s].notes[i], nb); VPRINT("%s%s", i ? " " : "", nb); }
          VPRINT("]");
        }
        VPRINT("\n");
      }
    }
  }

  printf("\n%d checks, %d fallas\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
