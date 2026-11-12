/* ============================================================================
   test_vocal_text.cpp  -  Tests de host de la etapa 2 del timbre VOCAL:
     A) texto -> fonemas -> sílabas (reglas del español), sin audio;
     B) consonantes sobre el tracto (oclusivas, fricativas, nasales, líquidas);
     C) letra en el motor: una sílaba por ataque, acordes, coda, reinicio.
   Con un directorio como argumento exporta WAVs estéreo para escucha:
     out_vocal_txt_*.wav
   Compilar/correr:
     g++ -std=c++17 -I. test/test_vocal_text.cpp dsp.cpp vocal.cpp vocal_text.cpp -o test_vocal_text
     ./test_vocal_text [dir_salida_wav]
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "dsp.h"
#include "vocal.h"
#include "vocal_text.h"

static int checks = 0, failures = 0;
#define CHECK(cond, msg, ...) do { checks++; if(!(cond)) { failures++; \
  printf("FAIL: " msg "\n", ##__VA_ARGS__); } } while(0)

static const double PI_D = 3.14159265358979323846;
static const float  SR = (float)SAMPLE_RATE;

static double goertzel(const std::vector<float>& x, double freq) {
  double w = 2.0 * PI_D * freq / SR, coeff = 2.0 * cos(w), s1 = 0, s2 = 0;
  for (float v : x) { double s0 = v + coeff * s1 - s2; s2 = s1; s1 = s0; }
  return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / ((double)x.size() * x.size());
}
static double band(const std::vector<float>& x, double lo, double hi, double step) {
  double s = 0; for (double f = lo; f <= hi; f += step) s += goertzel(x, f); return s;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) {
  if (b > x.size()) b = x.size();
  double s = 0; for (size_t i = a; i < b; i++) s += (double)x[i] * x[i];
  return sqrt(s / (double)(b - a));
}
static bool finite(const std::vector<float>& x) {
  for (float v : x) if (!std::isfinite(v)) return false;
  return true;
}
static std::vector<float> slice(const std::vector<float>& x, int msA, int msB) {
  size_t a = (size_t)(msA * 44.1), b = (size_t)(msB * 44.1);
  if (b > x.size()) b = x.size();
  return std::vector<float>(x.begin() + a, x.begin() + b);
}
static void render(std::vector<float>& out, int ms) {
  float b[64];
  for (int n = (int)(ms * 44.1); n > 0; n -= 64) { dspRenderBlock(b, 64); out.insert(out.end(), b, b + 64); }
}
static std::string sylText(const char* text) {
  VocalLyrics ly; int n = vocalTextToLyrics(text, &ly);
  std::string s;
  for (int i = 0; i < n; i++) { char b[16]; vocalSylToText(ly.syl[i], b, sizeof(b)); if (i) s += "-"; s += b; }
  return s;
}

// ------------------------------------------------------------ A) texto
static void testText() {
  printf("[vtext] texto -> sílabas (español):\n");
  static const struct { const char* in; const char* out; } T[] = {
    { "hola", "o-la" },               { "queso", "ke-so" },          { "guitarra", "gi-ta-rra" },
    { "ping\xC3\xBCino", "pin-gui-no" }, { "chico", "chi-ko" },      { "lluvia", "yu-bia" },
    { "ni\xC3\xB1o", "ni-nyo" },      { "cielo", "sie-lo" },         { "pa\xC3\xADs", "pa-is" },
    { "aire", "ai-re" },              { "perro", "pe-rro" },         { "rosa", "rro-sa" },
    { "enredo", "en-rre-do" },        { "examen", "ek-sa-men" },     { "transporte", "trans-por-te" },
    { "abstracto", "abs-trak-to" },   { "yo", "yo" },                { "rey", "rrei" },
    { "muy", "mui" },                 { "construir", "kons-truir" }, { "poeta", "po-e-ta" },
    { "d\xC3\xAD" "a", "di-a" },      { "agua", "a-gua" },           { "extra", "eks-tra" },
    { "atlas", "at-las" },            { "ciudad", "siu-dad" },       { "buey", "buei" },
    { "huevo", "ue-bo" },             { "guerra", "ge-rra" },        { "zapato", "sa-pa-to" },
    { "gente", "jen-te" },            { "jam\xC3\xB3n", "ja-mon" },  { "vaca", "ba-ka" },
    { "Tolhuin canta", "to-luin-kan-ta" },  { "CASA  de, papel!", "ka-sa-de-pa-pel" },
    { "ca\xC3\xB1" "a y az\xC3\xBA" "car", "ka-nya-i-a-su-kar" },
    { "", "" },                       { "123 ... ?", "" },           { "xilofon", "si-lo-fon" },
  };
  int bad = 0;
  for (const auto& t : T) {
    std::string got = sylText(t.in);
    CHECK(got == t.out, "\"%s\" -> \"%s\" (esperado \"%s\")", t.in, got.c_str(), t.out);
    if (got != t.out) bad++;
  }
  // Estructura de una sílaba: ataque / núcleo / coda y vocal principal.
  VocalLyrics ly; vocalTextToLyrics("trans", &ly);
  CHECK(ly.count == 1 && ly.syl[0].onset[0] == VP_T && ly.syl[0].onset[1] == VP_R &&
        ly.syl[0].nuc[0] == VP_A && ly.syl[0].coda[0] == VP_N && ly.syl[0].coda[1] == VP_S, "estructura de \"trans\"");
  vocalTextToLyrics("buey", &ly);
  CHECK(ly.count == 1 && ly.syl[0].main == 1 && ly.syl[0].nuc[1] == VP_E, "triptongo: la vocal principal de \"buey\" es la e");
  // Entradas hostiles: no desbordan ni cuelgan.
  std::string longTxt; for (int i = 0; i < 400; i++) longTxt += "tra ";
  int n = vocalTextToLyrics(longTxt.c_str(), &ly);
  CHECK(n == VOCAL_SYL_MAX && ly.count == VOCAL_SYL_MAX, "texto largo: se recorta a %d sílabas (dio %d)", VOCAL_SYL_MAX, n);
  CHECK(vocalTextToLyrics("\xFF\xFE\xC3", &ly) == 0, "bytes inválidos no generan sílabas");
  CHECK(vocalTextToLyrics("bcdfghjklmnpqrstvwxz", &ly) <= 1, "palabra sin vocales");
  CHECK(vocalTextToLyrics(nullptr, &ly) == 0, "texto nulo");
  printf("  texto      %d casos, %d distintos de lo esperado; \"transporte\" -> %s\n",
         (int)(sizeof(T) / sizeof(T[0])), bad, sylText("transporte").c_str());
}

// ------------------------------------------------------------ B) consonantes
// Canta UNA sílaba desde el silencio y devuelve el audio (nota A2, 600 ms).
static std::vector<float> sing(const char* syl, int ms = 600, uint8_t note = 45) {
  dspInit(); dspVocalLyricsOn(true); dspVocalSetText(syl);
  dspNoteOn(note, T_VOCAL);
  std::vector<float> x; render(x, ms);
  return x;
}
static void testConsonants() {
  printf("[vtext] consonantes sobre el tracto:\n");
  std::vector<float> a = sing("a");
  double vowel = rms(a, 13230, 22050);                       // nivel de la vocal sostenida
  CHECK(vowel > 0.02, "la vocal de referencia no suena (%.4f)", vowel);

  // Oclusiva sorda: silencio durante el cierre, después explosión y vocal.
  std::vector<float> pa = sing("pa");
  double clo = rms(pa, 441, 2646), aft = rms(pa, 13230, 22050);
  CHECK(clo < 0.02 * vowel, "p: el cierre no es silencio (%.5f)", clo);
  CHECK(fabs(aft - vowel) < 0.2 * vowel, "pa: no llega a la misma A (%.4f vs %.4f)", aft, vowel);
  printf("  p          cierre=%.5f  vocal=%.4f\n", clo, aft);

  // Fricativas: ruido sin sonoridad, y cada lugar de articulación con su color.
  double hf[3]; const char* FR[3] = { "sa", "fa", "ja" };
  for (int i = 0; i < 3; i++) {
    std::vector<float> x = sing(FR[i]);
    std::vector<float> fr = slice(x, 20, 80);
    double lvl = rms(fr, 0, fr.size());
    double f0 = goertzel(fr, 110.0), tot = band(fr, 100, 10000, 100);
    hf[i] = band(fr, 3500, 10000, 100) / tot;
    CHECK(lvl > 0.15 * vowel && lvl < 1.5 * vowel, "%s: nivel de la fricción fuera de rango (%.4f, vocal %.4f)", FR[i], lvl, vowel);
    CHECK(f0 < 0.02 * tot, "%s: la fricativa sorda tiene sonoridad", FR[i]);
    CHECK(fabs(rms(x, 13230, 22050) - vowel) < 0.2 * vowel, "%s: no llega a la A", FR[i]);
    printf("  %c          fricción=%.4f  agudos(>3.5k)=%.2f\n", FR[i][0], lvl, hf[i]);
  }
  CHECK(hf[0] > 0.5, "s: no es un ruido agudo (%.2f)", hf[0]);
  CHECK(hf[0] > 1.5 * hf[2], "s y j suenan igual (%.2f vs %.2f)", hf[0], hf[2]);

  // Nasales: suenan (por la nariz) con la boca cerrada, graves y más suaves que la vocal.
  const char* NA[3] = { "ma", "na", "\xC3\xB1" "a" };
  for (int i = 0; i < 3; i++) {
    std::vector<float> x = sing(NA[i]);
    std::vector<float> mu = slice(x, 30, 75);
    double lvl = rms(mu, 0, mu.size());
    double lo = band(mu, 100, 600, 10), hi = band(mu, 1500, 5000, 50);
    CHECK(lvl > 0.03 * vowel && lvl < 0.8 * vowel, "nasal %d: murmullo fuera de rango (%.4f, vocal %.4f)", i, lvl, vowel);
    CHECK(lo > 5.0 * hi, "nasal %d: el murmullo no es grave", i);
    CHECK(fabs(rms(x, 13230, 22050) - vowel) < 0.2 * vowel, "nasal %d: no llega a la A", i);
    if (i == 0) printf("  m          murmullo=%.4f (vocal %.4f)  graves/agudos=%.0fx\n", lvl, vowel, lo / (hi + 1e-30));
  }
  // Sin velo abierto, un cierre de labios sonoro NO deja salir casi nada: la
  // diferencia entre "ma" y "ba" es la rama nasal.
  {
    std::vector<float> m = sing("ma"), b = sing("ba");
    double lm = rms(m, 1323, 2205), lb = rms(b, 1323, 2205);
    CHECK(lm > 2.0 * lb, "m no suena más que b durante el cierre (%.5f vs %.5f)", lm, lb);
  }
  // Vibrante múltiple: el nivel sube y baja varias veces antes de la vocal.
  {
    std::vector<float> x = sing("rra");
    int flips = 0; double prev = 0; int dir = 0;
    for (int ms = 0; ms < 110; ms += 6) {
      double r = rms(x, (size_t)(ms * 44.1), (size_t)((ms + 6) * 44.1));
      int d = (r > prev) ? 1 : -1; if (dir && d != dir) flips++; dir = d; prev = r;
    }
    CHECK(flips >= 3, "rr: no se oyen los golpes de la vibrante (%d cambios)", flips);
    printf("  rr         %d cambios de nivel en 110 ms\n", flips);
  }
  // Todas las consonantes con todas las vocales: finito, acotado y termina en la vocal.
  int bad = 0; float pk = 0;
  static const char* CONS[] = { "p", "b", "t", "d", "k", "g", "f", "s", "j", "ch", "m", "n",
                                "\xC3\xB1", "l", "r", "rr", "y", "tr", "pl", "gr" };
  for (const char* c : CONS)
    for (const char* v : { "a", "e", "i", "o", "u" }) {
      std::string syl = std::string(c) + v;
      std::vector<float> x = sing(syl.c_str(), 450);
      std::vector<float> ref = sing(v, 450);
      double a1 = rms(x, 15435, x.size()), a2 = rms(ref, 15435, ref.size());
      bool ok = finite(x) && fabs(a1 - a2) < 0.25 * a2;
      for (float s : x) if (fabsf(s) > pk) pk = fabsf(s);
      if (!ok) { bad++; if (bad <= 5) printf("    \"%s\": final=%.4f vocal sola=%.4f\n", syl.c_str(), a1, a2); }
    }
  CHECK(bad == 0, "%d sílabas CV no terminan en su vocal", bad);
  CHECK(pk < 0.98f, "sílabas CV: clip (pico %.3f)", pk);
  printf("  CV         %d sílabas (20 ataques x 5 vocales): pico max=%.3f\n", (int)(sizeof(CONS) / sizeof(CONS[0])) * 5, pk);
}

static void testArticModule() {
  printf("[vtext] articulación aislada (sin dsp):\n");
  // Gestos al azar sobre un tracto, con excitación y ruido a tope: siempre finito
  // y acotado, y la cola de tramos nunca desborda.
  VocalTract t; t.init(SR, 17.5f);
  VocalArtic a; a.reset(0.0f);
  VocalLyrics ly; vocalTextToLyrics("transportes abstractos construir buey guerra ni\xC3\xB1o chancho", &ly);
  uint32_t s = 777u; bool fin = true; float pk = 0; int syl = 0;
  for (int i = 0; i < 400000; i++) {
    s = s * 1664525u + 1013904223u;
    if ((i % 3000) == 0) {
      a.flush();                                       // como el motor: manda la sílaba nueva
      if (i % 9000 == 0) a.closeSyllable(ly.syl[syl % ly.count]);
      a.startSyllable(ly.syl[(syl++) % ly.count], (i & 1) != 0);
    }
    if ((i & 63) == 0) { a.control(64.0f / SR); if (a.dirty) { a.apply(t); a.dirty = false; } }
    float nz = ((float)(s >> 8) / 8388608.0f - 1.0f);
    float y = t.tickFull(nz * a.voiceT, nz * a.noiseT);
    if (!std::isfinite(y)) fin = false;
    if (fabsf(y) > pk) pk = fabsf(y);
    if (a.qn > VOCAL_SEG_MAX) fin = false;
  }
  CHECK(fin, "articulación: valor no finito o cola desbordada");
  CHECK(pk < 400.0f, "articulación: salida descontrolada (pico %.1f)", pk);
  // Tras vaciar la cola queda en vocal abierta: sin constricción, velo cerrado, sonora.
  for (int i = 0; i < 4000; i++) a.control(64.0f / SR);
  CHECK(!a.busy() && a.c < 1e-3f && a.velum < 1e-3f && a.voiceT == 1.0f && a.noiseT == 0.0f,
        "articulación: no vuelve al reposo (c=%.3f velo=%.3f voz=%.2f ruido=%.2f)", a.c, a.velum, a.voiceT, a.noiseT);
  printf("  gestos     400k muestras de sílabas encadenadas: pico=%.1f, vuelve al reposo\n", pk);
}

// ------------------------------------------------------------ C) letra en el motor
static void testLyricsEngine() {
  printf("[vtext] letra en el motor:\n");
  dspInit(); dspVocalLyricsOn(true); dspVocalSetText("la casa de papel");
  CHECK(dspVocalSyllableCount() == 6, "\"la casa de papel\" = %d sílabas (esperado 6)", dspVocalSyllableCount());
  CHECK(dspVocalLyricsActive(), "la letra no quedó activa");
  std::vector<float> x;
  // Un ACORDE de 4 notas = UNA sílaba.
  for (uint8_t n : { 48, 55, 60, 64 }) dspNoteOn(n, T_VOCAL);
  render(x, 300);
  CHECK(dspVocalSyllable() == 1, "un acorde avanzó %d sílabas (esperado 1)", dspVocalSyllable());
  dspAllOff(); render(x, 500);
  // Notas separadas: una sílaba cada una; al terminar la letra vuelve a empezar.
  for (int i = 1; i < 6; i++) {
    dspNoteOn(57, T_VOCAL); render(x, 250);
    CHECK(dspVocalSyllable() == (i + 1) % 6, "nota %d: próxima sílaba = %d", i, dspVocalSyllable());
    dspNoteOff(57); render(x, 250);
  }
  CHECK(dspVocalSyllable() == 0, "la letra no dio la vuelta");
  // Reinicio.
  dspNoteOn(57, T_VOCAL); render(x, 200); dspNoteOff(57); render(x, 300);
  dspVocalLyricsReset();
  CHECK(dspVocalSyllable() == 0, "reset no vuelve a la primera sílaba");
  CHECK(finite(x), "letra: NaN/Inf");
  // Rasgueo (notas a 35 ms): sigue siendo una sola sílaba.
  dspInit(); dspVocalSetText("la casa de papel");
  const uint8_t st[4] = { 48, 55, 60, 64 };
  dspStrumChord(st, 4, T_VOCAL, 35.0f, SR);
  render(x, 400);
  CHECK(dspVocalSyllable() == 1, "el rasgueo avanzó %d sílabas (esperado 1)", dspVocalSyllable());
  // Coda al soltar: en "can" la n suena DESPUÉS de soltar, por la nariz, y luego silencio.
  dspInit(); dspVocalSetText("can");
  std::vector<float> c;
  dspNoteOn(45, T_VOCAL); render(c, 500);
  size_t off = c.size();
  dspNoteOff(45); render(c, 1200);
  CHECK(finite(c), "coda: NaN/Inf");
  float last = 0; for (size_t i = c.size() - 4410; i < c.size(); i++) if (fabsf(c[i]) > last) last = fabsf(c[i]);
  CHECK(last == 0.0f && dspActiveVoices() == 0, "coda: no termina en silencio (%.2e, %d voces)", last, dspActiveVoices());
  CHECK(rms(c, off + 441, off + 4410) > 0.0, "coda: nada suena al soltar");
  // Sin letra (o apagada) se comporta como la etapa 1: misma salida que un motor sin letra.
  std::vector<float> a1, a2, a3;
  dspInit(); dspVocalSetText(""); dspNoteOn(52, T_VOCAL); render(a1, 400);
  dspInit(); dspVocalSetText("pa"); dspVocalLyricsOn(false); dspNoteOn(52, T_VOCAL); render(a2, 400);
  dspVocalLyricsOn(true);
  dspInit(); dspVocalSetText("pa"); dspNoteOn(52, T_VOCAL); render(a3, 400);
  CHECK(a1 == a2, "letra apagada no equivale a sin letra");
  CHECK(!(a1 == a3), "con letra \"pa\" suena igual que sin letra");
  CHECK(!dspVocalLyricsActive() || dspVocalSyllableCount() == 1, "estado de la letra");
  // La vocal de la sílaba manda sobre wtMorph: "li" con el timbre en A suena a I.
  dspInit(); dspVocalSetText("li");
  dspNoteOn(45, T_VOCAL); std::vector<float> li; render(li, 700);
  std::vector<float> t = slice(li, 350, 700);
  double open = band(t, 600, 820, 20) / band(t, 100, 4000, 20);
  CHECK(open < 0.02, "\"li\" no suena a I (banda del F1 de A = %.3f)", open);
  // Otros timbres siguen igual con una letra cargada.
  dspInit(); dspVocalSetText("la la la");
  std::vector<float> b1, b2;
  dspNoteOn(60, T_STRINGS); render(b1, 300);
  dspVocalSetText(""); dspInit(); dspNoteOn(60, T_STRINGS); render(b2, 300);
  CHECK(b1 == b2, "una letra cargada cambió el sonido de STRINGS");
  dspVocalSetText("");
  printf("  motor      acorde=1 sílaba, rasgueo=1 sílaba, vuelta completa, coda al soltar, sin letra = etapa 1\n");
}

static void testLegatoAndMix() {
  printf("[vtext] ligado, mono y polifonía con letra:\n");
  // Frase ligada (nota nueva antes de soltar la anterior) + acordes de 8: finito y sin clip.
  dspInit(); dspVocalLyricsOn(true); dspVocalSetText("canta la voz del sur");
  std::vector<float> l, r; float bl[AUDIO_BLOCK], br[AUDIO_BLOCK];
  static const uint8_t CH[8] = { 36, 43, 48, 52, 55, 60, 64, 67 };
  for (int rep = 0; rep < 10; rep++) {
    for (int i = 0; i < 8; i++) dspNoteOn((uint8_t)(CH[i] + (rep % 3)), T_VOCAL);
    for (int b = 0; b < 40; b++) { dspRenderStereo(bl, br, AUDIO_BLOCK); l.insert(l.end(), bl, bl + AUDIO_BLOCK); r.insert(r.end(), br, br + AUDIO_BLOCK); }
    if (rep & 1) dspAllOff();                         // la mitad ligadas, la mitad sueltas
  }
  float pk = 0; for (float v : l) if (fabsf(v) > pk) pk = fabsf(v);
  CHECK(finite(l) && finite(r), "ligado: NaN/Inf");
  CHECK(pk <= 1.0f && rms(l, 0, l.size()) > 0.01, "ligado: nivel (pico %.3f)", pk);
  // Mono con legato: cada nota nueva es una sílaba.
  dspInit(); dspVocalSetText("do re mi fa");
  std::vector<float> m;
  for (int i = 0; i < 4; i++) { dspMonoNote((uint8_t)(57 + i), T_VOCAL); render(m, 250); }
  CHECK(dspVocalSyllable() == 0 && finite(m), "mono legato: no cantó las 4 sílabas (próxima=%d)", dspVocalSyllable());
  // Glide de acordes: cada acorde es una sílaba.
  dspInit(); dspVocalSetText("do re mi fa"); dspSetGlide(true, 80.0f);
  const uint8_t c1[3] = { 48, 52, 55 }, c2[3] = { 50, 53, 57 };
  dspGlideChord(c1, 3, T_VOCAL); render(m, 250);
  dspGlideChord(c2, 3, T_VOCAL); render(m, 250);
  CHECK(dspVocalSyllable() == 2 && finite(m), "glide de acordes: próxima sílaba = %d (esperado 2)", dspVocalSyllable());
  dspSetGlide(false, 0); dspVocalSetText("");
  printf("  ligado     8 voces con letra: pico=%.3f; mono y glide avanzan sílaba\n", pk);
}

// ------------------------------------------------------------ WAVs
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
static void renderSt(std::vector<float>& L, std::vector<float>& R, int ms) {
  float bl[AUDIO_BLOCK], br[AUDIO_BLOCK];
  for (int n = (int)(ms * 44.1); n > 0; n -= (int)AUDIO_BLOCK) {
    dspRenderStereo(bl, br, (int)AUDIO_BLOCK);
    L.insert(L.end(), bl, bl + AUDIO_BLOCK); R.insert(R.end(), br, br + AUDIO_BLOCK);
  }
}
// Canta `text` una sílaba por nota sobre `mel` (se repite). chord = suma 3ra y 5ta.
static void singPhrase(const std::string& path, const char* text, const uint8_t* mel, int nmel,
                       int noteMs, int gapMs, bool chord) {
  dspInit(); dspVocalLyricsOn(true); dspVocalSetText(text);
  std::vector<float> L, R;
  int n = dspVocalSyllableCount();
  for (int i = 0; i < n; i++) {
    uint8_t note = mel[i % nmel];
    dspNoteOn(note, T_VOCAL);
    if (chord) { dspNoteOn((uint8_t)(note + 4), T_VOCAL); dspNoteOn((uint8_t)(note + 7), T_VOCAL); dspNoteOn((uint8_t)(note - 12), T_VOCAL); }
    renderSt(L, R, noteMs);
    if (gapMs > 0) { dspAllOff(); renderSt(L, R, gapMs); }
  }
  dspAllOff(); renderSt(L, R, 1200);
  writeWavStereo(path, L, R);
  dspVocalSetText("");
}
static void exportWavs(const std::string& dir) {
  static const uint8_t M1[] = { 57, 57, 59, 60, 59, 57, 55, 57 };
  static const uint8_t M2[] = { 48, 50, 52, 53, 55, 53, 52, 50 };
  singPhrase(dir + "/out_vocal_txt_hola.wav", "hola mundo, tolhuin canta", M1, 8, 420, 90, false);
  singPhrase(dir + "/out_vocal_txt_frase.wav", "la voz del sur canta en la monta\xC3\xB1" "a", M1, 8, 420, 90, false);
  singPhrase(dir + "/out_vocal_txt_acordes.wav", "tierra del fuego, viento y mar", M2, 8, 620, 120, true);
  singPhrase(dir + "/out_vocal_txt_ligado.wav", "la voz del sur canta en la monta\xC3\xB1" "a", M1, 8, 480, 0, false);
  singPhrase(dir + "/out_vocal_txt_oclusivas.wav", "pa ta ca ba da ga", M1, 1, 450, 150, false);
  singPhrase(dir + "/out_vocal_txt_fricativas.wav", "sa fa ja cha ya", M1, 1, 450, 150, false);
  singPhrase(dir + "/out_vocal_txt_nasales_liquidas.wav", "ma na \xC3\xB1" "a la ara rra", M1, 1, 450, 150, false);
  printf("[vtext] WAVs de escucha en %s/out_vocal_txt_*.wav\n", dir.c_str());
}

int main(int argc, char** argv) {
  testText();
  testConsonants();
  testArticModule();
  testLyricsEngine();
  testLegatoAndMix();
  if (argc > 1) exportWavs(argv[1]);
  printf("\n%d checks, %d fallas\n", checks, failures);
  return failures ? 1 : 0;
}
