/* ============================================================================
   bench_voicelead.cpp  -  Benchmark de HOST de harmonyVoiceLeadMode (CHORALE /
   CONTRARY). NO forma parte del firmware ni de los tests funcionales: es un
   programa aparte que se compila y corre a mano (o desde run_tests.ps1 con un
   flag), para separar el rendimiento NORMAL (3-5 voces) del MÁXIMO teórico (7-8).

   Compilar/correr:
     g++ -O2 -std=c++17 -I.. bench_voicelead.cpp ../harmony.cpp -o bench_voicelead
     ./bench_voicelead

   Mide, por configuración (n, modo, primer acorde vs con historial):
     mediana, p95, máximo, candidatos evaluados, tiempo por candidato, y la
     diferencia CHORALE vs CONTRARY. El reloj es de pared (host); los números son
     RELATIVOS (no predicen microsegundos del ESP32-S3), sirven para detectar
     desproporción entre el caso normal y el peor caso.
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <chrono>

#include "harmony.h"
#include "config.h"

extern long gVlCandidatesEvaluated;   // contador host (definido en harmony.cpp)

using Clock = std::chrono::steady_clock;

// Acordes de prueba por cantidad de voces (clases de altura distintas).
static void chordFor(int n, uint8_t* out) {
  static const uint8_t C3[] = {60,64,67};             // C mayor
  static const uint8_t C4[] = {60,64,67,71};          // Cmaj7
  static const uint8_t C5[] = {60,64,67,71,74};       // Cmaj9
  static const uint8_t C7[] = {60,62,64,65,67,69,71}; // escala entera (7 clases)
  static const uint8_t C8[] = {60,61,62,63,64,65,66,67};
  const uint8_t* s = (n==3)?C3:(n==4)?C4:(n==5)?C5:(n==7)?C7:C8;
  for (int i = 0; i < n; i++) out[i] = s[i];
}

struct Stat { double medNs, p95Ns, maxNs; long cand; };

static Stat benchOne(int n, VoiceLeadMode mode, bool withHistory, int iters) {
  uint8_t pristine[MAX_CHORD_NOTES]; chordFor(n, pristine);
  uint8_t root = pristine[0] % 12;
  uint8_t buf[MAX_CHORD_NOTES];

  // Candidatos evaluados (una llamada aislada; sólo depende de n).
  harmonyVoiceLeadReset();
  memcpy(buf, pristine, n);
  gVlCandidatesEvaluated = 0;
  harmonyVoiceLeadMode(buf, (uint8_t)n, root, mode);
  long cand = gVlCandidatesEvaluated;

  // Historial: para "con historial" se calienta una vez y NO se resetea entre
  // iteraciones (progresión del mismo acorde -> converge). Para "primer acorde"
  // se resetea antes de cada llamada (siempre sin historial).
  harmonyVoiceLeadReset();
  if (withHistory) { memcpy(buf, pristine, n); harmonyVoiceLeadMode(buf, (uint8_t)n, root, mode); }

  std::vector<double> samp; samp.reserve(iters);
  volatile uint32_t sink = 0;
  for (int it = 0; it < iters; it++) {
    if (!withHistory) harmonyVoiceLeadReset();
    memcpy(buf, pristine, n);                       // refresca la entrada (se muta in-place)
    auto t0 = Clock::now();
    harmonyVoiceLeadMode(buf, (uint8_t)n, root, mode);
    auto t1 = Clock::now();
    samp.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    sink += buf[0] + buf[n-1];                      // evita que se optimice la llamada
  }
  (void)sink;
  std::sort(samp.begin(), samp.end());
  Stat s;
  s.medNs = samp[samp.size()/2];
  s.p95Ns = samp[(size_t)(samp.size()*0.95)];
  s.maxNs = samp.back();
  s.cand  = cand;
  return s;
}

int main() {
  printf("=== bench harmonyVoiceLeadMode (host, -O2; ns por llamada) ===\n");
  printf("  reloj de pared; numeros RELATIVOS (no us del ESP32-S3)\n\n");
  printf("  %-3s %-9s %-9s %8s %8s %8s %8s %10s\n",
         "n", "modo", "hist", "med(ns)", "p95(ns)", "max(ns)", "cand", "ns/cand");

  int ns[5]      = { 3, 4, 5, 7, 8 };
  VoiceLeadMode modes[2] = { VL_CHORALE, VL_CONTRARY };
  const char* mn[2] = { "CHORALE", "CONTRARY" };

  double medCho[5] = {0}, medCon[5] = {0};

  for (int ni = 0; ni < 5; ni++) {
    int n = ns[ni];
    int iters = (n <= 5) ? 10000 : 2000;             // >=10k para n<=5; peor caso declarado 2000
    for (int hi = 0; hi < 2; hi++) {
      for (int mi = 0; mi < 2; mi++) {
        Stat s = benchOne(n, modes[mi], hi == 1, iters);
        printf("  %-3d %-9s %-9s %8.0f %8.0f %8.0f %8ld %10.2f\n",
               n, mn[mi], hi ? "hist" : "first",
               s.medNs, s.p95Ns, s.maxNs, s.cand, s.medNs / (s.cand ? s.cand : 1));
        if (hi == 1) { if (mi == 0) medCho[ni] = s.medNs; else medCon[ni] = s.medNs; }
      }
    }
    printf("      (iters=%d)\n", iters);
  }

  printf("\n  CHORALE vs CONTRARY (mediana con historial):\n");
  for (int ni = 0; ni < 5; ni++)
    printf("    n=%d  CHORALE=%.0f ns  CONTRARY=%.0f ns  (x%.2f)\n",
           ns[ni], medCho[ni], medCon[ni], medCho[ni] > 0 ? medCon[ni]/medCho[ni] : 0.0);

  printf("\n  Nota: n<=5 (uso musical habitual) vs n=7/8 (máximo teórico). El salto\n");
  printf("  refleja la cota factorial 5040; no se optimiza el peor caso en esta fase.\n");
  return 0;
}
