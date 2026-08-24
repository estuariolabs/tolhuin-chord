/* Benchmark relativo del hot path DSP en host. No predice microsegundos del
   ESP32-S3; compara BRASS/HBRASS con idénticas 12 voces y FX para detectar una
   regresión algorítmica antes de medir BLOCK_BUDGET_US en la placa. */
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "dsp.h"

int main(int argc, char** argv) {
  uint8_t timbre = (uint8_t)(argc > 1 ? atoi(argv[1]) : T_BRASS);
  uint8_t fxMode = (uint8_t)(argc > 2 ? atoi(argv[2]) : 2);
  dspInit();
  dspSetFxMode(fxMode);
  dspSetChorus(true, 0.55f, 6.0f, 0.35f);
  dspSetBody(true, 0.9f);
  const uint8_t notes[MAX_VOICES] = {36,43,48,52,55,58,60,63,67,70,74,77};
  for (int i=0; i<MAX_VOICES; ++i)
    dspNoteOn(notes[i], timbre, 0.80f, 0, i == 0 ? VG_BASS : VG_CHORD);

  constexpr int SECONDS = 20;
  constexpr int BLOCKS = (SECONDS * (int)SAMPLE_RATE) / (int)AUDIO_BLOCK;
  float l[AUDIO_BLOCK], r[AUDIO_BLOCK];
  volatile float checksum = 0.0f;
  auto t0 = std::chrono::steady_clock::now();
  for (int b=0; b<BLOCKS; ++b) {
    dspRenderStereo(l, r, AUDIO_BLOCK);
    checksum += l[b & (AUDIO_BLOCK-1)] + r[(b*7) & (AUDIO_BLOCK-1)];
  }
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double,std::milli>(t1-t0).count();
  std::printf("timbre=%u fxmode=%u rendered=%ds host_ms=%.2f us_per_block=%.2f checksum=%.6f\n",
              timbre, fxMode, SECONDS, ms, ms*1000.0/BLOCKS, (double)checksum);
  return std::isfinite((double)checksum) ? 0 : 2;
}
