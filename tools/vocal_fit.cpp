/* ============================================================================
   vocal_fit.cpp  -  Ajuste offline (host) de las áreas del tracto por vocal.
   Usa el MISMO tracto de vocal.h: mide los formantes de su respuesta al impulso
   y mueve los 8 puntos de control hasta acercarlos a los objetivos. Imprime las
   tablas VOCAL_AREA / VOCAL_GAIN para pegar en vocal.cpp. Determinista.
   Compilar:  g++ -std=c++17 -O2 -I. tools/vocal_fit.cpp vocal.cpp vocal_text.cpp -o vocal_fit
   Uso:       vocal_fit            (ajusta e imprime tablas)
              vocal_fit 0..4       (ajusta sólo esa vocal: A E I O U)
              vocal_fit check      (sólo mide las tablas actuales de vocal.cpp)
   ============================================================================ */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include "vocal.h"

static const float SR = 44100.0f;
static const float LEN_CM = 17.5f;     // tracto L
static const float LEN_R_CM = 16.7f;   // tracto R (otro "cantante")

// Objetivos F1/F2/F3 (Hz): valores típicos redondeados de vocales del español,
// voz masculina adulta (ver docs/VOCAL.md).
static const float TARGET[VOCAL_VOWELS][3] = {
  { 700, 1200, 2500 },   // A
  { 450, 1950, 2600 },   // E
  { 280, 2300, 3000 },   // I
  { 480,  900, 2500 },   // O
  { 310,  720, 2400 },   // U
};
// Formas de partida cualitativas (glotis -> labios).
static const float START[VOCAL_VOWELS][VOCAL_CTRL_POINTS] = {
  { 0.6f, 0.5f, 0.6f, 1.2f, 3.0f, 5.0f, 5.5f, 4.5f },   // A: faringe angosta, boca abierta
  { 0.6f, 2.5f, 3.5f, 3.0f, 1.5f, 1.0f, 1.5f, 2.5f },   // E: faringe ancha, paladar medio
  { 0.6f, 3.5f, 5.0f, 4.0f, 1.0f, 0.3f, 0.4f, 1.5f },   // I: constricción palatal
  { 0.6f, 1.0f, 0.8f, 1.0f, 3.0f, 5.0f, 3.5f, 0.9f },   // O: lengua atrás, labios redondeados
  { 0.6f, 2.5f, 3.0f, 0.6f, 0.5f, 3.5f, 4.0f, 0.3f },   // U: constricción velar, labios cerrados
};
static const char* NAME = "AEIOU";

static uint32_t rngS = 0x1234567u;
static float frand() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5;
                       return (float)(rngS & 0xFFFFFF) / 16777216.0f; }
static float grand() { return (frand() + frand() + frand() + frand() - 2.0f) * 1.7f; }

// Mide los 3 primeros picos (Hz) de la respuesta del tracto. Devuelve cuántos halló.
static int measure(const float* ctrl, float lenCm, float* F) {
  static VocalTract t;
  t.init(SR, lenCm);
  t.setAreas(ctrl, 1.0f, true);
  const int N = 2048;
  static float ir[N];
  for (int i = 0; i < N; i++) ir[i] = t.tick(i == 0 ? 1.0f : 0.0f);
  const int NF = 760; const float F0 = 150.0f, DF = 5.0f;
  static float mag[NF];
  for (int j = 0; j < NF; j++) {
    float w = 6.2831853f * (F0 + DF * j) / SR, cw = cosf(w), sw = sinf(w);
    float re = 0, im = 0, c = 1, s = 0;
    for (int i = 0; i < N; i++) {
      re += ir[i] * c; im -= ir[i] * s;
      float c2 = c * cw - s * sw; s = s * cw + c * sw; c = c2;
    }
    mag[j] = re * re + im * im;
  }
  int found = 0;
  for (int j = 1; j < NF - 1 && found < 3; j++) {
    bool top = true;                                 // máximo en ±60 Hz (ignora rizado)
    for (int q = j - 12; q <= j + 12 && top; q++)
      if (q >= 0 && q < NF && q != j && mag[q] > mag[j]) top = false;
    if (top && mag[j] > mag[j - 1] && mag[j] >= mag[j + 1]) {
      float a = logf(mag[j - 1]), b = logf(mag[j]), c = logf(mag[j + 1]);
      float off = 0.5f * (a - c) / (a - 2 * b + c);
      F[found++] = F0 + DF * (j + off);
    }
  }
  return found;
}

static float cost(const float* ctrl, const float* targ) {
  // La forma tiene que dar la vocal con los DOS largos de tubo que usa el coro
  // (L = 17.5 cm, R = 16.7 cm: el más corto sube todos los formantes ~5 %).
  static const float W[3] = { 1.0f, 1.0f, 0.5f };
  float c = 0;
  for (int pass = 0; pass < 2; pass++) {
    float len = pass ? LEN_R_CM : LEN_CM, sc = pass ? LEN_CM / LEN_R_CM : 1.0f;
    float F[3]; int nf = measure(ctrl, len, F);
    if (nf < 3) return 100.0f;
    for (int i = 0; i < 3; i++) { float e = logf(F[i] / (targ[i] * sc)); c += W[i] * e * e; }
  }
  for (int i = 0; i < VOCAL_CTRL_POINTS - 1; i++) {   // suavidad: forma plausible
    float d = logf(ctrl[i] / ctrl[i + 1]); c += 0.003f * d * d;
  }
  return c;
}

// RMS de salida con la fuente glótica real a varias alturas -> nivelado por vocal.
static float levelGain(const float* ctrl) {
  static const float F0S[4] = { 110, 165, 220, 330 };
  double acc = 0; int cnt = 0;
  for (float f0 : F0S) {
    VocalTract t; t.init(SR, LEN_CM); t.setAreas(ctrl, 1.0f, true);
    float ph = 0, dt = f0 / SR;
    for (int i = 0; i < 22050; i++) {
      ph += dt; if (ph >= 1) ph -= 1;
      float y = t.tick(vocalGlottisDeriv(ph, dt));
      if (i >= 11025) { acc += (double)y * y; cnt++; }
    }
  }
  return 0.25f / (float)sqrt(acc / cnt);
}

int main(int argc, char** argv) {
  bool check = (argc > 1 && strcmp(argv[1], "check") == 0);
  int only = (argc > 1 && !check) ? atoi(argv[1]) : -1;   // ajustar una sola vocal
  float out[VOCAL_VOWELS][VOCAL_CTRL_POINTS], gains[VOCAL_VOWELS];
  for (int v = 0; v < VOCAL_VOWELS; v++) {
    if (only >= 0 && v != only) continue;
    float best[VOCAL_CTRL_POINTS];
    memcpy(best, check ? VOCAL_AREA[v] : START[v], sizeof(best));
    float bc = cost(best, TARGET[v]);
    if (!check) {
      const int IT = 5000;
      for (int it = 0; it < IT; it++) {
        float sig = 0.35f * (1.0f - (float)it / IT) + 0.02f;
        float tr[VOCAL_CTRL_POINTS]; memcpy(tr, best, sizeof(tr));
        int one = (it & 1) ? 1 + (int)(frand() * (VOCAL_CTRL_POINTS - 1)) : -1;
        for (int c = 1; c < VOCAL_CTRL_POINTS; c++) {   // c=0 (laringe) queda fijo
          if (one >= 0 && c != one) continue;
          tr[c] *= expf(grand() * sig);
          if (tr[c] < 0.15f) tr[c] = 0.15f;
          if (tr[c] > 8.0f)  tr[c] = 8.0f;
        }
        float tc = cost(tr, TARGET[v]);
        if (tc < bc) { bc = tc; memcpy(best, tr, sizeof(best)); }
      }
    }
    memcpy(out[v], best, sizeof(best));
    gains[v] = check ? VOCAL_GAIN[v] : levelGain(best);
    float F[3] = {0, 0, 0}, G[3] = {0, 0, 0};
    measure(best, LEN_CM, F); measure(best, LEN_R_CM, G);
    fprintf(stderr, "%c  objetivo %4.0f %4.0f %4.0f | 17.5cm %4.0f %4.0f %4.0f | 16.7cm %4.0f %4.0f %4.0f | costo %.5f\n",
            NAME[v], TARGET[v][0], TARGET[v][1], TARGET[v][2], F[0], F[1], F[2], G[0], G[1], G[2], bc);
  }
  if (check) return 0;
  if (only >= 0) {
    printf("  {");
    for (int c = 0; c < VOCAL_CTRL_POINTS; c++) printf(" %.3ff%s", out[only][c], c < VOCAL_CTRL_POINTS - 1 ? "," : "");
    printf(" },   // %c  gain %.4ff\n", NAME[only], gains[only]);
    return 0;
  }
  printf("const float VOCAL_AREA[VOCAL_VOWELS][VOCAL_CTRL_POINTS] = {\n");
  for (int v = 0; v < VOCAL_VOWELS; v++) {
    printf("  {");
    for (int c = 0; c < VOCAL_CTRL_POINTS; c++) printf(" %.3ff%s", out[v][c], c < VOCAL_CTRL_POINTS - 1 ? "," : "");
    printf(" },   // %c\n", NAME[v]);
  }
  printf("};\nconst float VOCAL_GAIN[VOCAL_VOWELS] = {");
  for (int v = 0; v < VOCAL_VOWELS; v++) printf(" %.4ff%s", gains[v], v < VOCAL_VOWELS - 1 ? "," : "");
  printf(" };\n");
  return 0;
}
