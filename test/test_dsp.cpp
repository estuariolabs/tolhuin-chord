// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   test_dsp.cpp  -  Test de host del núcleo DSP (sin hardware).
   dsp.cpp es C++ puro: se renderiza a un buffer float en la PC y se verifican
   propiedades numéricas y deterministas (no requiere oído).

   Compilar/correr (lo hace test/run_tests.ps1):
     g++ -std=c++17 -I.. test_dsp.cpp ../dsp.cpp -o test_dsp
     ./test_dsp      (devuelve 0 si todo pasa, 1 si hay fallas)

   Para cada timbre (BRASS / EPIANO / STRINGS) toca el acorde C4-E4-G4, renderiza
   ~1 s y verifica:
     1) sin NaN / Inf
     2) sin clip: |x| <= 1
     3) DC ~ 0 (media cercana a cero)
     4) RMS en un rango razonable (ni silencio ni saturado)
     5) fundamentales presentes: la energía (Goertzel) en C4/E4/G4 domina
        ampliamente sobre una sonda sub-fundamental (100 Hz)
   Exporta test/out_<timbre>.wav (16-bit mono) para escucha opcional.
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>

#include "dsp.h"
#include "config.h"

static int checks = 0, failures = 0;
#define CHECK(cond, msg, ...) do { checks++; if(!(cond)) { failures++; \
  printf("FAIL: " msg "\n", ##__VA_ARGS__); } } while(0)

static const double PI_D = 3.14159265358979323846;

static float midiToFreq(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

// Seno simple a partir de fase 0..1 (para el test de vibrato).
static float fastSineLocal(float ph) { return (float)sin(2.0 * PI_D * ph); }

// Potencia de Goertzel a una frecuencia dada (normalizada por N).
static double goertzel(const std::vector<float>& x, double freq, double sr) {
  int N = (int)x.size();
  double w = 2.0 * PI_D * freq / sr;
  double coeff = 2.0 * cos(w);
  double s0 = 0, s1 = 0, s2 = 0;
  for (int i = 0; i < N; i++) {
    s0 = x[i] + coeff * s1 - s2;
    s2 = s1; s1 = s0;
  }
  double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return power / ((double)N * N);
}

// Escribe un WAV 16-bit mono.
static void writeWav(const char* path, const std::vector<float>& x, uint32_t sr) {
  FILE* f = fopen(path, "wb");
  if (!f) { printf("WARN: no pude escribir %s\n", path); return; }
  uint32_t n = (uint32_t)x.size();
  uint32_t dataBytes = n * 2;
  uint32_t fileBytes = 36 + dataBytes;
  uint16_t ch = 1, bits = 16;
  uint32_t byteRate = sr * ch * bits / 8;
  uint16_t blockAlign = ch * bits / 8;

  fwrite("RIFF", 1, 4, f); fwrite(&fileBytes, 4, 1, f); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f);
  uint32_t fmtLen = 16; uint16_t fmt = 1;
  fwrite(&fmtLen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
  fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
  for (uint32_t i = 0; i < n; i++) {
    float v = x[i]; if (v > 1) v = 1; if (v < -1) v = -1;
    int16_t s = (int16_t)(v * 32767.0f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

static void testTimbre(uint8_t timbre, const char* wavPath) {
  dspInit();                       // estado limpio por timbre
  dspNoteOn(60, timbre);           // C4
  dspNoteOn(64, timbre);           // E4
  dspNoteOn(67, timbre);           // G4

  const int N = (int)SAMPLE_RATE;  // ~1 segundo
  std::vector<float> buf;
  buf.reserve(N);

  float block[AUDIO_BLOCK];
  int rendered = 0;
  while (rendered < N) {
    dspRenderBlock(block, (int)AUDIO_BLOCK);
    for (int i = 0; i < (int)AUDIO_BLOCK && rendered < N; i++, rendered++)
      buf.push_back(block[i]);
  }

  const char* name = dspTimbreName(timbre);

  // 1) sin NaN/Inf, 2) sin clip
  bool finite = true, noClip = true;
  double sum = 0, sumSq = 0;
  for (float v : buf) {
    if (!std::isfinite(v)) finite = false;
    if (v > 1.0f || v < -1.0f) noClip = false;
    sum += v; sumSq += (double)v * v;
  }
  CHECK(finite, "[%s] hay NaN/Inf", name);
  CHECK(noClip, "[%s] hay clip (|x|>1)", name);

  // 3) DC ~ 0
  double dc = sum / buf.size();
  CHECK(fabs(dc) < 0.05, "[%s] DC fuera de rango: %.4f", name, dc);

  // 4) RMS en rango
  double rms = sqrt(sumSq / buf.size());
  CHECK(rms > 0.02 && rms < 0.8, "[%s] RMS fuera de rango: %.4f", name, rms);

  // 5) fundamentales presentes (Goertzel) vs sonda sub-fundamental
  double pC = goertzel(buf, midiToFreq(60), SAMPLE_RATE);
  double pE = goertzel(buf, midiToFreq(64), SAMPLE_RATE);
  double pG = goertzel(buf, midiToFreq(67), SAMPLE_RATE);
  double probe = goertzel(buf, 100.0, SAMPLE_RATE);   // por debajo de la fundamental
  double tones = pC + pE + pG;
  CHECK(tones > 10.0 * (probe + 1e-12),
        "[%s] fundamentales debiles: tones=%.3e probe=%.3e", name, tones, probe);

  printf("  %-7s  dc=%+.4f  rms=%.4f  C=%.2e E=%.2e G=%.2e probe=%.2e\n",
         name, dc, rms, pC, pE, pG, probe);

  writeWav(wavPath, buf, SAMPLE_RATE);
}

// Genera un buffer de un oscilador a frecuencia f0 (naive o band-limited).
static std::vector<float> genOsc(bool square, bool blep, double f0, int N) {
  std::vector<float> buf; buf.reserve(N);
  float ph = 0.0f, dt = (float)(f0 / SAMPLE_RATE);
  for (int i = 0; i < N; i++) {
    float s;
    if (square) s = blep ? dspOscSquareBlep(ph, dt) : dspOscSquareNaive(ph);
    else        s = blep ? dspOscSawBlep(ph, dt)    : dspOscSawNaive(ph);
    buf.push_back(s);
    ph += dt; if (ph >= 1.0f) ph -= 1.0f;
  }
  return buf;
}

// T1.1: a f0=7000 Hz (SR 44100) hay alias inharmónico fuerte en 4900 y 9100 Hz
// (de los armónicos plegados). PolyBLEP debe reducir esa energía vs. naive.
static void testAntiAlias() {
  const int N = (int)SAMPLE_RATE;
  const double f0 = 7000.0;
  const double aliasFreqs[2] = { 4900.0, 9100.0 };

  for (int sq = 0; sq < 2; sq++) {
    std::vector<float> naive = genOsc(sq, /*blep=*/false, f0, N);
    std::vector<float> blep  = genOsc(sq, /*blep=*/true,  f0, N);
    double aN = 0, aB = 0;
    for (double fa : aliasFreqs) {
      aN += goertzel(naive, fa, SAMPLE_RATE);
      aB += goertzel(blep,  fa, SAMPLE_RATE);
    }
    const char* w = sq ? "SQUARE" : "SAW";
    CHECK(aB < 0.5 * aN, "[%s] PolyBLEP no redujo el alias: naive=%.3e blep=%.3e",
          w, aN, aB);
    printf("  alias %-6s  naive=%.3e  blep=%.3e  (ratio %.2f)\n",
           w, aN, aB, aB / (aN + 1e-18));
  }
}

// T1.2: la envolvente ADSR debe llegar a sustain, soltar a 0 y no dar saltos
// bruscos entre muestras (sin clicks).
static void testAdsr() {
  const float sr = (float)SAMPLE_RATE;

  // --- STRINGS: A=45 D=60 S=0.80 R=420, no percusivo ---
  {
    Adsr env;
    env.config(45.0f, 60.0f, 0.80f, 420.0f, false, sr);
    env.noteOn();
    float prev = env.level, maxJump = 0.0f;
    // procesar 150 ms (supera A+D=105 ms): debe quedar en sustain
    int nAD = (int)(0.150f * sr);
    for (int i = 0; i < nAD; i++) { float v = env.process(); float d = fabsf(v - prev); if (d > maxJump) maxJump = d; prev = v; }
    CHECK(fabsf(env.level - 0.80f) < 1e-3f, "[ADSR str] no llegó a sustain: %.4f", env.level);
    CHECK(env.stage == Adsr::ST_SUS, "[ADSR str] no está en SUS tras A+D");

    // release: tras > R debe quedar en 0 e inactiva
    env.noteOff();
    int nR = (int)(0.500f * sr);
    for (int i = 0; i < nR; i++) { float v = env.process(); float d = fabsf(v - prev); if (d > maxJump) maxJump = d; prev = v; }
    CHECK(env.level == 0.0f, "[ADSR str] release no llegó a 0: %.6f", env.level);
    CHECK(!env.active(), "[ADSR str] sigue activa tras release");
    // sin clicks: ningún salto supera el mayor incremento configurado (slew rate)
    float bound = fmaxf(env.atkInc, fmaxf(env.decInc, env.relInc)) * 1.001f;
    CHECK(maxJump <= bound, "[ADSR str] discontinuidad %.5f > slew %.5f", maxJump, bound);
    printf("  ADSR STR   maxJump=%.6f slew=%.6f (sustain=0.80, release->0 OK)\n", maxJump, bound);
  }

  // --- EPIANO percusivo: A=2 D=900 S=0 perc -> decae a 0 y termina solo ---
  {
    Adsr env;
    env.config(2.0f, 900.0f, 0.0f, 150.0f, true, sr);
    env.noteOn();
    float prev = env.level, maxJump = 0.0f;
    int n = (int)(1.2f * sr);   // supera A+D
    for (int i = 0; i < n; i++) { float v = env.process(); float d = fabsf(v - prev); if (d > maxJump) maxJump = d; prev = v; }
    CHECK(env.level == 0.0f, "[ADSR ep] percusivo no llegó a 0: %.6f", env.level);
    CHECK(!env.active(), "[ADSR ep] percusivo sigue activo");
    float bound = fmaxf(env.atkInc, fmaxf(env.decInc, env.relInc)) * 1.001f;
    CHECK(maxJump <= bound, "[ADSR ep] discontinuidad %.5f > slew %.5f", maxJump, bound);
    printf("  ADSR EP    maxJump=%.6f slew=%.6f (percusivo ->0 y termina OK)\n", maxJump, bound);
  }
}

// T1.3: el filtro SVF debe atenuar agudos por encima del cutoff y mantenerse
// estable (sin desbordes) en todo el barrido de cutoff/resonancia.
static void testFilter() {
  const double sr = (double)SAMPLE_RATE;
  const int N = (int)SAMPLE_RATE;   // 1 s

  // --- atenuación: cutoff 500 Hz, una sinusoide grave pasa y una aguda se corta ---
  {
    Svf f; f.setCoeffs(500.0f, 0.707f, (float)sr);
    std::vector<float> inHi, outHi;
    double ph = 0, dt = 2.0 * PI_D * 5000.0 / sr;
    for (int i = 0; i < N; i++) { float x = (float)sin(ph); ph += dt; inHi.push_back(x); outHi.push_back(f.lp(x)); }
    double pinHi = goertzel(inHi, 5000.0, sr), poutHi = goertzel(outHi, 5000.0, sr);
    CHECK(poutHi < 0.01 * pinHi, "[SVF] no atenuó 5000 Hz: in=%.3e out=%.3e", pinHi, poutHi);

    Svf g; g.setCoeffs(500.0f, 0.707f, (float)sr);
    std::vector<float> inLo, outLo;
    ph = 0; dt = 2.0 * PI_D * 200.0 / sr;
    for (int i = 0; i < N; i++) { float x = (float)sin(ph); ph += dt; inLo.push_back(x); outLo.push_back(g.lp(x)); }
    double pinLo = goertzel(inLo, 200.0, sr), poutLo = goertzel(outLo, 200.0, sr);
    CHECK(poutLo > 0.5 * pinLo, "[SVF] atenuó de más 200 Hz: in=%.3e out=%.3e", pinLo, poutLo);
    printf("  SVF atten  5000Hz out/in=%.2e   200Hz out/in=%.2f\n",
           poutHi / (pinHi + 1e-18), poutLo / (pinLo + 1e-18));
  }

  // --- estabilidad: barrido de cutoff y resonancia con ruido blanco ---
  {
    const float cuts[6] = { 50, 200, 1000, 5000, 15000, 19000 };
    const float qs[5]   = { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f };
    uint32_t seed = 0x12345678u;
    bool allOk = true; float worst = 0.0f;
    for (float cut : cuts) for (float q : qs) {
      Svf f; f.setCoeffs(cut, q, (float)sr);
      int M = (int)(0.1 * sr);
      for (int i = 0; i < M; i++) {
        seed = seed * 1664525u + 1013904223u;
        float x = ((int)(seed >> 9) / (float)(1 << 22)) - 1.0f;  // ruido [-1,1)
        float y = f.lp(x);
        if (!std::isfinite(y)) allOk = false;
        if (fabsf(y) > worst) worst = fabsf(y);
      }
    }
    CHECK(allOk, "[SVF] inestable: salida no finita en el barrido");
    CHECK(worst < 100.0f, "[SVF] desborde en el barrido: pico=%.2f", worst);
    printf("  SVF sweep  finito=%s  pico=%.3f (30 combinaciones cutoff/Q)\n",
           allOk ? "si" : "NO", worst);
  }
}

// Renderiza N muestras del timbre `t` tocando la nota `note` (acorde de 1 nota).
static std::vector<float> renderNote(uint8_t t, uint8_t note, int N) {
  dspInit();
  dspNoteOn(note, t);
  std::vector<float> buf; buf.reserve(N);
  float block[AUDIO_BLOCK]; int done = 0;
  while (done < N) {
    dspRenderBlock(block, (int)AUDIO_BLOCK);
    for (int i = 0; i < (int)AUDIO_BLOCK && done < N; i++, done++) buf.push_back(block[i]);
  }
  return buf;
}

// T1.4: LFO con rate correcto y modulación medible en el buffer (vibrato + trémolo).
static void testLfo() {
  const float  sr  = (float)SAMPLE_RATE;
  const double srd = (double)SAMPLE_RATE;

  // --- rate del módulo Lfo: 5 Hz, medido sobre 10 s (preciso) ---
  {
    Lfo lfo; lfo.setRate(5.0f);
    float dt = 1.0f / sr;
    int seconds = 10;
    int crossings = 0; float prev = lfo.process(dt);
    bool bounded = true;
    for (int i = 1; i < seconds * (int)sr; i++) {
      float v = lfo.process(dt);
      if (v < -1.0001f || v > 1.0001f) bounded = false;
      if (prev < 0.0f && v >= 0.0f) crossings++;   // flanco ascendente = 1 período
      prev = v;
    }
    float freq = crossings / (float)seconds;
    CHECK(bounded, "[LFO] fuera de [-1,1]");
    CHECK(fabsf(freq - 5.0f) < 0.15f, "[LFO] rate medido %.2f Hz != 5.0", freq);
    printf("  LFO rate   medido=%.2f Hz (esperado 5.0)\n", freq);
  }

  // --- vibrato en el buffer: el LFO modula el pitch -> bandas laterales en
  //     f0 ± lfoRate. Se usa el mismo Lfo y el mismo patrón que renderVoice
  //     (sc = 1 + depth*lfoVal sobre el incremento de fase), a tasa de bloque. ---
  {
    const double f0 = 1000.0, fm = 5.0, depth = 0.01;   // ±1% de pitch, medible
    Lfo lfo; lfo.setRate((float)fm);
    float blockDt = (float)AUDIO_BLOCK / sr;
    int N = (int)(1.0f * sr);
    std::vector<float> buf; buf.reserve(N);
    float ph = 0.0f, lfoVal = 0.0f; int inBlock = 0;
    for (int i = 0; i < N; i++) {
      if (inBlock == 0) { lfoVal = lfo.process(blockDt); inBlock = (int)AUDIO_BLOCK; }
      inBlock--;
      float inc = (float)(f0 * (1.0 + depth * lfoVal)) / sr;
      ph += inc; if (ph >= 1.0f) ph -= 1.0f;
      buf.push_back(fastSineLocal(ph));
    }
    double pSide = goertzel(buf, f0 + fm,       srd);   // 1ra banda lateral del vibrato
    double pOff  = goertzel(buf, f0 + 3.0 * fm, srd);   // fuera de la banda
    CHECK(pSide > 4.0 * (pOff + 1e-15), "[LFO] vibrato débil: side=%.3e off=%.3e", pSide, pOff);
    printf("  vibrato    side(f0+5)=%.3e  off(f0+15)=%.3e  ratio=%.1f\n",
           pSide, pOff, pSide / (pOff + 1e-15));
  }

  // --- trémolo en el buffer: STRINGS modula la amplitud a lfoRate (5.2 Hz).
  //     Se mide sobre la envolvente (RMS por ventana) con Goertzel. ---
  {
    const float fm = 5.2f;
    int N = (int)(1.5f * sr);
    std::vector<float> buf = renderNote(T_STRINGS, 60, N);

    const int W = 256;                  // envolvente: RMS por ventana de 256 muestras
    std::vector<float> env;
    for (int i = 0; i + W <= N; i += W) {
      double s = 0; for (int j = 0; j < W; j++) { float x = buf[i+j]; s += (double)x*x; }
      env.push_back((float)sqrt(s / W));
    }
    int skip = (int)(0.4f * sr / W);    // saltar ataque + apertura de filtro
    std::vector<float> sus(env.begin() + skip, env.end());
    // profundidad de modulación
    float mn = 1e9f, mx = -1e9f;
    for (float e : sus) { if (e<mn)mn=e; if (e>mx)mx=e; }
    float modDepth = (mx - mn) / (mx + mn + 1e-9f);
    // energía de la modulación a lfoRate vs banda alta, sobre la env (sr_env=sr/W)
    double srEnv = srd / W;
    double pMod = goertzel(sus, fm,        srEnv);
    double pOff = goertzel(sus, fm + 12.0, srEnv);
    CHECK(modDepth > 0.03f, "[LFO] trémolo débil: modDepth=%.4f", modDepth);
    CHECK(pMod > 3.0 * (pOff + 1e-15), "[LFO] trémolo no está a lfoRate: mod=%.3e off=%.3e", pMod, pOff);
    printf("  trémolo    modDepth=%.3f  mod(5.2Hz)=%.3e  off(17Hz)=%.3e (STRINGS)\n",
           modDepth, pMod, pOff);
  }
}

// T2.1: el delay estéreo debe producir ecos al retardo correcto, con feedback
// que decae (estable), y salida estéreo real (L≠R).
static void testDelay() {
  const float sr = (float)SAMPLE_RATE;
  DelayStereo d; d.reset();
  d.setParams(10.0f, 0.5f, 1.0f, sr);   // 10 ms, fb 0.5, full wet (ecos visibles)
  int dL = (int)(10.0f * sr / 1000.0f); // 441 muestras a 44100

  int N = 5 * dL;
  std::vector<float> L(N), R(N);
  bool finite = true, bounded = true;
  for (int i = 0; i < N; i++) {
    float in = (i == 0) ? 1.0f : 0.0f;   // impulso
    d.process(in, L[i], R[i]);
    if (!std::isfinite(L[i]) || !std::isfinite(R[i])) finite = false;
    if (fabsf(L[i]) > 1.001f || fabsf(R[i]) > 1.001f) bounded = false;
  }

  // ecos de L en k*dL con amplitud que decae por fb
  float e1 = L[dL], e2 = L[2*dL], e3 = L[3*dL];
  CHECK(finite,  "[DELAY] salida no finita");
  CHECK(bounded, "[DELAY] desborde (|x|>1)");
  CHECK(e1 > 0.9f && e1 < 1.1f,  "[DELAY] 1er eco fuera de lugar/amplitud: %.3f", e1);
  CHECK(fabsf(e2 / e1 - 0.5f) < 0.05f, "[DELAY] feedback != 0.5: e2/e1=%.3f", e2/e1);
  CHECK(fabsf(e3 / e2 - 0.5f) < 0.05f, "[DELAY] feedback != 0.5: e3/e2=%.3f", e3/e2);
  CHECK(e3 < e2 && e2 < e1, "[DELAY] no decae monótonamente");

  // estéreo real: el canal R usa otro tiempo (3/4) -> L y R difieren en el eco
  CHECK(fabsf(L[dL] - R[dL]) > 0.3f, "[DELAY] L y R demasiado parecidos (no estéreo)");

  printf("  delay      ecos e1=%.3f e2=%.3f e3=%.3f (fb~0.5)  L!=R en dL: |%.3f-%.3f|\n",
         e1, e2, e3, L[dL], R[dL]);
}

// T2.2: la reverb debe dar una cola que decae suave (RT60 en rango), estable y
// sin NaN/clip ante un impulso.
static void testReverb() {
  const float sr = (float)SAMPLE_RATE;
  Reverb r; r.reset();
  r.setParams(0.6f, 0.5f, 1.0f);   // sala media, full wet para medir la cola

  int N = (int)(3.0f * sr);
  std::vector<float> y(N);
  bool finite = true, bounded = true;
  for (int i = 0; i < N; i++) {
    float in = (i == 0) ? 1.0f : 0.0f;
    y[i] = r.process(in);
    if (!std::isfinite(y[i])) finite = false;
    if (fabsf(y[i]) > 5.0f) bounded = false;
  }

  // envolvente RMS por ventanas de 1024 muestras
  const int W = 1024;
  std::vector<float> env; std::vector<float> tWin;
  for (int i = 0; i + W <= N; i += W) {
    double s = 0; for (int j = 0; j < W; j++) s += (double)y[i+j]*y[i+j];
    env.push_back((float)sqrt(s / W));
    tWin.push_back((i + W * 0.5f) / sr);
  }
  // pico y RT60 (caída de 60 dB en amplitud = factor 1e-3)
  int pk = 0; for (int i = 1; i < (int)env.size(); i++) if (env[i] > env[pk]) pk = i;
  float thr = env[pk] * 1e-3f;
  float rt60 = -1.0f;
  for (int i = pk + 1; i < (int)env.size(); i++) if (env[i] < thr) { rt60 = tWin[i] - tWin[pk]; break; }

  // tendencia decreciente: energía a ~2 s menor que a ~0.5 s
  auto envAt = [&](float t){ int idx=(int)(t*sr/W); if(idx>=(int)env.size())idx=env.size()-1; return env[idx]; };
  bool decae = envAt(2.0f) < envAt(0.5f);

  CHECK(finite,  "[REVERB] salida no finita");
  CHECK(bounded, "[REVERB] inestable (|x|>5)");
  CHECK(decae,   "[REVERB] la cola no decae (0.5s vs 2s)");
  CHECK(rt60 > 0.2f && rt60 < 4.0f, "[REVERB] RT60 fuera de rango: %.2f s", rt60);
  printf("  reverb     RT60=%.2f s  pico=%.3f  env@0.5s=%.4f env@2s=%.4f\n",
         rt60, env[pk], envAt(0.5f), envAt(2.0f));
}

// T3.1: STRUM. El plan ordena las notas ascendente y las espacia por el retardo;
// el scheduler en vivo dispara los note-on en ese orden y a esos tiempos.
static void testStrum() {
  const float sr = (float)SAMPLE_RATE;
  uint8_t notes[3] = { 67, 60, 64 };   // G, C, E desordenadas

  // --- plan puro: orden ascendente + espaciado por el retardo ---
  {
    StrumPlan p = dspMakeStrum(notes, 3, 30.0f, sr);
    int step = (int)(30.0f * sr / 1000.0f);
    CHECK(p.count == 3, "[STRUM] count != 3");
    CHECK(p.note[0] == 60 && p.note[1] == 64 && p.note[2] == 67, "[STRUM] no ordenó ascendente");
    CHECK(p.atSample[0] == 0 && p.atSample[1] == step && p.atSample[2] == 2*step,
          "[STRUM] espaciado incorrecto: %d %d %d", p.atSample[0], p.atSample[1], p.atSample[2]);
    printf("  plan       notas %d<%d<%d  paso=%d muestras\n", p.note[0], p.note[1], p.note[2], step);
  }

  // --- scheduler en vivo: las voces se activan de a una, espaciadas ~30 ms ---
  {
    const float strumMs = 30.0f;
    int step = (int)(strumMs * sr / 1000.0f);
    dspInit();
    dspStrumChord(notes, 3, T_BRASS, strumMs, sr);

    std::vector<int> fireAt;
    int prev = 0, sample = 0;
    float block[AUDIO_BLOCK];
    for (int b = 0; b < 40 && (int)fireAt.size() < 3; b++) {
      dspRenderBlock(block, (int)AUDIO_BLOCK);
      sample += (int)AUDIO_BLOCK;
      int c = dspActiveVoices();
      while (c > prev) { fireAt.push_back(sample); prev++; }
    }
    CHECK(fireAt.size() == 3, "[STRUM] no se dispararon 3 notas (fueron %d)", (int)fireAt.size());
    if (fireAt.size() == 3) {
      int d1 = fireAt[1] - fireAt[0], d2 = fireAt[2] - fireAt[1];
      CHECK(abs(d1 - step) <= (int)AUDIO_BLOCK, "[STRUM] separación 1->2 %d != ~%d", d1, step);
      CHECK(abs(d2 - step) <= (int)AUDIO_BLOCK, "[STRUM] separación 2->3 %d != ~%d", d2, step);
      printf("  en vivo    disparos en %d/%d/%d muestras (paso ~%d)\n",
             fireAt[0], fireAt[1], fireAt[2], step);
    }
  }
}

// T3.2: arpegiador. Orden de patrón correcto y notas en las subdivisiones del BPM.
static void testArp() {
  const float sr = (float)SAMPLE_RATE;

  // --- orden de patrones (puro), acorde de 3 notas ---
  {
    int up[6], dn[6], ud[6];
    for (int s = 0; s < 6; s++) {
      up[s] = dspArpStepIndex(s, 3, ARP_UP);
      dn[s] = dspArpStepIndex(s, 3, ARP_DOWN);
      ud[s] = dspArpStepIndex(s, 3, ARP_UPDOWN);
    }
    int expUp[6] = {0,1,2,0,1,2};
    int expDn[6] = {2,1,0,2,1,0};
    int expUd[6] = {0,1,2,1,0,1};
    bool okUp=true, okDn=true, okUd=true;
    for (int s=0;s<6;s++){ if(up[s]!=expUp[s])okUp=false; if(dn[s]!=expDn[s])okDn=false; if(ud[s]!=expUd[s])okUd=false; }
    CHECK(okUp, "[ARP] patrón UP incorrecto");
    CHECK(okDn, "[ARP] patrón DOWN incorrecto");
    CHECK(okUd, "[ARP] patrón UPDOWN incorrecto");

    // Estilos nuevos con acorde de 4 notas (índices 0..3).
    int expDU[6] = { 3, 2, 1, 0, 1, 2 };   // DOWNUP: baja hasta 0 y sube (sin repetir extremos)
    int expCv[4] = { 0, 3, 1, 2 };         // CONVERGE: afuera->adentro
    int expDv[4] = { 2, 1, 3, 0 };         // DIVERGE: adentro->afuera
    bool okDU=true, okCv=true, okDv=true;
    for (int s=0;s<6;s++) if (dspArpStepIndex(s,4,ARP_DOWNUP)  != expDU[s]) okDU=false;
    for (int s=0;s<4;s++) if (dspArpStepIndex(s,4,ARP_CONVERGE)!= expCv[s]) okCv=false;
    for (int s=0;s<4;s++) if (dspArpStepIndex(s,4,ARP_DIVERGE) != expDv[s]) okDv=false;
    CHECK(okDU, "[ARP] patrón DOWNUP incorrecto");
    CHECK(okCv, "[ARP] patrón CONVERGE incorrecto");
    CHECK(okDv, "[ARP] patrón DIVERGE incorrecto");
    // Todos los índices caen en rango [0, n) para cualquier estilo y paso.
    bool inRange = true;
    for (int st = 0; st < ARP_COUNT; st++)
      for (int nn = 1; nn <= 8; nn++)
        for (int s = 0; s < 40; s++) {
          int idx = dspArpStepIndex(s, nn, (ArpPattern)st);
          if (idx < 0 || idx >= nn) inRange = false;
        }
    CHECK(inRange, "[ARP] un estilo devolvió índice fuera de rango");
    printf("  patrones   UP/DOWN/UPDOWN/DOWNUP/CONVERGE/DIVERGE OK, índices en rango\n");
  }

  // --- GATE: staccato (gate corto) deja MENOS energía en la cola del paso que
  //   legato (gate=1). Se compara la misma ventana con ambos gates. ---
  {
    uint8_t notes[3] = { 60, 64, 67 };
    auto tailRms = [&](float gate) {
      dspInit();
      dspArpStart(notes, 3, T_SINE, 120.0f, 2, ARP_UP, gate, sr);   // paso ~11025
      std::vector<float> buf; float block[AUDIO_BLOCK];
      for (int b = 0; b < 40; b++) { dspRenderBlock(block, (int)AUDIO_BLOCK);
        for (int i = 0; i < (int)AUDIO_BLOCK; i++) buf.push_back(block[i]); }
      double e = 0; int a = 8000, z = 10000;      // cola del paso, antes del próximo
      for (int i = a; i < z; i++) e += (double)buf[i] * buf[i];
      return sqrt(e / (z - a));
    };
    double legato = tailRms(1.0f), stacc = tailRms(0.2f);
    CHECK(stacc < legato * 0.7, "[ARP] el gate no acorta la nota (legato=%.3f staccato=%.3f)", legato, stacc);
    printf("  gate       cola legato=%.3f > staccato=%.3f (gate corta)\n", legato, stacc);
    dspInit();
  }

  // --- en vivo: 120 BPM, corcheas (2/pulso) -> paso 0.25 s; patrón UP ---
  {
    uint8_t notes[3] = { 60, 64, 67 };
    int stepsPerBeat = 2;
    float bpm = 120.0f;
    int step = (int)(60.0f / bpm * sr / stepsPerBeat);   // 11025 muestras

    dspInit();
    dspArpStart(notes, 3, T_BRASS, bpm, stepsPerBeat, ARP_UP, 1.0f, sr);

    std::vector<int> at; std::vector<int> seq;
    int prevNote = -1, sample = 0;
    float block[AUDIO_BLOCK];
    for (int b = 0; b < 280 && (int)seq.size() < 6; b++) {
      dspRenderBlock(block, (int)AUDIO_BLOCK);
      sample += (int)AUDIO_BLOCK;
      int ln = dspArpLastNote();
      if (ln != prevNote) { at.push_back(sample); seq.push_back(ln); prevNote = ln; }
    }
    CHECK(seq.size() == 6, "[ARP] no avanzaron 6 pasos (fueron %d)", (int)seq.size());
    if (seq.size() == 6) {
      bool ord = seq[0]==60 && seq[1]==64 && seq[2]==67 && seq[3]==60 && seq[4]==64 && seq[5]==67;
      CHECK(ord, "[ARP] orden de notas UP incorrecto");
      bool timing = true;
      for (int i = 1; i < 6; i++) if (abs((at[i]-at[i-1]) - step) > (int)AUDIO_BLOCK) timing = false;
      CHECK(timing, "[ARP] tiempos fuera de la subdivisión del BPM");
      printf("  en vivo    notas %d,%d,%d,%d,%d,%d  paso~%d muestras (corcheas@120)\n",
             seq[0],seq[1],seq[2],seq[3],seq[4],seq[5], step);
    }
  }
}

// Correlación de Pearson entre dos canales (sobre una región).
static double correl(const std::vector<float>& a, const std::vector<float>& b, int from) {
  double sa=0, sb=0; int n=0;
  for (int i=from;i<(int)a.size();i++){ sa+=a[i]; sb+=b[i]; n++; }
  double ma=sa/n, mb=sb/n, num=0, da=0, db=0;
  for (int i=from;i<(int)a.size();i++){ double x=a[i]-ma, y=b[i]-mb; num+=x*y; da+=x*x; db+=y*y; }
  return num / (sqrt(da*db) + 1e-18);
}

// T3.4: el ancho estéreo de las cuerdas decorrelaciona L y R; los timbres mono
// dan L==R. Se mide sobre el render estéreo SECO (sin delay, que ya decorrelaciona).
static void testStereoWidth() {
  const float sr = (float)SAMPLE_RATE;
  int N = (int)(1.0f * sr);
  int skip = (int)(0.4f * sr);   // saltar ataque + apertura de filtro

  auto renderStereoDry = [&](uint8_t t, std::vector<float>& L, std::vector<float>& R){
    dspInit(); dspNoteOn(60, t);
    L.clear(); R.clear(); L.reserve(N); R.reserve(N);
    float bl[AUDIO_BLOCK], br[AUDIO_BLOCK]; int done=0;
    while (done < N) {
      dspRenderStereoDry(bl, br, (int)AUDIO_BLOCK);
      for (int i=0;i<(int)AUDIO_BLOCK && done<N;i++,done++){ L.push_back(bl[i]); R.push_back(br[i]); }
    }
  };

  std::vector<float> sL, sR, bL, bR;
  renderStereoDry(T_STRINGS, sL, sR);
  renderStereoDry(T_BRASS,   bL, bR);

  double corrStr   = correl(sL, sR, skip);
  double corrBrass = correl(bL, bR, skip);

  CHECK(corrStr < 0.98, "[STEREO] cuerdas no decorrelacionan (corr=%.4f)", corrStr);
  CHECK(corrBrass > 0.999, "[STEREO] brass debería ser mono (corr=%.4f)", corrBrass);
  printf("  ancho      STRINGS corr(L,R)=%.3f (ancho)   BRASS corr=%.4f (mono)\n",
         corrStr, corrBrass);
}

// T5.5: cambiar el acorde durante el arp (p.ej. al mover el joystick) NO debe
// dejar notas colgadas: el arp toca de a una y la nota previa se apaga sola.
static void testArpNoStuck() {
  const float sr = (float)SAMPLE_RATE;
  dspInit();
  float blk[AUDIO_BLOCK];
  uint8_t chords[6][4] = {
    {60,64,67,0}, {60,64,67,62}, {60,65,69,0},
    {62,65,69,72}, {59,62,67,0}, {60,64,67,71}
  };
  uint8_t counts[6] = { 3, 4, 3, 4, 3, 4 };
  int peak = 0;
  for (int k = 0; k < 6; k++) {
    dspArpStart(chords[k], counts[k], T_BRASS, 120.0f, 2, ARP_UP, 1.0f, sr);   // re-feed (color)
    int N = (int)(0.35f * sr), done = 0;     // ≥ un paso (0.25 s)
    while (done < N) {
      dspRenderBlock(blk, (int)AUDIO_BLOCK);
      done += (int)AUDIO_BLOCK;
      int a = dspActiveVoices(); if (a > peak) peak = a;
    }
  }
  CHECK(peak <= 3, "[ARP] notas colgadas al cambiar de acorde (pico voces=%d)", peak);
  printf("  arp resync pico de voces tras 6 cambios = %d (<=3 ok)\n", peak);
  dspArpStop();
}

// T6.3: humanización a nivel DSP — micro-retardo de arranque y ganancia por voz.
static void testHumanize() {
  const float sr = (float)SAMPLE_RATE;
  float blk[AUDIO_BLOCK];

  // --- micro-retardo: la voz está en silencio hasta que vence el retardo ---
  {
    dspInit();
    int delay = 2000;
    dspNoteOn(60, T_SINE, 1.0f, delay);
    // primeros 1536 muestras (< 2000): debe estar en silencio
    float pre = 0; int n1 = 0;
    for (int b = 0; b < 6; b++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) { float a = fabsf(blk[i]); if (a > pre) pre = a; n1++; } }
    // luego suena
    double sq = 0; int n2 = 0;
    for (int b = 0; b < 20; b++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) { sq += (double)blk[i]*blk[i]; n2++; } }
    double rms = sqrt(sq / n2);
    CHECK(pre < 1e-4f, "[HUMAN] sonó durante el retardo (pico=%.5f)", pre);
    CHECK(rms > 0.01,  "[HUMAN] no sonó tras el retardo (rms=%.4f)", rms);
    printf("  retardo    silencio_previo=%.5f  rms_post=%.4f\n", pre, rms);
  }

  // --- ganancia por voz: vel 0.5 da ~la mitad de RMS que vel 1.0 ---
  {
    auto rmsOf = [&](float vel){
      dspInit(); dspNoteOn(60, T_SINE, vel, 0);
      for (int b = 0; b < 8; b++) dspRenderBlock(blk, (int)AUDIO_BLOCK);  // pasar ataque
      double sq = 0; int n = 0;
      for (int b = 0; b < 20; b++){ dspRenderBlock(blk,(int)AUDIO_BLOCK);
        for (int i=0;i<(int)AUDIO_BLOCK;i++){ sq += (double)blk[i]*blk[i]; n++; } }
      return sqrt(sq / n);
    };
    double full = rmsOf(1.0f), half = rmsOf(0.5f);
    CHECK(fabs(half / full - 0.5) < 0.1, "[HUMAN] vel no escala lineal: full=%.4f half=%.4f", full, half);
    printf("  ganancia   rms(1.0)=%.4f  rms(0.5)=%.4f  ratio=%.2f\n", full, half, half/full);
  }
}

// T6.5: un ACORDE (no una sola nota) debe abrirse en estéreo: las voces se
// panean a posiciones distintas -> L y R llevan mezclas distintas -> corr << 1.
static void testChordWidth() {
  const float sr = (float)SAMPLE_RATE;
  int N = (int)(1.0f * sr), skip = (int)(0.3f * sr);
  uint8_t chord[4] = { 48, 52, 55, 59 };   // C3 E3 G3 B3

  for (int t = 0; t < 2; t++) {            // BRASS (mono->paneo) y ORGAN
    uint8_t timbre = (t == 0) ? T_BRASS : T_ORGAN;
    dspInit();
    for (int i = 0; i < 4; i++) dspNoteOn(chord[i], timbre);
    std::vector<float> L, R; L.reserve(N); R.reserve(N);
    float bl[AUDIO_BLOCK], br[AUDIO_BLOCK]; int done = 0;
    while (done < N) {
      dspRenderStereoDry(bl, br, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK && done < N; i++, done++) { L.push_back(bl[i]); R.push_back(br[i]); }
    }
    double c = correl(L, R, skip);
    const char* nm = (t == 0) ? "BRASS" : "ORGAN";
    CHECK(c < 0.9, "[WIDTH] el acorde %s no se abrió en estéreo (corr=%.3f)", nm, c);
    printf("  acorde %-5s corr(L,R)=%.3f  (referencia ~0.0; 1.0=mono)\n", nm, c);
  }
}

// T7.x: modo mono (una sola voz, legato) y glide (el pitch desliza, no salta).
static void testMonoGlide() {
  const float sr = (float)SAMPLE_RATE;
  float blk[AUDIO_BLOCK];
  auto render = [&](double secs){ int N=(int)(secs*sr),d=0; while(d<N){ dspRenderBlock(blk,(int)AUDIO_BLOCK); d+=(int)AUDIO_BLOCK; } };

  // --- mono = 1 voz: tras dos notas legato sigue habiendo una sola voz ---
  {
    dspInit(); dspSetGlide(false, 100);
    dspMonoNote(57, T_SINE); render(0.10);
    dspMonoNote(69, T_SINE); render(0.10);
    CHECK(dspActiveVoices() == 1, "[MONO] deberia haber 1 voz, hay %d", dspActiveVoices());
    printf("  mono       voces activas = %d\n", dspActiveVoices());
  }

  // --- glide: justo tras saltar 57->69, ¿la energía está cerca del origen (desliza)
  //     o ya en destino (salta)? ---
  auto capture = [&](bool glide){
    dspInit(); dspSetGlide(glide, 100);
    dspMonoNote(57, T_SINE); render(0.25);        // asienta en A3 (220 Hz)
    dspMonoNote(69, T_SINE);                       // objetivo A4 (440 Hz)
    std::vector<float> buf; int N=(int)(0.03*sr);  // 30 ms inmediatos
    int d=0; while(d<N){ dspRenderBlock(blk,(int)AUDIO_BLOCK); for(int i=0;i<(int)AUDIO_BLOCK&&d<N;i++,d++) buf.push_back(blk[i]); }
    double p220=goertzel(buf,220.0,sr), p440=goertzel(buf,440.0,sr);
    return std::pair<double,double>(p220,p440);
  };
  auto off = capture(false);
  auto on  = capture(true);
  CHECK(off.second > off.first, "[GLIDE] sin glide deberia saltar a 440 (p440=%.2e p220=%.2e)", off.second, off.first);
  CHECK(on.first  > on.second, "[GLIDE] con glide deberia seguir cerca de 220 (p220=%.2e p440=%.2e)", on.first, on.second);
  printf("  glide off  p220=%.2e p440=%.2e (salta)   glide on  p220=%.2e p440=%.2e (desliza)\n",
         off.first, off.second, on.first, on.second);
}

// El glide poly entre acordes: las voces se deslizan (legato), no se re-disparan.
static void testPolyGlide() {
  const float sr = (float)SAMPLE_RATE;
  float blk[AUDIO_BLOCK];
  auto render = [&](double secs){ int N=(int)(secs*sr),d=0; while(d<N){ dspRenderBlock(blk,(int)AUDIO_BLOCK); d+=(int)AUDIO_BLOCK; } };

  dspInit(); dspSetGlide(true, 150);
  uint8_t A[3] = { 48, 52, 55 };          // C3 E3 G3
  uint8_t B[3] = { 55, 59, 62 };          // G3 B3 D4 (sube ~una quinta)
  for (int i = 0; i < 3; i++) dspNoteOn(A[i], T_SINE);
  render(0.30);
  int before = dspActiveVoices();

  dspGlideChord(B, 3, T_SINE);             // cambio de acorde con glide
  // 30 ms inmediatos: si desliza, la energía sigue cerca del acorde VIEJO
  std::vector<float> buf; int N=(int)(0.03*sr), d=0;
  while (d<N){ dspRenderBlock(blk,(int)AUDIO_BLOCK); for(int i=0;i<(int)AUDIO_BLOCK&&d<N;i++,d++) buf.push_back(blk[i]); }
  int after = dspActiveVoices();
  double pOld = goertzel(buf, midiToFreq(48), sr);   // raíz vieja (C3)
  double pNew = goertzel(buf, midiToFreq(62), sr);   // nota más alta nueva (D4)

  CHECK(before == 3 && after == 3, "[POLYGLIDE] cambió la cantidad de voces (%d->%d)", before, after);
  CHECK(pOld > pNew, "[POLYGLIDE] no deslizó: ya saltó al acorde nuevo (old=%.2e new=%.2e)", pOld, pNew);
  printf("  poly glide voces %d->%d   energia vieja=%.2e > nueva=%.2e (desliza)\n",
         before, after, pOld, pNew);

  // Caso clave: glide ON y PRIMER acorde (sin voces previas) -> debe SONAR (no
  // silenciarse). Captura del bug "al encender glide se silencia todo".
  {
    dspInit(); dspSetGlide(true, 150);
    uint8_t C[3] = { 48, 52, 55 };
    dspGlideChord(C, 3, T_SINE);          // sin voces previas -> todas frescas
    render(0.20);
    double sq = 0; int M = 0;
    for (int b = 0; b < 20; b++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) { sq += (double)blk[i]*blk[i]; M++; } }
    double rms = sqrt(sq / M);
    CHECK(dspActiveVoices() == 3, "[POLYGLIDE] primer acorde no sostiene 3 voces (%d)", dspActiveVoices());
    CHECK(rms > 0.02, "[POLYGLIDE] primer acorde con glide se silenció (rms=%.4f)", rms);
    printf("  1er acorde voces=%d rms=%.4f (suena con glide on)\n", dspActiveVoices(), rms);
  }
}

// Delay editable: setear tiempo y sincronizar a tempo dan el retardo esperado.
static void testDelayEdit() {
  dspInit();
  dspDelaySet(120.0f, 0.4f, 0.3f);
  float t1 = dspDelayTimeMs();
  CHECK(fabs(t1 - 120.0) < 5.0, "[DELAYEDIT] setTime 120ms -> %.1f", t1);

  // sync 1/8 a 120 BPM = 250 ms (negra=500ms)
  dspDelaySyncBpm(120.0f, 0.5f);
  float t2 = dspDelayTimeMs();
  CHECK(fabs(t2 - 250.0) < 5.0, "[DELAYEDIT] sync 1/8@120 -> %.1f (esperado 250)", t2);

  // sync 1/4 a 150 BPM = 400 ms (clamp al buffer ~400)
  dspDelaySyncBpm(150.0f, 1.0f);
  float t3 = dspDelayTimeMs();
  CHECK(t3 > 350.0 && t3 <= 400.0, "[DELAYEDIT] sync 1/4@150 -> %.1f", t3);
  printf("  delay edit setTime=%.0f  1/8@120=%.0f  1/4@150=%.0f ms\n", t1, t2, t3);
}

// Sustain conmutable: al soltar, el release largo hace que la nota siga sonando
// donde con el release corto del timbre ya estaría en silencio.
static void testSustain() {
  const float sr = (float)SAMPLE_RATE;
  float blk[AUDIO_BLOCK];
  const uint8_t T = T_SINE;   // timbre sostenido (relMs corto = 300 ms)

  // RMS de una ventana corta tras soltar y dejar pasar `releaseSecs` de release.
  auto rmsAfterRelease = [&](bool sustainOn, double releaseSecs){
    dspInit();
    if (sustainOn) dspSetSustain(true, 1500.0f);   // release largo
    dspNoteOn(60, T);
    int settle = (int)(0.5 * sr), d = 0;            // dejar llegar al sustain
    while (d < settle) { dspRenderBlock(blk, (int)AUDIO_BLOCK); d += (int)AUDIO_BLOCK; }
    dspNoteOff(60);
    int skip = (int)(releaseSecs * sr); d = 0;       // avanzar dentro del release
    while (d < skip) { dspRenderBlock(blk, (int)AUDIO_BLOCK); d += (int)AUDIO_BLOCK; }
    double sq = 0; int n = 0;
    for (int b = 0; b < 20; b++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) { sq += (double)blk[i]*blk[i]; n++; } }
    return sqrt(sq / n);
  };

  double dry = rmsAfterRelease(false, 0.5);   // release 300 ms -> a 0.5 s ya callado
  double sus = rmsAfterRelease(true,  0.5);   // release 1500 ms -> a 0.5 s aún resonando
  CHECK(dry < 0.01,       "[SUS] sin sustain debería estar callado a 0.5s (rms=%.4f)", dry);
  CHECK(sus > 0.02,       "[SUS] con sustain debería seguir sonando a 0.5s (rms=%.4f)", sus);
  CHECK(sus > dry + 0.02, "[SUS] el sustain no alargó el release (dry=%.4f sus=%.4f)", dry, sus);
  printf("  release    rms@0.5s sin=%.4f  con=%.4f\n", dry, sus);
}

// Generate Random Sound: randomizar cambia el sonido; resetear lo restaura idéntico.
static void testRandomSound() {
  float blk[AUDIO_BLOCK];
  auto renderSig = [&]()->std::vector<float>{
    dspNoteOn(60, T_SINE);                       // T_SINE es determinista (sin ruido de aliento)
    std::vector<float> b;
    for (int k = 0; k < 40; k++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) b.push_back(blk[i]); }
    return b;
  };
  auto diffRms = [](std::vector<float>& x, std::vector<float>& y){
    double sq = 0; size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; i++) { double d = x[i]-y[i]; sq += d*d; }
    return sqrt(sq / n);
  };
  dspInit();                                  auto base  = renderSig();
  dspInit(); dspRandomizeSound(T_SINE, 12345); auto rand1 = renderSig();
  dspInit(); dspRandomizeSound(T_SINE, 12345); dspResetSound(T_SINE); auto reset = renderSig();
  double dRand = diffRms(base, rand1), dReset = diffRms(base, reset);
  CHECK(dRand > 0.01,   "[RANDOM] randomizar no cambió el sonido (diff=%.4f)", dRand);
  CHECK(dReset < 1e-6,  "[RANDOM] resetear no restauró el timbre (diff=%.6f)", dReset);
  printf("  random     diff(base,rand)=%.4f  diff(base,reset)=%.6f\n", dRand, dReset);
}

// Percusión: el bombo es grave y decae; la caja decae y tiene más agudos (ruido).
static void testDrums() {
  // --- KICK (motor SYNTH: los checks espectrales son del sintetizado) ---
  dspInit();
  dspDrumsSetEngine(false);
  dspTriggerKick();
  std::vector<float> k;
  for (int i = 0; i < 13230; i++) k.push_back(dspDrumTick());   // 0.3 s
  bool fin = true, noclip = true;
  for (float v : k) { if (!std::isfinite(v)) fin = false; if (v > 1.0f || v < -1.0f) noclip = false; }
  double ke0 = 0, ke1 = 0;
  for (int i = 0;     i < 2000;  i++) ke0 += (double)k[i] * k[i];
  for (int i = 11000; i < 13000; i++) ke1 += (double)k[i] * k[i];
  double kLo = goertzel(k, 60.0, SAMPLE_RATE), kHi = goertzel(k, 3000.0, SAMPLE_RATE);
  CHECK(fin && noclip, "[DRUM] kick NaN/clip");
  CHECK(ke0 > ke1 * 5.0, "[DRUM] el kick no decae (e0=%.1f e1=%.1f)", ke0, ke1);
  CHECK(kLo > kHi * 20.0, "[DRUM] el kick no es grave (60Hz=%.2e 3kHz=%.2e)", kLo, kHi);

  // --- SNARE ---
  dspInit();
  dspDrumsSetEngine(false);
  dspTriggerSnare();
  std::vector<float> s;
  for (int i = 0; i < 8820; i++) s.push_back(dspDrumTick());    // 0.2 s
  fin = true; noclip = true;
  for (float v : s) { if (!std::isfinite(v)) fin = false; if (v > 1.0f || v < -1.0f) noclip = false; }
  double se0 = 0, se1 = 0;
  for (int i = 0;    i < 1500; i++) se0 += (double)s[i] * s[i];
  for (int i = 7000; i < 8800; i++) se1 += (double)s[i] * s[i];
  double sHi = goertzel(s, 3000.0, SAMPLE_RATE);
  CHECK(fin && noclip, "[DRUM] snare NaN/clip");
  CHECK(se0 > se1 * 5.0, "[DRUM] la caja no decae (e0=%.1f e1=%.1f)", se0, se1);
  CHECK(sHi > kHi, "[DRUM] la caja no tiene más agudos que el kick (%.2e vs %.2e)", sHi, kHi);
  printf("  drums      kick grave/agudo=%.0fx ; caja agudos > kick (%.1ex)\n", kLo / (kHi + 1e-12), sHi / (kHi + 1e-12));

  // --- HI-HAT: corto, decae y es MUY agudo (ruido pasa-altos ~6.5 kHz) ---
  dspInit();
  dspDrumsSetEngine(false);
  dspTriggerHat();
  std::vector<float> h;
  for (int i = 0; i < 6615; i++) h.push_back(dspDrumTick());    // 0.15 s
  fin = true; noclip = true;
  for (float v : h) { if (!std::isfinite(v)) fin = false; if (v > 1.0f || v < -1.0f) noclip = false; }
  double he0 = 0, he1 = 0;
  for (int i = 0;    i < 1000; i++) he0 += (double)h[i] * h[i];
  for (int i = 5000; i < 6600; i++) he1 += (double)h[i] * h[i];
  double hLo = goertzel(h, 200.0, SAMPLE_RATE), hHi = goertzel(h, 9000.0, SAMPLE_RATE);
  CHECK(fin && noclip, "[DRUM] hat NaN/clip");
  CHECK(he0 > he1 * 5.0, "[DRUM] el hat no decae (e0=%.3f e1=%.3f)", he0, he1);
  CHECK(hHi > hLo * 5.0, "[DRUM] el hat no es agudo (9kHz=%.2e 200Hz=%.2e)", hHi, hLo);

  // --- VOLUMEN GENERAL de la batería: escala lineal en la mezcla estéreo ---
  auto drumRms = [&](float gain) {
    dspInit();
    dspDrumsSetGain(gain);
    dspTriggerKick();
    static float L[256], R[256];
    double e = 0; int n = 0;
    for (int b = 0; b < 20; b++) {
      dspRenderStereo(L, R, 256);
      for (int i = 0; i < 256; i++) { e += (double)L[i] * L[i]; n++; }
    }
    return sqrt(e / n);
  };
  double r1 = drumRms(1.0f), r4 = drumRms(0.25f);
  CHECK(r1 > 1e-4, "[DRUM] con gain 1.0 no suena (rms=%.2e)", r1);
  CHECK(fabs(r4 / r1 - 0.25) < 0.05, "[DRUM] gain 0.25 no escala (ratio=%.3f)", r4 / r1);
  printf("  hat        9k/200Hz=%.0fx (agudo, decae) ; drum gain 0.25 -> ratio=%.3f\n",
         hHi / (hLo + 1e-12), r4 / r1);

  // --- Motor de SAMPLES (drum_samples.h): suena, no revienta y termina solo ---
  dspInit();
  dspDrumsSetEngine(true);
  CHECK(dspDrumsEngine(), "[DRUM] el motor samples no quedo activo");
  dspTriggerKick(); dspTriggerSnare(); dspTriggerHat();
  std::vector<float> m;
  for (int i = 0; i < (int)SAMPLE_RATE; i++) m.push_back(dspDrumTick());  // 1 s > sample más largo
  fin = true; noclip = true;
  double me = 0;
  for (float v : m) { if (!std::isfinite(v)) fin = false; if (v > 1.0f || v < -1.0f) noclip = false; me += (double)v * v; }
  me = sqrt(me / m.size());
  CHECK(fin && noclip, "[DRUM] samples NaN/clip (el soft-clip del bus debe limitar a +-1)");
  CHECK(me > 0.01, "[DRUM] los samples no suenan (rms=%.4f)", me);
  double tail = 0;
  for (int i = (int)SAMPLE_RATE - 11025; i < (int)SAMPLE_RATE; i++) tail += (double)m[i] * m[i];
  CHECK(tail < 1e-6, "[DRUM] un sample no terminó solo (cola=%.2e)", tail);
  printf("  samples    kick+snare+hat rms=%.4f, terminan solos (cola=%.1e)\n", me, tail);
  dspInit();
}

// Looper de 4 capas: grabación + wrap-around + reproducción en loop, suma de
// 4 capas con soft-clip (sin clip duro) y overdub.
static void testLooper() {
  static int16_t b0[2000], b1[2000], b2[2000], b3[2000];
  int16_t* bufs[LOOPER_LAYERS] = { b0, b1, b2, b3 };
  looperInit(bufs, 2000);

  // grabar un seno conocido en la capa 0 (500 muestras) -> define el maestro
  looperRecordToggle(0);
  for (int i = 0; i < 500; i++) looperTick(0.5f * (float)sin(2.0 * PI_D * i / 50.0));
  looperRecordToggle(0);                          // stop -> master=500, capa0 PLAY
  CHECK(looperMasterLen() == 500, "[LOOPER] master=%u (esperaba 500)", looperMasterLen());
  CHECK(looperLayerState(0) == 2, "[LOOPER] capa0 no quedó en PLAY (%d)", looperLayerState(0));

  // reproducción: se repite con período = master (wrap-around correcto)
  std::vector<float> out;
  for (int i = 0; i < 1000; i++) out.push_back(looperTick(0.0f));
  bool loops = true, fin = true, noclip = true;
  for (int i = 0; i < 400; i++) if (fabs(out[i] - out[i + 500]) > 2e-3) loops = false;
  for (float v : out) { if (!std::isfinite(v)) fin = false; if (v > 1.0f || v < -1.0f) noclip = false; }
  CHECK(loops, "[LOOPER] no se repite con período = master");
  CHECK(fin && noclip, "[LOOPER] NaN o clip en reproducción");

  // 4 capas a casi full no deben clipear (soft-clip tanh en la suma)
  looperClearAll();
  CHECK(looperMasterLen() == 0, "[LOOPER] clearAll no reseteó el maestro");
  looperRecordToggle(0);
  for (int i = 0; i < 200; i++) looperTick(0.95f);
  looperRecordToggle(0);                          // master=200
  for (int L = 1; L < LOOPER_LAYERS; L++) {
    looperRecordToggle(L);                         // graba un loop completo -> PLAY al wrap
    for (int i = 0; i < 200; i++) looperTick(0.95f);
  }
  float pk = 0;
  for (int i = 0; i < 400; i++) { float v = looperTick(0.0f); if (fabs(v) > pk) pk = fabs(v); }
  CHECK(pk <= 1.0f, "[LOOPER] 4 capas clipean (pico %.3f)", pk);

  // overdub: PLAY -> OVERDUB
  looperRecordToggle(0);
  CHECK(looperLayerState(0) == 3, "[LOOPER] no entró en OVERDUB (%d)", looperLayerState(0));
  printf("  looper     master=500, loop=%d, 4capas pico=%.3f (soft-clip)\n", loops, pk);
  looperClearAll();
}

// Sync externo del arp: con ExtSync el arp NO avanza solo; sólo dspArpAdvance()
// dispara pasos (uno por tick externo), siguiendo el patrón.
static void testArpExtSync() {
  dspInit();
  uint8_t chord[3] = { 60, 64, 67 };
  dspArpStart(chord, 3, T_SINE, 120.0f, 2, ARP_UP, 1.0f, (float)SAMPLE_RATE);
  dspArpSetExtSync(true);                          // desconecta el reloj interno
  float blk[AUDIO_BLOCK];
  // ~1 s sin avanzar: no debe disparar ningún paso (queda en -1)
  for (int b = 0; b < 170; b++) dspRenderBlock(blk, (int)AUDIO_BLOCK);
  CHECK(dspArpLastNote() == -1, "[ARPSYNC] avanzó sin tick externo (nota=%d)", dspArpLastNote());
  // avanzar 4 pasos a mano -> patrón UP sobre {60,64,67}: 60,64,67,60
  int expected[4] = { 60, 64, 67, 60 };
  bool ok = true;
  for (int s = 0; s < 4; s++) {
    dspArpAdvance();
    dspRenderBlock(blk, (int)AUDIO_BLOCK);
    if (dspArpLastNote() != expected[s]) ok = false;
  }
  CHECK(ok, "[ARPSYNC] la secuencia por ticks no siguió el patrón UP");
  printf("  arp sync   sin tick: sin pasos; 4 ticks -> 60,64,67,60 (%s)\n", ok ? "ok" : "MAL");
}

// ----------------------------------------------------------------------------
// SECUENCIADOR DE PASOS (ARP_SEQ): cada paso es índice de nota / silencio /
// ligadura. Se avanza a mano (sync externo) y se verifica la nota disparada:
// índice -> nota del acorde ; REST -> calla (-1) ; TIE -> sostiene la anterior.
// Además: wrap del patrón y transposición sobre otro acorde.
// ----------------------------------------------------------------------------
static void testArpStepSeq() {
  dspInit();
  uint8_t chord[3] = { 60, 64, 67 };                     // índices 0,1,2
  uint8_t seq[6] = { 0, 2, ARP_STEP_REST, 1, ARP_STEP_TIE, 1 };
  dspArpSetActiveSeq(seq, 6);
  dspArpStart(chord, 3, T_SINE, 120.0f, 4, ARP_SEQ, 1.0f, (float)SAMPLE_RATE);
  dspArpSetExtSync(true);                                // avanzo yo, paso a paso
  float blk[AUDIO_BLOCK];
  int expect[7] = { 60, 67, -1, 64, 64, 64, 60 };        // idx0,idx2,REST,idx1,TIE,idx1,wrap->idx0
  bool ok = true;
  for (int s = 0; s < 7; s++) {
    dspArpAdvance();
    dspRenderBlock(blk, (int)AUDIO_BLOCK);
    if (dspArpLastNote() != expect[s]) { ok = false;
      printf("    paso %d: nota=%d (esperaba %d)\n", s, dspArpLastNote(), expect[s]); }
  }
  CHECK(ok, "[ARPSEQ] la secuencia idx/rest/tie/wrap no coincidió");

  // Transponible: MISMO patrón sobre otro acorde -> idx0 = la nueva raíz (62).
  uint8_t chord2[3] = { 62, 65, 69 };
  dspArpStop();
  dspArpSetActiveSeq(seq, 6);
  dspArpStart(chord2, 3, T_SINE, 120.0f, 4, ARP_SEQ, 1.0f, (float)SAMPLE_RATE);
  dspArpSetExtSync(true);
  dspArpAdvance(); dspRenderBlock(blk, (int)AUDIO_BLOCK);
  CHECK(dspArpLastNote() == 62, "[ARPSEQ] no transpuso (idx0 != 62, dio %d)", dspArpLastNote());
  printf("  arpseq     60,67,-,64,64(tie),64,60 (wrap) ; transpone a otro acorde -> 62\n");
  dspInit();
}

// Robo de voz con SUSTAIN: cambiar de acorde con colas largas fuerza el robo;
// no debe haber NaN/clip ni saltos bruscos (clicks/crack) en la salida.
static void testVoiceSteal() {
  const uint8_t timbres[2] = { T_BRASS, T_FLUTE };
  const char*   names[2]   = { "BRASS", "FLUTE" };
  for (int ti = 0; ti < 2; ti++) {
    dspInit();
    dspSetSustain(true, 1500.0f);                 // colas largas -> se acumulan voces
    float blk[AUDIO_BLOCK];
    float prev = 0; bool first = true; double maxJump = 0; bool finite = true, noClip = true;
    for (int chord = 0; chord < 8; chord++) {      // 8 "acordes" seguidos (más voces que MAX_VOICES)
      for (int k = 0; k < 4; k++) dspNoteOn((uint8_t)(48 + chord + k * 4), timbres[ti]);
      for (int b = 0; b < 8; b++) {                // ~46 ms entre acordes
        dspRenderBlock(blk, (int)AUDIO_BLOCK);
        for (int i = 0; i < (int)AUDIO_BLOCK; i++) {
          float v = blk[i];
          if (!std::isfinite(v)) finite = false;
          if (v > 1.0f || v < -1.0f) noClip = false;
          if (!first) { double j = fabs(v - prev); if (j > maxJump) maxJump = j; }
          prev = v; first = false;
        }
      }
    }
    CHECK(finite, "[STEAL] %s: hay NaN/Inf al robar voces", names[ti]);
    CHECK(noClip, "[STEAL] %s: clip (|x|>1) al robar voces", names[ti]);
    CHECK(maxJump < 0.5, "[STEAL] %s: salto brusco (click/crack) al robar voz: %.3f", names[ti], maxJump);
    printf("  steal %-5s maxJump=%.4f (sin click si <0.5)\n", names[ti], maxJump);
  }
}

// BODY: la capa de cuerpo/unísono (detune+drift+transitorio) cambia el sonido;
// apagada no toca la afinación (render idéntico al de fábrica, determinista).
static void testBody() {
  float blk[AUDIO_BLOCK];
  auto renderSig = [&]()->std::vector<float>{
    dspNoteOn(60, T_SINE);
    std::vector<float> b;
    for (int k = 0; k < 40; k++) { dspRenderBlock(blk, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) b.push_back(blk[i]); }
    return b;
  };
  auto diffRms = [](std::vector<float>& x, std::vector<float>& y){
    double sq = 0; size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; i++) { double d = x[i]-y[i]; sq += d*d; }
    return sqrt(sq / n);
  };
  dspInit();                        auto off  = renderSig();
  dspInit(); dspSetBody(true, 1.0f); auto on   = renderSig();
  dspInit();                        auto off2 = renderSig();
  double dBody = diffRms(off, on), dOff = diffRms(off, off2);
  CHECK(dBody > 0.01, "[BODY] BODY no cambió el sonido (diff=%.4f)", dBody);
  CHECK(dOff < 1e-6,  "[BODY] BODY off no es determinista (diff=%.6f)", dOff);
  printf("  body       diff(off,on)=%.4f  diff(off,off)=%.6f\n", dBody, dOff);
}

// Chorus estéreo: con el chorus activo, L y R se decorrelacionan (ancho); apagado
// (y sin delay) una nota mono queda L≈R. Aísla el chorus poniendo el delay en 0.
static void testChorus() {
  const float sr = (float)SAMPLE_RATE;
  int N = (int)(1.5f * sr), skip = (int)(0.4f * sr);
  auto corrOf = [&](bool on)->double{
    dspInit();
    dspDelaySet(200.0f, 0.0f, 0.0f);             // neutraliza el delay (aísla el chorus)
    if (on) dspSetChorus(true, 0.8f, 7.0f, 0.5f);
    dspNoteOn(60, T_SINE);
    std::vector<float> L, R; L.reserve(N); R.reserve(N);
    float bl[AUDIO_BLOCK], br[AUDIO_BLOCK]; int done = 0;
    while (done < N) {
      dspRenderStereo(bl, br, (int)AUDIO_BLOCK);
      for (int i = 0; i < (int)AUDIO_BLOCK && done < N; i++, done++) { L.push_back(bl[i]); R.push_back(br[i]); }
    }
    return correl(L, R, skip);
  };
  double off = corrOf(false), on = corrOf(true);
  // El motor ya panea por voz, así que "off" no es 1.0; el test es RELATIVO:
  // el chorus debe ensanchar (bajar la correlación) de forma clara.
  CHECK(on < off - 0.1, "[CHORUS] el chorus no ensanchó la imagen (off=%.3f on=%.3f)", off, on);
  printf("  chorus     corr(L,R) off=%.3f on=%.3f (mas bajo = mas ancho)\n", off, on);
}

// Tremolo global: con el tremolo activo la amplitud de salida debe oscilar a la
// tasa fijada (rateHz), y plana cuando está apagado.
static void testTremolo() {
  const float sr = (float)SAMPLE_RATE;
  const float rate = 6.0f, depth = 0.6f;
  float L[AUDIO_BLOCK], R[AUDIO_BLOCK];

  // Envolvente (RMS por bloque) del mono de salida durante `secs`, con el tremolo
  // configurado por el caller (on/off).
  auto envOf = [&](bool on)->std::vector<float>{
    dspInit();
    if (on) dspSetTremolo(true, rate, depth);
    dspNoteOn(60, T_SINE);
    std::vector<float> env;
    int blocks = (int)(1.2 * sr / AUDIO_BLOCK);
    for (int b = 0; b < blocks; b++) {
      dspRenderStereo(L, R, (int)AUDIO_BLOCK);
      double sq = 0;
      for (int i = 0; i < (int)AUDIO_BLOCK; i++) { float m = 0.5f*(L[i]+R[i]); sq += (double)m*m; }
      env.push_back((float)sqrt(sq / AUDIO_BLOCK));
    }
    return env;
  };

  std::vector<float> on = envOf(true), off = envOf(false);
  double envSr = sr / AUDIO_BLOCK;                 // tasa de muestreo de la envolvente
  // Goertzel de la envolvente (quitando su media) a la tasa del tremolo vs una banda lejana.
  auto modAt = [&](std::vector<float>& e, double f){
    double mean = 0; for (float v : e) mean += v; mean /= e.size();
    std::vector<float> ac; for (float v : e) ac.push_back(v - (float)mean);
    return goertzel(ac, f, envSr);
  };
  double onMod = modAt(on, rate), offMod = modAt(off, rate), onFar = modAt(on, 1.0);
  CHECK(onMod > 20.0 * (offMod + 1e-12), "[TREM] sin modulación a %.0f Hz (on=%.3e off=%.3e)", rate, onMod, offMod);
  CHECK(onMod > 5.0 * (onFar + 1e-12),   "[TREM] la modulación no está a la tasa fijada (on=%.3e far=%.3e)", onMod, onFar);
  printf("  tremolo    mod@%.0fHz on=%.3e off=%.3e (lejos=%.3e)\n", rate, onMod, offMod, onFar);
}

// ----------------------------------------------------------------------------
// Edición de timbres por USB (web-config): get/set/factory y semántica de los
// tres niveles (vivo / preset del usuario / fábrica).
// ----------------------------------------------------------------------------
static void testEnvEdit() {
  dspInit();
  EnvCfg fab = dspEnvFactory(T_BRASS);
  EnvCfg e   = dspEnvGet(T_BRASS);
  CHECK(fabs(e.atkMs - fab.atkMs) < 1e-4f, "[ENV] tras init el vivo != fabrica");

  e.atkMs = 123.0f; e.cutHz = 1234.0f;
  dspEnvSet(T_BRASS, e);
  EnvCfg r = dspEnvGet(T_BRASS);
  CHECK(fabs(r.atkMs - 123.0f) < 1e-4f && fabs(r.cutHz - 1234.0f) < 1e-2f,
        "[ENV] set/get no hace roundtrip (atk=%.1f cut=%.1f)", r.atkMs, r.cutHz);

  // El reset (cambio de timbre / deshacer un random) restaura el PRESET DEL
  // USUARIO, no la fábrica: la edición web sobrevive a cambiar de timbre.
  dspRandomizeSound(T_BRASS, 42);
  dspResetSound(T_BRASS);
  r = dspEnvGet(T_BRASS);
  CHECK(fabs(r.atkMs - 123.0f) < 1e-4f, "[ENV] el reset pierde la edicion del usuario");
  CHECK(fabs(dspEnvFactory(T_BRASS).atkMs - fab.atkMs) < 1e-4f, "[ENV] la fabrica se ensucio");
  printf("  env       edit atk=123 sobrevive reset; fabrica atk=%.1f intacta\n", fab.atkMs);

  dspEnvSet(T_BRASS, fab);   // deja el timbre limpio para los demas tests
}

// ----------------------------------------------------------------------------
// Patrones de batería editables (slots de usuario 4..7) y que suenan al render.
// ----------------------------------------------------------------------------
static void testDrumPatternEdit() {
  dspInit();
  uint32_t k = 1, s = 1, h = 1;
  dspDrumPatternGet(4, &k, &s, &h);
  CHECK(k == 0 && s == 0 && h == 0, "[DRUMPAT] el slot de usuario no nace vacio (k=%u s=%u h=%u)", k, s, h);

  // Máscaras de 16 pasos (ejercita el bit 15 y el canal de hat).
  dspDrumPatternSet(4, 0x8001, 0x0008, 0x4444);
  dspDrumPatternGet(4, &k, &s, &h);
  CHECK(k == 0x8001 && s == 0x0008 && h == 0x4444,
        "[DRUMPAT] set/get no hace roundtrip (k=0x%04X s=0x%04X h=0x%04X)", k, s, h);

  dspDrumPatternSet(0, 0xFFFF, 0xFFFF, 0xFFFF);   // el slot 0 es "off": no editable
  dspDrumPatternGet(0, &k, &s, &h);
  CHECK(k == 0 && s == 0 && h == 0, "[DRUMPAT] el slot 0 (off) se dejo editar");
  dspDrumPatternSet(4, 0x8001, 0x0008, 0x4444);   // restaura el slot 4 para el render

  // El patrón de usuario suena: render de medio segundo con el slot 4 activo.
  dspAllOff();
  dspDrumsSet(4, 240.0f);
  std::vector<float> L(SAMPLE_RATE / 2), R(SAMPLE_RATE / 2);
  dspRenderStereo(L.data(), R.data(), (int)L.size());
  double rms = 0; for (float v : L) rms += (double)v * v;
  rms = sqrt(rms / L.size());
  CHECK(rms > 0.005, "[DRUMPAT] el patron de usuario no suena (rms=%.4f)", rms);

  // Timing de la grilla: hat SOLO en el paso 0, a 240 bpm -> semicorchea = 2756
  // muestras, compás de 16 pasos ≈ 1.0 s. Renderizado por bloques de 256 (como
  // el firmware). Debe sonar al inicio, callar a mitad de compás y volver a
  // sonar tras el wrap: verifica el largo del paso Y el ciclo de 16.
  dspInit();
  dspDrumPatternSet(4, 0, 0, 0x0001);
  dspDrumsSet(4, 240.0f);
  std::vector<float> tl;
  {
    float bl[256], br[256];
    for (int b = 0; b < (2 * (int)SAMPLE_RATE) / 256; b++) {
      dspRenderStereo(bl, br, 256);
      for (int i = 0; i < 256; i++) tl.push_back(bl[i]);
    }
  }
  auto energy = [&](int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)tl[i] * tl[i]; return e; };
  double eHit = energy(0, 4000), eMid = energy(20000, 40000), eWrap = energy(44096, 48096);
  CHECK(eHit  > eMid * 20.0, "[DRUMPAT] el golpe del paso 0 no se distingue (hit=%.5f mid=%.6f)", eHit, eMid);
  CHECK(eWrap > eMid * 20.0, "[DRUMPAT] no repite tras el wrap de 16 pasos (wrap=%.5f mid=%.6f)", eWrap, eMid);
  printf("  drumpat   slot4 (16 pasos) k=0x8001 s=0x0008 h=0x4444 rms=%.4f ; wrap 16 ok\n", rms);
  dspDrumsSet(0, 120.0f);

  // GRILLA TERNARIA: con div=3 el paso dura 60/bpm/3 (no /4). Hat SOLO en el
  // paso 1: a 240 bpm cae en la muestra 3675 (ternario) y NO en la 2756
  // (binario). Verifica que la subdivisión cambia el reloj de verdad.
  dspInit();
  dspDrumPatternSet(5, 0, 0, 0x0002);            // hat en el paso 1
  dspDrumPatternCfgSet(5, 1, 3);                 // 1 compás ternario = 12 pasos
  {
    int spb = 0, tot = 0;
    dspDrumsSet(5, 240.0f);
    dspDrumsGridInfo(&spb, &tot);
    CHECK(spb == 12 && tot == 12, "[DRUMGRID] grilla ternaria mal (spb=%d tot=%d)", spb, tot);
    dspDrumPatternCfgSet(5, 2, 3);               // 2 compases -> 24 pasos (en vivo)
    dspDrumsGridInfo(&spb, &tot);
    CHECK(tot == 24, "[DRUMGRID] 2 compases ternarios != 24 pasos (tot=%d)", tot);
    dspDrumPatternCfgSet(5, 1, 3);
    dspDrumsSet(5, 240.0f);
  }
  std::vector<float> t3;
  {
    float bl[256], br[256];
    for (int b = 0; b < (int)SAMPLE_RATE / 256; b++) {
      dspRenderStereo(bl, br, 256);
      for (int i = 0; i < 256; i++) t3.push_back(bl[i]);
    }
  }
  auto e3 = [&](int a, int b) { double e = 0; for (int i = a; i < b; i++) e += (double)t3[i] * t3[i]; return e; };
  double eTern = e3(3675, 3675 + 1200);          // ventana del golpe ternario
  double eBin  = e3(2300, 2300 + 1200);          // donde caería el binario (antes del ternario)
  CHECK(eTern > eBin * 5.0, "[DRUMGRID] el paso ternario no cae donde debe (tern=%.5f bin=%.6f)", eTern, eBin);
  printf("  drumgrid  div=3: paso 1 en m.3675 (no 2756), 2 compases = 24 pasos\n");
  dspDrumsSet(0, 120.0f);
}

// Tablas de nombres COMPLETAS: un inicializador de menos deja NULLs y el
// printf("%s", NULL) del #hello reinicia la placa (bug real del fw-3.0).
static void testNames() {
  bool ok = true;
  for (int t = 0; t < T_COUNT; t++) {
    const char* n = dspTimbreName((uint8_t)t);
    if (!n || !n[0]) { ok = false; CHECK(false, "[NAMES] timbre %d sin nombre (NULL/vacio)", t); }
  }
  for (int p = 0; p < DRUM_PATTERNS; p++) {
    const char* n = dspPresetNameGet(0, p);
    if (!n || !n[0]) { ok = false; CHECK(false, "[NAMES] patron %d sin nombre", p); }
  }
  for (int s = 0; s < ARP_SLOTS; s++) {
    const char* n = dspPresetNameGet(1, s);
    if (!n || !n[0]) { ok = false; CHECK(false, "[NAMES] arp %d sin nombre", s); }
  }
  for (int r = 0; r < ARP_RATES; r++) {
    const char* n = dspArpRateName((uint8_t)r);
    if (!n || !n[0]) { ok = false; CHECK(false, "[NAMES] rate %d sin nombre", r); }
  }
  CHECK(ok, "[NAMES] hay tablas de nombres incompletas");
  if (ok) printf("  names      %d timbres + %d patrones + %d arps + %d rates: todos con nombre\n",
                 T_COUNT, DRUM_PATTERNS, ARP_SLOTS, ARP_RATES);
}

// Matching HiChord: un BAJO sine (raíz -1 oct) da MUCHO más sub-bajo que el mismo
// bajo con la voz saw del acorde -> es la razón del "bass role" dedicado. Y el
// STRINGS recalibrado es brillante (energía en agudos, no sólo fundamental).
static void testHiChordBass() {
  auto renderChordBass = [&](uint8_t bassTimbre) {
    dspInit();
    int chord[] = { 48, 55, 60, 63, 67, 70 };            // Cm9 desplegado (STRINGS)
    for (int nt : chord) dspNoteOn((uint8_t)nt, T_STRINGS, 0.85f, 0);
    dspNoteOn(36, bassTimbre, 1.0f, 0);                  // C2 con la voz de bajo
    std::vector<float> mono; float L[256], R[256];
    for (int b = 0; b < (int)SAMPLE_RATE / 256; b++) {  // ~1 s
      dspRenderStereo(L, R, 256);
      for (int i = 0; i < 256; i++) mono.push_back(0.5f * (L[i] + R[i]));
    }
    return mono;
  };
  std::vector<float> sineB = renderChordBass(T_SINE);
  std::vector<float> sawB  = renderChordBass(T_STRINGS);
  // Sin clip (el soft-clip del bus debe limitar aun con el sub a tope).
  bool noclip = true; for (float v : sineB) if (v > 1.0f || v < -1.0f) noclip = false;
  CHECK(noclip, "[HICHORD] el acorde + sub sine clipea");
  double subSine = goertzel(sineB, 65.4, SAMPLE_RATE);  // C2
  double subSaw  = goertzel(sawB,  65.4, SAMPLE_RATE);
  CHECK(subSine > subSaw * 2.0, "[HICHORD] el sub sine no domina al saw (sine=%.2e saw=%.2e)", subSine, subSaw);
  // STRINGS brillante: energía en 2 kHz comparable a la fundamental de una voz aguda.
  double hi = goertzel(sineB, 2093.0, SAMPLE_RATE);     // ~C7, agudos del ensemble
  CHECK(hi > 1e-9, "[HICHORD] STRINGS quedó sin agudos (2kHz=%.2e)", hi);
  printf("  hichord    sub sine/saw=%.1fx, agudos 2kHz presente, sin clip\n", subSine / (subSaw + 1e-15));
  dspInit();
}

// Timbres de USUARIO (T_USER1/2): suenan, y cambiar la FAMILIA de oscilador
// cambia de verdad el motor que renderiza (misma nota, señal distinta).
static void testUserTimbre() {
  auto render = [&](uint8_t fam, std::vector<float>& out) {
    dspInit();
    EnvCfg e = dspEnvGet(T_USER1);
    e.family = fam;
    dspEnvSet(T_USER1, e);
    dspNoteOn(60, T_USER1, 1.0f, 0);
    out.assign(43 * 256, 0.0f);                       // ~0.25 s en bloques de 256
    for (size_t i = 0; i < out.size(); i += 256) dspRenderBlock(&out[i], 256);
  };
  std::vector<float> a, b;
  render(T_SINE, a);
  render(T_BRASS, b);
  double ra = 0, rb = 0, d = 0;
  bool fin = true;
  for (size_t i = 0; i < a.size(); i++) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i])) fin = false;
    ra += (double)a[i] * a[i]; rb += (double)b[i] * b[i];
    d  += fabs((double)a[i] - (double)b[i]);
  }
  ra = sqrt(ra / a.size()); rb = sqrt(rb / b.size()); d /= a.size();
  CHECK(fin, "[USR] NaN en el timbre de usuario");
  CHECK(ra > 0.01 && rb > 0.01, "[USR] el slot de usuario no suena (rms %.4f / %.4f)", ra, rb);
  CHECK(d > 0.005, "[USR] cambiar la familia no cambia el sonido (diff=%.5f)", d);
  printf("  usr        USR1 suena (rms sine=%.3f brass=%.3f), familia cambia el motor (diff=%.4f)\n", ra, rb, d);
  dspInit();
}

// ----------------------------------------------------------------------------
// RELOJ MAESTRO: fase única del compás. barSamples sigue la fórmula del BPM, la
// posición avanza por bloque y envuelve en el "1". Es la base del phase-lock.
// ----------------------------------------------------------------------------
static void testMasterClock() {
  dspInit();                                       // arranca a 120 BPM
  uint32_t bs120 = dspClockBarSamples();
  CHECK(bs120 == (uint32_t)(60.0f/120.0f*SAMPLE_RATE*4.0f),
        "[CLK] barSamples@120 = %u (esperaba %u)", bs120, (uint32_t)(60.0f/120.0f*SAMPLE_RATE*4.0f));

  dspClockSet(140.0f);
  uint32_t bs140 = dspClockBarSamples();
  CHECK(bs140 == (uint32_t)(60.0f/140.0f*SAMPLE_RATE*4.0f),
        "[CLK] barSamples@140 = %u", bs140);
  CHECK(bs140 < bs120, "[CLK] subir el BPM no acortó el compás (%u >= %u)", bs140, bs120);

  // La posición avanza AUDIO_BLOCK por bloque y envuelve una sola vez por compás.
  dspClockReset();
  CHECK(dspClockPos() == 0, "[CLK] reset no dejó la fase en 0 (%u)", dspClockPos());
  float blk[AUDIO_BLOCK];
  dspRenderBlock(blk, (int)AUDIO_BLOCK);
  CHECK(dspClockPos() == (uint32_t)AUDIO_BLOCK,
        "[CLK] tras 1 bloque pos=%u (esperaba %u)", dspClockPos(), (uint32_t)AUDIO_BLOCK);
  // Renderiza justo más de un compás y verifica que la fase envolvió (< barSamples).
  int blocks = (int)(bs140 / AUDIO_BLOCK) + 2;
  for (int b = 0; b < blocks; b++) dspRenderBlock(blk, (int)AUDIO_BLOCK);
  CHECK(dspClockPos() < bs140, "[CLK] la fase no envolvió en el compás (pos=%u bar=%u)", dspClockPos(), bs140);
  printf("  clock      bar@120=%u bar@140=%u, pos avanza %d/bloque y envuelve en el '1'\n",
         bs120, bs140, (int)AUDIO_BLOCK);
}

// ----------------------------------------------------------------------------
// PHASE-LOCK de la batería: el golpe NO se dispara al encender, sino en el "1"
// del reloj maestro. Se enciende un patrón con kick SÓLO en el paso 0 a mitad de
// compás y se verifica que el kick recién suena al cruzar el downbeat siguiente.
// ----------------------------------------------------------------------------
static void testDrumClockLock() {
  dspInit();
  dspDrumPatternSet(4, 0x0001, 0, 0);              // slot 4: kick sólo en el paso 0
  uint32_t bar = dspClockBarSamples();             // 88200 @120
  float L[AUDIO_BLOCK], R[AUDIO_BLOCK];

  // Avanza ~medio compás con la batería APAGADA (la fase del reloj corre igual).
  int halfBlocks = (int)((bar / 2) / AUDIO_BLOCK);
  for (int b = 0; b < halfBlocks; b++) dspRenderStereo(L, R, (int)AUDIO_BLOCK);
  uint32_t posAtEnable = dspClockPos();
  CHECK(posAtEnable > bar / 4 && posAtEnable < bar,
        "[DRUMLOCK] no quedamos a mitad de compás (pos=%u bar=%u)", posAtEnable, bar);

  dspDrumsSet(4, 120.0f);                           // ENCIENDE: reinicia el reloj al "1" (stop->play)
  CHECK(dspClockPos() == 0, "[DRUMLOCK] encender no reinició el reloj (pos=%u)", dspClockPos());

  // Con el reinicio, el kick del paso 0 cae JUSTO al arrancar (no a mitad de compás):
  // la ventana inicial suena fuerte; a mitad de compás (sin kick) queda callada; y
  // en el wrap del compás vuelve el kick del paso 0.
  std::vector<float> sig;
  int spanBlocks = ((int)bar + (int)(bar / 4)) / AUDIO_BLOCK;
  for (int b = 0; b < spanBlocks; b++) {
    dspRenderStereo(L, R, (int)AUDIO_BLOCK);
    for (int i = 0; i < (int)AUDIO_BLOCK; i++) sig.push_back(0.5f*(L[i]+R[i]));
  }
  auto energy = [&](int a, int b){ double e=0; a=a<0?0:a; b=b>(int)sig.size()?(int)sig.size():b;
                                   for (int i=a;i<b;i++) e+=(double)sig[i]*sig[i]; return e; };
  double eStart = energy(0, 4000);                                   // kick del paso 0, al arrancar
  double eMid   = energy((int)(bar/2) - 2000, (int)(bar/2) + 2000);  // mitad de compás, sin kick
  double eWrap  = energy((int)bar - 500, (int)bar + 6000);           // wrap: vuelve el kick del paso 0
  CHECK(eStart > eMid * 20.0 + 1e-9,
        "[DRUMLOCK] el kick no arrancó en el paso 0 (start=%.5f mid=%.6f)", eStart, eMid);
  CHECK(eWrap > eMid * 20.0 + 1e-9,
        "[DRUMLOCK] no repite en el wrap del compás (wrap=%.5f mid=%.6f)", eWrap, eMid);
  printf("  drumlock   stop->play reinicia el reloj: kick en el paso 0 (start=%.4f >> mid=%.6f)\n",
         eStart, eMid);
  dspDrumsSet(0, 120.0f);
  dspDrumPatternSet(4, 0, 0, 0);
}

// ----------------------------------------------------------------------------
// LOOPER cuantizado (opt-in): la capa maestra arranca y termina en el "1", así
// su largo queda en compases enteros (± un bloque de resolución).
// ----------------------------------------------------------------------------
static void testLooperQuantize() {
  static int16_t q0[70000], q1[70000], q2[70000], q3[70000];
  int16_t* bufs[LOOPER_LAYERS] = { q0, q1, q2, q3 };
  dspInit();
  looperInit(bufs, 70000);
  dspLooperQuantize(true);
  CHECK(dspLooperQuantizeOn(), "[LPQ] no quedó activo el modo cuantizado");
  dspClockSet(400.0f);                              // compás corto (~26460 muestras) para el test
  uint32_t bar = dspClockBarSamples();
  dspNoteOn(60, T_SINE);                            // algo que grabar

  float L[AUDIO_BLOCK], R[AUDIO_BLOCK];
  auto renderBars = [&](double bars){ int n=(int)(bars*bar), d=0;
    while (d < n) { dspRenderStereo(L, R, (int)AUDIO_BLOCK); d += (int)AUDIO_BLOCK; } };

  looperRecordToggle(0);                            // ARMA (espera el próximo "1")
  CHECK(looperMasterLen() == 0, "[LPQ] la grabación arrancó antes del '1'");
  renderBars(2.4);                                  // cruza ≥1 downbeat -> empieza a grabar, ~2 compases
  CHECK(looperLayerState(0) == 1, "[LPQ] la capa maestra no está grabando (%d)", looperLayerState(0));  // 1 = LP_REC
  looperRecordToggle(0);                            // ARMA el fin (se aplica en el próximo "1")
  renderBars(1.2);                                  // deja que cruce el downbeat de cierre

  uint32_t master = looperMasterLen();
  CHECK(master > 0, "[LPQ] no se definió el maestro");
  int k = (int)((master + bar/2) / bar);            // compases redondeados
  int err = abs((int)master - k * (int)bar);
  CHECK(k >= 1, "[LPQ] el maestro no llegó a un compás (master=%u bar=%u)", master, bar);
  CHECK(err <= (int)AUDIO_BLOCK,
        "[LPQ] el largo no es múltiplo de compás (master=%u = %d bares?, err=%d > %d)",
        master, k, err, (int)AUDIO_BLOCK);
  printf("  loopquant  master=%u ≈ %d compás(es) de %u (err=%d ≤ %d muestras)\n",
         master, k, bar, err, (int)AUDIO_BLOCK);
  dspLooperQuantize(false);
  looperClearAll();
  dspInit();
}

// ----------------------------------------------------------------------------
// SYNC IN del reloj maestro: dspClockSyncTicks mapea el tick del clock externo
// (96/compás 4/4) a la fase exacta del reloj, y realinea aunque el reloj haya
// avanzado por su cuenta (disciplina la fase a un clock MIDI entrante).
// ----------------------------------------------------------------------------
static void testClockExtSync() {
  dspInit();
  dspClockSet(120.0f);
  uint32_t bar = dspClockBarSamples();               // 88200

  dspClockSyncTicks(0);
  CHECK(dspClockPos() == 0, "[CLKSYNC] tick 0 no cayó en el '1' (pos=%u)", dspClockPos());
  dspClockSyncTicks(48);                              // medio compás
  CHECK(dspClockPos() == bar / 2, "[CLKSYNC] tick 48 pos=%u (esperaba %u)", dspClockPos(), bar/2);
  dspClockSyncTicks(24);                              // un pulso
  CHECK(dspClockPos() == bar / 4, "[CLKSYNC] tick 24 pos=%u (esperaba %u)", dspClockPos(), bar/4);
  dspClockSyncTicks(96);                              // envuelve -> el '1'
  CHECK(dspClockPos() == 0, "[CLKSYNC] tick 96 (wrap) no volvió al '1' (pos=%u)", dspClockPos());

  // Aunque el reloj interno avance, el sync lo vuelve a clavar a la fase del tick.
  float blk[AUDIO_BLOCK];
  dspClockSyncTicks(0);
  for (int b = 0; b < 5; b++) dspRenderBlock(blk, (int)AUDIO_BLOCK);   // corre libre un rato
  CHECK(dspClockPos() > 0, "[CLKSYNC] el reloj no avanzó solo entre ticks");
  dspClockSyncTicks(0);                               // llega el tick del '1'
  CHECK(dspClockPos() == 0, "[CLKSYNC] el tick no realineó la fase (pos=%u)", dspClockPos());
  printf("  clksync    tick->fase: 0,24,48,96 -> 0,%u,%u,0 ; realinea tras correr libre\n", bar/4, bar/2);
  dspInit();
}

// WAVE (wavetable morphing): suena, sin NaN, y el morph cambia el ESPECTRO de
// verdad (con wtMorph=0 el frame oscuro casi no tiene 5to armónico; con
// wtMorph=1 la env de filtro barre a los frames brillantes y aparece).
static void testWaveMorph() {
  auto render = [&](float morph, std::vector<float>& out) {
    dspInit();
    EnvCfg e = dspEnvGet(T_WAVE);
    e.wtMorph = morph;
    e.cutHz = 8000.0f; e.fEnvOct = 0.0f;              // filtro abierto: medir el OSC
    dspEnvSet(T_WAVE, e);
    dspNoteOn(60, T_WAVE, 1.0f, 0);
    out.assign(86 * 256, 0.0f);                       // ~0.5 s
    for (size_t i = 0; i < out.size(); i += 256) dspRenderBlock(&out[i], 256);
  };
  std::vector<float> dark, bright;
  render(0.0f, dark);
  render(1.0f, bright);
  bool fin = true;
  double rms = 0;
  for (size_t i = 0; i < bright.size(); i++) {
    if (!std::isfinite(bright[i]) || !std::isfinite(dark[i])) fin = false;
    rms += (double)bright[i] * bright[i];
  }
  rms = sqrt(rms / bright.size());
  CHECK(fin, "[WAVE] NaN en el render");
  CHECK(rms > 0.01, "[WAVE] no suena (rms=%.4f)", rms);
  double f0 = 261.63;                                  // C4
  double h5d = goertzel(dark,   f0 * 5.0, SAMPLE_RATE);
  double h5b = goertzel(bright, f0 * 5.0, SAMPLE_RATE);
  CHECK(h5b > h5d * 3.0, "[WAVE] el morph no abre el espectro (H5 %.2e -> %.2e)", h5d, h5b);
  printf("  wave       suena (rms=%.3f), morph abre H5 %.1fx\n", rms, h5b / (h5d + 1e-15));
  dspInit();
}

// GRUPOS DE VOZ: el glide poliacorde NO debe tocar la melodía (VG_MEL), y un
// note-off de otra capa con la misma altura no debe matarla (bug real: las
// capas del looper se "chocaban" en el glide).
static void testVoiceGroups() {
  dspInit();
  dspSetGlide(true, 100.0f);
  dspNoteOn(72, T_SINE, 1.0f, 0, VG_MEL);            // melodía: C5 (523.25 Hz)
  uint8_t ch1[3] = { 48, 52, 55 };
  for (uint8_t nn : ch1) dspNoteOn(nn, T_SINE);      // acorde (VG_CHORD)
  std::vector<float> buf(SAMPLE_RATE / 4);
  dspRenderBlock(buf.data(), (int)buf.size());
  uint8_t ch2[3] = { 50, 53, 57 };
  dspGlideChord(ch2, 3, T_SINE);                     // re-conduce SOLO el acorde
  buf.assign(SAMPLE_RATE / 2, 0.0f);
  dspRenderBlock(buf.data(), (int)buf.size());
  double mel = goertzel(buf, 523.25, SAMPLE_RATE);
  CHECK(mel > 1e-6, "[VG] el glide de acordes mato la melodia (523Hz=%.2e)", mel);
  dspNoteOff(72, VG_CHORD);                          // off de OTRA capa: no la toca
  buf.assign(SAMPLE_RATE / 4, 0.0f);
  dspRenderBlock(buf.data(), (int)buf.size());
  double mel2 = goertzel(buf, 523.25, SAMPLE_RATE);
  CHECK(mel2 > 1e-6, "[VG] un off de la capa ACORDE mato la melodia (%.2e)", mel2);
  dspNoteOff(72, VG_MEL);                            // off de SU capa: sí la suelta
  std::vector<float> tail(SAMPLE_RATE);
  dspRenderBlock(tail.data(), (int)tail.size());
  std::vector<float> last(tail.end() - SAMPLE_RATE / 4, tail.end());
  double mel3 = goertzel(last, 523.25, SAMPLE_RATE);
  CHECK(mel3 < mel2 * 0.05, "[VG] el off de la capa MEL no la solto (%.2e vs %.2e)", mel3, mel2);
  printf("  vgroups    glide/off por capa: melodia intacta (%.1e), off propio la suelta\n", mel);
  dspInit();
}

int main(int argc, char** argv) {
  // Directorio de salida de los .wav (argv[1]); por defecto el dir actual.
  const char* dir = (argc > 1) ? argv[1] : ".";
  char pBrass[512], pEpiano[512], pStrings[512];
  snprintf(pBrass,   sizeof(pBrass),   "%s/out_brass.wav",   dir);
  snprintf(pEpiano,  sizeof(pEpiano),  "%s/out_epiano.wav",  dir);
  snprintf(pStrings, sizeof(pStrings), "%s/out_strings.wav", dir);

  char pSine[512], pTri[512], pOrgan[512], pFlute[512];
  snprintf(pSine,  sizeof(pSine),  "%s/out_sine.wav",  dir);
  snprintf(pTri,   sizeof(pTri),   "%s/out_tri.wav",   dir);
  snprintf(pOrgan, sizeof(pOrgan), "%s/out_organ.wav", dir);
  snprintf(pFlute, sizeof(pFlute), "%s/out_flute.wav", dir);

  printf("[test_dsp] render por timbre (C4-E4-G4, ~1 s):\n");
  testTimbre(T_BRASS,    pBrass);
  testTimbre(T_EPIANO,   pEpiano);
  testTimbre(T_STRINGS,  pStrings);
  testTimbre(T_SINE,     pSine);
  testTimbre(T_TRIANGLE, pTri);
  testTimbre(T_ORGAN,    pOrgan);
  testTimbre(T_FLUTE,    pFlute);
  {
    char pWave[512];
    snprintf(pWave, sizeof(pWave), "%s/out_wave.wav", dir);
    testTimbre(T_WAVE, pWave);
  }

  printf("[test_dsp] WAVE (wavetable morphing):\n");
  testWaveMorph();

  printf("[test_dsp] grupos de voz (capas del looper no se cruzan):\n");
  testVoiceGroups();

  printf("[test_dsp] anti-aliasing PolyBLEP (nota aguda 7000 Hz):\n");
  testAntiAlias();

  printf("[test_dsp] envolvente ADSR (sustain / release / sin clicks):\n");
  testAdsr();

  printf("[test_dsp] filtro SVF (atenuación de agudos / estabilidad):\n");
  testFilter();

  printf("[test_dsp] LFO (rate del módulo / trémolo en el buffer):\n");
  testLfo();

  printf("[test_dsp] delay estéreo (respuesta al impulso / L≠R):\n");
  testDelay();

  printf("[test_dsp] reverb (RT60 / decaimiento / estabilidad):\n");
  testReverb();

  printf("[test_dsp] delay editable / sync a tempo:\n");
  testDelayEdit();

  printf("[test_dsp] STRUM (orden ascendente / espaciado por retardo):\n");
  testStrum();

  printf("[test_dsp] arpegiador (patrones / subdivisiones del BPM):\n");
  testArp();

  printf("[test_dsp] arp: cambio de acorde en vivo sin notas colgadas:\n");
  testArpNoStuck();

  printf("[test_dsp] ancho estéreo en cuerdas (decorrelación L/R):\n");
  testStereoWidth();

  printf("[test_dsp] humanización (micro-retardo / ganancia por voz):\n");
  testHumanize();

  printf("[test_dsp] ancho estéreo del acorde (paneo por voz):\n");
  testChordWidth();

  printf("[test_dsp] mono + glide (una voz / pitch que desliza):\n");
  testMonoGlide();

  printf("[test_dsp] glide poly entre acordes (legato / desliza):\n");
  testPolyGlide();

  printf("[test_dsp] sustain conmutable (release largo al soltar):\n");
  testSustain();

  printf("[test_dsp] chorus estéreo (decorrelación L/R al activar):\n");
  testChorus();

  printf("[test_dsp] generate random sound (randomiza y restaura):\n");
  testRandomSound();

  printf("[test_dsp] arp sync externo (avanza por ticks, no por reloj interno):\n");
  testArpExtSync();

  printf("[test_dsp] secuenciador de pasos (ARP_SEQ: índice / silencio / ligadura):\n");
  testArpStepSeq();

  printf("[test_dsp] percusión kick/snare (grave+decae / agudos+decae):\n");
  testDrums();

  printf("[test_dsp] looper 4 capas (grabación/wrap/overdub/no-clip):\n");
  testLooper();

  printf("[test_dsp] robo de voz con sustain (sin clicks al cambiar de acorde):\n");
  testVoiceSteal();

  printf("[test_dsp] capa BODY (cuerpo/unísono: detune+drift+transitorio):\n");
  testBody();

  printf("[test_dsp] tremolo global sincronizable (modulación a la tasa fijada):\n");
  testTremolo();

  printf("[test_dsp] edición de timbres (web-config: vivo/usuario/fábrica):\n");
  testEnvEdit();

  printf("[test_dsp] patrones de batería editables (slots de usuario):\n");
  testDrumPatternEdit();

  printf("[test_dsp] tablas de nombres completas (sin NULLs):\n");
  testNames();

  printf("[test_dsp] matching HiChord (sub sine dedicado + STRINGS brillante):\n");
  testHiChordBass();

  printf("[test_dsp] timbres de usuario (familia de oscilador elegible):\n");
  testUserTimbre();

  printf("[test_dsp] reloj maestro (compás por BPM / fase que envuelve en el '1'):\n");
  testMasterClock();

  printf("[test_dsp] phase-lock de batería (el kick cae en el '1', no al encender):\n");
  testDrumClockLock();

  printf("[test_dsp] looper cuantizado (largo en compases enteros):\n");
  testLooperQuantize();

  printf("[test_dsp] sync IN del reloj maestro (fase disciplinada al clock MIDI):\n");
  testClockExtSync();

  printf("\n%d checks, %d fallas\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
