/* ============================================================================
   test_vocal.cpp  -  Tests de host de la voz por modelado físico (T_VOCAL).
   Dos niveles:
     A) vocal.h/.cpp AISLADO (sin dsp): física del tubo, formantes, glotis.
     B) integrado en el motor: nota sostenida, ataque/liberación, cambio de
        vocal, rango de alturas, acordes, índices de timbre, valores finitos.
   Deterministas, sin oído. Con un directorio como argumento exporta WAVs
   estéreo (camino del firmware, con efectos) para escucha comparada:
     out_vocal_A/E/I/O/U.wav  misma frase en cada vocal
     out_vocal_morph.wav      acorde sostenido barriendo A->E->I->O->U
     out_vocal_escala.wav     escala C2..G6 (rango útil)

   Compilar/correr:
     g++ -std=c++17 -I. test/test_vocal.cpp dsp.cpp vocal.cpp vocal_text.cpp -o test_vocal
     ./test_vocal [dir_salida_wav]
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "dsp.h"
#include "vocal.h"

static int checks = 0, failures = 0;
#define CHECK(cond, msg, ...) do { checks++; if(!(cond)) { failures++; \
  printf("FAIL: " msg "\n", ##__VA_ARGS__); } } while(0)

static const double PI_D = 3.14159265358979323846;
static const float  SR = (float)SAMPLE_RATE;
static float midiToFreq(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static double goertzel(const std::vector<float>& x, double freq) {
  double w = 2.0 * PI_D * freq / SR, coeff = 2.0 * cos(w), s1 = 0, s2 = 0;
  for (float v : x) { double s0 = v + coeff * s1 - s2; s2 = s1; s1 = s0; }
  return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((double)x.size() * x.size());
}
static double rms(const std::vector<float>& x, size_t a, size_t b) {
  double s = 0; for (size_t i = a; i < b; i++) s += (double)x[i] * x[i];
  return sqrt(s / (double)(b - a));
}
static bool finite(const std::vector<float>& x) {
  for (float v : x) if (!std::isfinite(v)) return false;
  return true;
}
static float peak(const std::vector<float>& x) {
  float p = 0; for (float v : x) { float a = fabsf(v); if (a > p) p = a; } return p;
}
// Render mono seco (sin efectos) de `n` muestras, agregando al vector.
static void render(std::vector<float>& out, int n) {
  float b[AUDIO_BLOCK];
  while (n > 0) {
    int m = n < (int)AUDIO_BLOCK ? n : (int)AUDIO_BLOCK;
    dspRenderBlock(b, m);
    out.insert(out.end(), b, b + m); n -= m;
  }
}
static void setVowel(float morph, uint8_t t = T_VOCAL) {
  EnvCfg e = dspEnvGet(t); e.wtMorph = morph; dspEnvSet(t, e);
}

// Tres primeros picos (Hz) de la respuesta al impulso de un tracto ya configurado.
static int formants(VocalTract& t, float* F) {
  const int N = 2048; std::vector<float> ir(N);
  for (int i = 0; i < N; i++) ir[i] = t.tick(i == 0 ? 1.0f : 0.0f);
  const int NF = 760; const double F0 = 150, DF = 5; std::vector<double> mag(NF);
  for (int j = 0; j < NF; j++) mag[j] = goertzel(ir, F0 + DF * j);
  int found = 0;
  for (int j = 1; j < NF - 1 && found < 3; j++) {
    bool top = mag[j] > mag[j - 1] && mag[j] >= mag[j + 1];
    for (int q = j - 12; q <= j + 12 && top; q++)
      if (q >= 0 && q < NF && mag[q] > mag[j]) top = false;
    if (top) F[found++] = (float)(F0 + DF * j);
  }
  return found;
}

// ---------------------------------------------------------------- A) módulo
static void testTubeUniform() {
  printf("[vocal] tubo uniforme (física de la guía de ondas):\n");
  // Tubo cerrado en glotis y abierto en labios: resonancias en c/4L * (1,3,5).
  VocalTract t; t.init(SR, 17.5f);
  float flat[VOCAL_CTRL_POINTS]; for (float& a : flat) a = 1.0f;
  t.setAreas(flat, 1.0f, true);
  float F[3] = {0, 0, 0}; int nf = formants(t, F);
  CHECK(t.n == 22, "17.5 cm a 44.1 kHz deben ser 22 secciones (n=%d)", t.n);
  CHECK(nf == 3, "el tubo uniforme no muestra 3 resonancias (%d)", nf);
  const float EXP[3] = { 500, 1500, 2500 };
  for (int i = 0; i < 3; i++)
    CHECK(fabsf(F[i] - EXP[i]) < 0.05f * EXP[i], "resonancia %d = %.0f Hz (esperado ~%.0f)", i + 1, F[i], EXP[i]);
  printf("  tubo       resonancias %.0f / %.0f / %.0f Hz (teoría 500/1500/2500)\n", F[0], F[1], F[2]);
}

static void testVowelFormants() {
  printf("[vocal] formantes por vocal (medidos sobre el modelo):\n");
  static const float TARGET[VOCAL_VOWELS][2] = { {700,1200}, {450,1950}, {280,2300}, {480,900}, {310,720} };
  static const char* NAME = "AEIOU";
  float f1[VOCAL_VOWELS], f2[VOCAL_VOWELS];
  for (int v = 0; v < VOCAL_VOWELS; v++) {
    for (int side = 0; side < 2; side++) {             // L = 17.5 cm, R = 16.7 cm
      VocalTract t; t.init(SR, side ? 16.7f : 17.5f);
      t.setVowel((float)v / (VOCAL_VOWELS - 1), true);
      float F[3] = {0, 0, 0}; int nf = formants(t, F);
      float tol = side ? 0.12f : 0.06f;                // el tubo corto sube todo ~5 %
      CHECK(nf >= 2, "%c: menos de 2 formantes", NAME[v]);
      CHECK(fabsf(F[0] - TARGET[v][0]) < tol * TARGET[v][0], "%c lado %d: F1=%.0f (objetivo %.0f)", NAME[v], side, F[0], TARGET[v][0]);
      CHECK(fabsf(F[1] - TARGET[v][1]) < tol * TARGET[v][1], "%c lado %d: F2=%.0f (objetivo %.0f)", NAME[v], side, F[1], TARGET[v][1]);
      if (!side) { f1[v] = F[0]; f2[v] = F[1];
        printf("  %c          F1=%4.0f F2=%4.0f F3=%4.0f Hz   (objetivo %.0f / %.0f)\n", NAME[v], F[0], F[1], F[2], TARGET[v][0], TARGET[v][1]); }
    }
  }
  // Triángulo vocálico: A la más abierta (F1 mayor), I la más anterior (F2 mayor),
  // U la más posterior/cerrada (F2 menor).
  CHECK(f1[0] > f1[1] && f1[0] > f1[3] && f1[1] > f1[2] && f1[3] > f1[4], "orden de F1 (apertura) incorrecto");
  CHECK(f2[2] > f2[1] && f2[1] > f2[0] && f2[0] > f2[3] && f2[3] > f2[4], "orden de F2 (anterioridad) incorrecto");
}

static void testGlottis() {
  printf("[vocal] fuente glótica:\n");
  bool fin = true; double worstDc = 0; float mn = 0, mx = 0;
  for (int note = MIDI_LO; note <= MIDI_HI; note += 5) {
    float dt = midiToFreq(note) / SR, ph = 0; double sum = 0; int n = 0;
    int per = (int)(1.0f / dt + 0.5f) * 200;           // ~200 ciclos
    for (int i = 0; i < per; i++) {
      ph += dt; if (ph >= 1) ph -= 1;
      float s = vocalGlottisDeriv(ph, dt), u = vocalGlottisFlow(ph);
      if (!std::isfinite(s) || !std::isfinite(u) || u < 0.0f || u > 1.001f) fin = false;
      if (s < mn) mn = s; if (s > mx) mx = s;
      sum += s; n++;
    }
    double dc = fabs(sum / n); if (dc > worstDc) worstDc = dc;
  }
  CHECK(fin, "glotis: valor no finito o flujo fuera de 0..1");
  CHECK(worstDc < 0.01, "glotis: DC %.4f (la derivada del flujo debe tener media ~0)", worstDc);
  CHECK(mn < -0.6f && mn > -1.3f && mx > 0.2f && mx < 0.6f, "glotis: forma fuera de rango (min %.2f max %.2f)", mn, mx);
  printf("  glotis     DC max=%.5f  rango [%.2f, %.2f] en C2..G6\n", worstDc, mn, mx);
}

static void testTractStability() {
  printf("[vocal] estabilidad del tracto:\n");
  // Áreas extremas y saltos bruscos de vocal con excitación a tope: la salida
  // tiene que quedar acotada, finita y apagarse sola al cortar la fuente.
  VocalTract t; t.init(SR, 17.5f);
  uint32_t s = 12345u; bool fin = true; float pk = 0;
  for (int i = 0; i < 200000; i++) {
    s = s * 1664525u + 1013904223u;
    if ((i & 1023) == 0) {
      float ctrl[VOCAL_CTRL_POINTS];
      for (float& a : ctrl) { s = s * 1664525u + 1013904223u; a = 0.02f + 9.0f * (float)(s >> 8) / 16777216.0f; }
      t.setAreas(ctrl, 1.0f, (i & 4095) == 0);
    }
    if ((i & 255) == 0) t.control(256.0f / SR);
    float y = t.tick(((float)(s >> 8) / 8388608.0f) - 1.0f);
    if (!std::isfinite(y)) fin = false;
    if (fabsf(y) > pk) pk = fabsf(y);
  }
  float tail = 0;
  for (int i = 0; i < 44100; i++) { float y = t.tick(0.0f); if (i > 22050 && fabsf(y) > tail) tail = fabsf(y); }
  CHECK(fin, "tracto: valor no finito con áreas extremas");
  CHECK(pk < 400.0f, "tracto: salida descontrolada (pico %.1f)", pk);
  CHECK(tail < 1e-6f, "tracto: no se apaga sin excitación (cola %.2e)", tail);
  printf("  tracto     áreas aleatorias 0.02..9: pico=%.1f, cola a 0.5 s=%.1e\n", pk, tail);
}

// ---------------------------------------------------------------- B) motor
static void testTimbreTable() {
  printf("[vocal] tabla de timbres (compatibilidad de índices guardados):\n");
  // Los índices que ya están en NVS / sesión / paquetes no se pueden mover.
  CHECK(T_BRASS == 0 && T_WAVE == 7 && T_HBRASS == 8 && T_EPIANO_B == 9 && T_EPIANO_C == 10,
        "se movió un timbre base");
  CHECK(T_USER1 == 11 && T_USER6 == 16, "se movieron los slots USR (%d..%d)", (int)T_USER1, (int)T_USER6);
  CHECK(T_VOCAL == 17 && T_COUNT == 18, "T_VOCAL debe ser el ÚLTIMO (T_VOCAL=%d T_COUNT=%d)", (int)T_VOCAL, (int)T_COUNT);
  dspInit();
  CHECK(strcmp(dspTimbreName(T_VOCAL), "VOCAL") == 0, "nombre de T_VOCAL = %s", dspTimbreName(T_VOCAL));
  for (int t = 0; t < T_COUNT; t++) CHECK(dspTimbreName((uint8_t)t) != nullptr, "nombre nulo en timbre %d", t);
  CHECK(dspEnvGet(T_VOCAL).family == ENG_VOCAL, "T_VOCAL no usa el motor vocal");
  for (int t = 0; t < T_VOCAL; t++)
    CHECK(dspEnvGet((uint8_t)t).family != ENG_VOCAL, "el timbre %d quedó con el motor vocal", t);
  CHECK(sizeof(EnvCfg) == sizeof(dspEnvFactory(T_BRASS)), "EnvCfg inconsistente");
  printf("  indices    T_VOCAL=%d (último), USR=%d..%d, T_COUNT=%d\n", (int)T_VOCAL, (int)T_USER1, (int)T_USER6, (int)T_COUNT);
}

static void testSustain() {
  printf("[vocal] nota sostenida:\n");
  dspInit();
  dspNoteOn(60, T_VOCAL);
  std::vector<float> x; render(x, 2 * (int)SAMPLE_RATE);
  size_t n = x.size();
  double r1 = rms(x, n / 2, 3 * n / 4), r2 = rms(x, 3 * n / 4, n);
  double dc = 0; for (size_t i = n / 2; i < n; i++) dc += x[i]; dc /= (double)(n / 2);
  std::vector<float> tail(x.begin() + n / 2, x.end());
  double f0 = goertzel(tail, midiToFreq(60)), sub = goertzel(tail, 100.0), off = goertzel(tail, midiToFreq(60) * 1.06);
  CHECK(finite(x), "sostenida: NaN/Inf");
  CHECK(peak(x) < 0.98f, "sostenida: clip (pico %.3f)", peak(x));
  CHECK(r1 > 0.02 && r1 < 0.35, "sostenida: RMS fuera de rango (%.4f)", r1);
  CHECK(fabs(r1 - r2) < 0.15 * r1, "sostenida: el nivel no es estable (%.4f -> %.4f)", r1, r2);
  CHECK(fabs(dc) < 0.01, "sostenida: DC %.4f", dc);
  CHECK(f0 > 50.0 * sub && f0 > 50.0 * off, "sostenida: la fundamental no domina (f0=%.2e sub=%.2e vecina=%.2e)", f0, sub, off);
  CHECK(dspActiveVoices() == 1, "sostenida: voces activas = %d", dspActiveVoices());
  printf("  sostenida  C4: rms=%.4f pico=%.3f dc=%.5f f0/sub=%.0fx\n", r1, peak(x), dc, f0 / (sub + 1e-30));
}

static void testAttackRelease() {
  printf("[vocal] ataque / liberación:\n");
  dspInit();
  dspNoteOn(57, T_VOCAL);
  std::vector<float> x; render(x, (int)SAMPLE_RATE);          // 1 s sostenida
  size_t off = x.size();
  dspNoteOff(57);
  render(x, (int)SAMPLE_RATE);                                 // 1 s de cola
  double a0 = rms(x, 0, 220), sus = rms(x, off - 8820, off);
  double early = rms(x, off + 2205, off + 4410), late = rms(x, off + 30870, x.size());
  float jump = 0;                                              // salto entre muestras = click
  for (size_t i = 1; i < x.size(); i++) { float d = fabsf(x[i] - x[i - 1]); if (d > jump) jump = d; }
  float last = 0; for (size_t i = x.size() - 4410; i < x.size(); i++) if (fabsf(x[i]) > last) last = fabsf(x[i]);
  CHECK(finite(x), "atk/rel: NaN/Inf");
  CHECK(a0 < 0.25 * sus, "ataque: arranca de golpe (5 ms rms=%.4f, sostenido=%.4f)", a0, sus);
  CHECK(early < sus && early > 0.0, "release: no baja de forma gradual (%.4f vs %.4f)", early, sus);
  CHECK(late < 0.01 * sus, "release: no se apaga (%.5f)", late);
  CHECK(jump < 0.2f, "atk/rel: click (salto %.3f)", jump);
  CHECK(last == 0.0f, "release: el tracto no quedó en silencio exacto (%.2e)", last);
  CHECK(dspActiveVoices() == 0, "release: quedaron voces activas (%d)", dspActiveVoices());
  // Tras dormirse, una nota nueva arranca limpia (sin restos del tubo anterior).
  std::vector<float> a, b;
  dspNoteOn(57, T_VOCAL); render(a, 4096);
  dspInit(); dspNoteOn(57, T_VOCAL); render(b, 4096);
  double diff = 0; for (size_t i = 0; i < a.size(); i++) diff += fabs(a[i] - b[i]);
  CHECK(diff < 0.05 * a.size() * rms(b, 0, b.size()), "re-ataque distinto a un arranque limpio (diff %.4f)", diff);
  printf("  atk/rel    5ms=%.4f sostenido=%.4f cola=%.5f salto max=%.3f\n", a0, sus, late, jump);
}

// Energía de una banda (suma de Goertzel) para comparar vocales.
static double band(const std::vector<float>& x, double lo, double hi) {
  double s = 0; for (double f = lo; f <= hi; f += 20.0) s += goertzel(x, f); return s;
}
static void testVowelChange() {
  printf("[vocal] cambio de vocal:\n");
  // Misma nota en cada vocal: el espectro tiene que seguir a los formantes.
  double hiBand[VOCAL_VOWELS], midBand[VOCAL_VOWELS], lowBand[VOCAL_VOWELS];
  static const char* NAME = "AEIOU";
  for (int v = 0; v < VOCAL_VOWELS; v++) {
    dspInit(); setVowel((float)v / (VOCAL_VOWELS - 1));
    dspNoteOn(45, T_VOCAL);                                    // A2: armónicos densos
    std::vector<float> x; render(x, (int)SAMPLE_RATE);
    std::vector<float> t(x.begin() + x.size() / 2, x.end());
    double tot = band(t, 100, 4000);
    lowBand[v] = band(t, 600, 820) / tot;                      // F1 de la abierta (A): vacío en I/U
    midBand[v] = band(t, 950, 1450) / tot;                     // F2 de A (y borde del de O)
    hiBand[v]  = band(t, 1800, 3200) / tot;                    // F2/F3 de las anteriores (E, I)
    CHECK(finite(x) && rms(x, x.size() / 2, x.size()) > 0.01, "vocal %c: no suena", NAME[v]);
    printf("  %c          600-820=%.4f  950-1450=%.4f  1800-3200=%.4f\n", NAME[v], lowBand[v], midBand[v], hiBand[v]);
  }
  // Cada vocal se distingue de las demás por DÓNDE pone la energía.
  CHECK(lowBand[0] > 3.0 * lowBand[2] && lowBand[0] > 3.0 * lowBand[4], "A no es más abierta (F1 alto) que I/U");
  CHECK(midBand[0] > 4.0 * midBand[1] && midBand[0] > 4.0 * midBand[2] && midBand[0] > 4.0 * midBand[4], "A no se distingue de E/I/U");
  CHECK(hiBand[1] > 4.0 * hiBand[3] && hiBand[2] > 4.0 * hiBand[4], "E/I (anteriores) no se distinguen de O/U (posteriores)");
  // Cambio EN VIVO con la nota sonando: sin click, sin NaN, y el timbre cambia.
  dspInit(); setVowel(VOCAL_MORPH_A);
  dspNoteOn(45, T_VOCAL);
  std::vector<float> x; render(x, (int)SAMPLE_RATE);
  setVowel(VOCAL_MORPH_I);
  render(x, (int)SAMPLE_RATE);
  std::vector<float> before(x.begin() + 22050, x.begin() + 44100), after(x.begin() + 66150, x.end());
  float jump = 0; for (size_t i = 1; i < x.size(); i++) { float d = fabsf(x[i] - x[i - 1]); if (d > jump) jump = d; }
  double hb = band(before, 600, 820) / band(before, 100, 4000), ha = band(after, 600, 820) / band(after, 100, 4000);
  CHECK(finite(x), "cambio en vivo: NaN/Inf");
  CHECK(jump < 0.25f, "cambio en vivo: click (salto %.3f)", jump);
  CHECK(hb > 3.0 * ha, "cambio en vivo: A->I no cambió el espectro (%.3f -> %.3f)", hb, ha);
  CHECK(fabs(ha - lowBand[2]) < 0.25 * lowBand[2], "cambio en vivo: no llega a la misma I (%.4f vs %.4f)", ha, lowBand[2]);
  printf("  en vivo    A->I con la nota sonando: banda 600-820 %.4f -> %.4f, salto max=%.3f\n", hb, ha, jump);
}

static void testPitchRange() {
  printf("[vocal] rango de alturas (C2..G6) x 5 vocales:\n");
  double lo = 1e9, hi = 0; int bad = 0; float pk = 0;
  for (int v = 0; v < VOCAL_VOWELS; v++)
    for (int note = MIDI_LO; note <= MIDI_HI; note++) {
      dspInit(); setVowel((float)v / (VOCAL_VOWELS - 1));
      dspNoteOn((uint8_t)note, T_VOCAL);
      std::vector<float> x; render(x, 22050);
      std::vector<float> t(x.begin() + 11025, x.end());
      double r = rms(t, 0, t.size());
      double f0 = goertzel(t, midiToFreq(note)), off = goertzel(t, midiToFreq(note) * 1.4);
      bool ok = finite(x) && r > 0.004 && r < 0.45 && peak(x) < 0.98f && f0 > 20.0 * off;
      if (!ok) { bad++; if (bad <= 5) printf("    nota %d vocal %d: rms=%.4f pico=%.3f f0/off=%.1f\n", note, v, r, peak(x), f0 / (off + 1e-30)); }
      if (r < lo) lo = r; if (r > hi) hi = r; if (peak(x) > pk) pk = peak(x);
    }
  CHECK(bad == 0, "rango: %d combinaciones nota/vocal fuera de criterio", bad);
  printf("  rango      %d notas x 5 vocales: rms %.4f..%.4f, pico max=%.3f\n", MIDI_HI - MIDI_LO + 1, lo, hi, pk);
}

static void testChords() {
  printf("[vocal] acordes (1, 2, 4, 8 y 12 voces):\n");
  static const uint8_t NOTES[MAX_VOICES] = { 48, 55, 60, 64, 67, 71, 74, 52, 43, 76, 79, 36 };
  for (int nv : { 1, 2, 4, 8, 12 }) {
    dspInit();
    for (int i = 0; i < nv; i++) dspNoteOn(NOTES[i], T_VOCAL);
    std::vector<float> l, r; float bl[AUDIO_BLOCK], br[AUDIO_BLOCK];
    for (int b = 0; b < 200; b++) {                            // camino del firmware (con FX)
      dspRenderStereo(bl, br, (int)AUDIO_BLOCK);
      l.insert(l.end(), bl, bl + AUDIO_BLOCK); r.insert(r.end(), br, br + AUDIO_BLOCK);
    }
    double rl = rms(l, l.size() / 2, l.size()), rr = rms(r, r.size() / 2, r.size());
    double f0 = goertzel(std::vector<float>(l.begin() + l.size() / 2, l.end()), midiToFreq(NOTES[0]));
    CHECK(finite(l) && finite(r), "%d voces: NaN/Inf", nv);
    CHECK(dspActiveVoices() == nv, "%d voces: activas = %d (no debe haber tope propio)", nv, dspActiveVoices());
    // (una nota sola puede quedar casi toda en un canal: el paneo es por nota)
    CHECK(rl + rr > 0.03 && rl < 0.7 && rr < 0.7, "%d voces: nivel L/R fuera de rango (%.3f / %.3f)", nv, rl, rr);
    if (nv >= 4) CHECK(rl > 0.02 && rr > 0.02, "%d voces: un canal quedó vacío (%.3f / %.3f)", nv, rl, rr);
    CHECK(peak(l) <= 1.0f && peak(r) <= 1.0f, "%d voces: fuera de [-1,1]", nv);
    CHECK(f0 > 1e-7, "%d voces: se perdió la nota grave", nv);
    printf("  %2d voces   rms L=%.3f R=%.3f pico=%.3f\n", nv, rl, rr, peak(l) > peak(r) ? peak(l) : peak(r));
  }
  // Mezclado con otro timbre: el coro no rompe a los demás ni al revés.
  dspInit();
  dspNoteOn(48, T_STRINGS); dspNoteOn(60, T_VOCAL); dspNoteOn(67, T_EPIANO);
  std::vector<float> x; render(x, (int)SAMPLE_RATE);
  CHECK(finite(x) && rms(x, 22050, 44100) > 0.02, "mezcla de timbres con VOCAL");
  // Slot de usuario apuntado al motor vocal (familia ENG_VOCAL).
  dspInit();
  EnvCfg e = dspEnvGet(T_VOCAL); dspEnvSet(T_USER3, e);
  dspNoteOn(60, T_USER3);
  std::vector<float> u; render(u, 22050);
  CHECK(finite(u) && rms(u, 11025, 22050) > 0.02, "USR con familia vocal no suena");
}

static void testDeterminism() {
  printf("[vocal] determinismo:\n");
  std::vector<float> a, b;
  for (int pass = 0; pass < 2; pass++) {
    dspInit(); setVowel(VOCAL_MORPH_O);
    dspNoteOn(50, T_VOCAL); dspNoteOn(57, T_VOCAL);
    render(pass ? b : a, 22050);
  }
  CHECK(a == b, "dos renders idénticos difieren");
  printf("  determ.    dos renders iguales muestra a muestra: %s\n", a == b ? "si" : "NO");
}

// ---------------------------------------------------------------- WAVs
static void writeWavStereo(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("WARN: no pude escribir %s\n", path.c_str()); return; }
  uint32_t n = (uint32_t)L.size(), bytes = n * 4, sr = SAMPLE_RATE;
  auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + bytes); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2); w32(sr); w32(sr * 4); w16(4); w16(16);
  fwrite("data", 1, 4, f); w32(bytes);
  for (uint32_t i = 0; i < n; i++) {
    float l = L[i], r = R[i];
    if (l > 1) l = 1; if (l < -1) l = -1; if (r > 1) r = 1; if (r < -1) r = -1;
    w16((uint16_t)(int16_t)(l * 32767.0f)); w16((uint16_t)(int16_t)(r * 32767.0f));
  }
  fclose(f);
}
static void renderSt(std::vector<float>& L, std::vector<float>& R, float seconds) {
  float bl[AUDIO_BLOCK], br[AUDIO_BLOCK];
  for (int n = (int)(seconds * SR); n > 0; n -= (int)AUDIO_BLOCK) {
    dspRenderStereo(bl, br, (int)AUDIO_BLOCK);
    L.insert(L.end(), bl, bl + AUDIO_BLOCK); R.insert(R.end(), br, br + AUDIO_BLOCK);
  }
}
// La MISMA frase para cada vocal: nota sola, dos notas, y dos acordes de 4.
static void phrase(std::vector<float>& L, std::vector<float>& R) {
  dspNoteOn(57, T_VOCAL); renderSt(L, R, 1.2f); dspNoteOff(57); renderSt(L, R, 0.5f);
  dspNoteOn(48, T_VOCAL); dspNoteOn(55, T_VOCAL); renderSt(L, R, 1.2f);
  dspNoteOff(48); dspNoteOff(55); renderSt(L, R, 0.5f);
  static const uint8_t C1[4] = { 48, 55, 60, 64 }, C2[4] = { 45, 52, 60, 64 };
  for (uint8_t n : C1) dspNoteOn(n, T_VOCAL);
  renderSt(L, R, 1.6f);
  for (uint8_t n : C1) dspNoteOff(n);
  for (uint8_t n : C2) dspNoteOn(n, T_VOCAL);
  renderSt(L, R, 1.6f);
  for (uint8_t n : C2) dspNoteOff(n);
  renderSt(L, R, 1.2f);
}
static void exportWavs(const std::string& dir) {
  static const char* NAME = "AEIOU";
  for (int v = 0; v < VOCAL_VOWELS; v++) {
    dspInit(); setVowel((float)v / (VOCAL_VOWELS - 1));
    std::vector<float> L, R; phrase(L, R);
    writeWavStereo(dir + "/out_vocal_" + NAME[v] + ".wav", L, R);
  }
  { // acorde sostenido barriendo las cinco vocales (1 s cada una)
    dspInit(); setVowel(VOCAL_MORPH_A);
    std::vector<float> L, R;
    for (uint8_t n : { 45, 52, 57, 61 }) dspNoteOn(n, T_VOCAL);
    for (int v = 0; v < VOCAL_VOWELS; v++) { setVowel((float)v / (VOCAL_VOWELS - 1)); renderSt(L, R, 1.0f); }
    dspAllOff(); renderSt(L, R, 1.0f);
    writeWavStereo(dir + "/out_vocal_morph.wav", L, R);
  }
  { // escala por todo el registro útil, en A
    dspInit();
    std::vector<float> L, R;
    for (int note = MIDI_LO; note <= MIDI_HI; note += 5) {
      dspNoteOn((uint8_t)note, T_VOCAL); renderSt(L, R, 0.45f);
      dspNoteOff((uint8_t)note); renderSt(L, R, 0.15f);
    }
    renderSt(L, R, 0.8f);
    writeWavStereo(dir + "/out_vocal_escala.wav", L, R);
  }
  printf("[vocal] WAVs de escucha en %s/out_vocal_*.wav\n", dir.c_str());
}

int main(int argc, char** argv) {
  testTubeUniform();
  testVowelFormants();
  testGlottis();
  testTractStability();
  testTimbreTable();
  testSustain();
  testAttackRelease();
  testVowelChange();
  testPitchRange();
  testChords();
  testDeterminism();
  if (argc > 1) exportWavs(argv[1]);
  printf("\n%d checks, %d fallas\n", checks, failures);
  return failures ? 1 : 0;
}
