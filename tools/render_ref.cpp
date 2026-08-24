/* ============================================================================
   render_ref.cpp  -  Banco de calibración de timbres (host).
   Renderiza el motor DSP puro tocando un acorde y vuelca un WAV estéreo, para
   comparar el espectro contra un audio de referencia (matching HiChord).
   Compilar:  g++ -std=c++17 -I.. tools/render_ref.cpp ../dsp.cpp -o render_ref
   Uso:       render_ref <timbre 0..12> <chorus 0/1> <body 0/1> out.wav
   ============================================================================ */
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "dsp.h"

static void writeWavStereo(const char* path, const std::vector<float>& L,
                           const std::vector<float>& R, int sr) {
  FILE* f = fopen(path, "wb");
  int n = (int)L.size(), bytes = n * 2 * 2;
  auto w32 = [&](uint32_t v){ fwrite(&v,4,1,f); };
  auto w16 = [&](uint16_t v){ fwrite(&v,2,1,f); };
  fwrite("RIFF",1,4,f); w32(36+bytes); fwrite("WAVE",1,4,f);
  fwrite("fmt ",1,4,f); w32(16); w16(1); w16(2); w32(sr); w32(sr*4); w16(4); w16(16);
  fwrite("data",1,4,f); w32(bytes);
  for (int i=0;i<n;i++){
    float l=L[i], r=R[i];
    if(l>1)l=1; if(l<-1)l=-1; if(r>1)r=1; if(r<-1)r=-1;
    w16((int16_t)(l*32767)); w16((int16_t)(r*32767));
  }
  fclose(f);
}

int main(int argc, char** argv) {
  int timbre = argc>1 ? atoi(argv[1]) : T_STRINGS;
  bool chorus = argc>2 ? atoi(argv[2]) : 0;
  bool body   = argc>3 ? atoi(argv[3]) : 0;
  const char* out = argc>4 ? argv[4] : "out_ our.wav";

  dspInit();
  // Overrides opcionales para barrer sin recompilar: cut res fenv (del timbre).
  if (argc > 5) {
    EnvCfg e = dspEnvGet((uint8_t)timbre);
    if (argc > 5) e.cutHz   = (float)atof(argv[5]);
    if (argc > 6) e.res     = (float)atof(argv[6]);
    if (argc > 7) e.fEnvOct = (float)atof(argv[7]);
    dspEnvSet((uint8_t)timbre, e);
  }
  // HBMORPH permite barrer candidatos HBRASS A/B/C sin recompilar ni cambiar
  // el preset de fábrica (0=redondo, 1=brillante).
  if (getenv("HBMORPH")) {
    EnvCfg e = dspEnvGet((uint8_t)timbre);
    e.wtMorph = (float)atof(getenv("HBMORPH"));
    dspEnvSet((uint8_t)timbre, e);
  }
  // Barrido offline de EPIANO sin tocar el preset de fábrica. Los tiempos FM
  // se expresan en ms acá y se convierten al incremento interno del motor.
  if (timbre == T_EPIANO && getenv("EP_VARIANT")) {
    EnvCfg e = dspEnvGet(T_EPIANO);
    auto ef = [&](const char* key, float& dst) { if (getenv(key)) dst = (float)atof(getenv(key)); };
    ef("EP_ATK", e.atkMs); ef("EP_DEC", e.decMs); ef("EP_SUS", e.sus); ef("EP_REL", e.relMs);
    ef("EP_FMIDX", e.fmIdx); ef("EP_FMFLOOR", e.fmFloor);
    ef("EP_TINE", e.fmIdx2); ef("EP_RATIO", e.tineRatio);
    ef("EP_CUT", e.cutHz); ef("EP_RES", e.res); ef("EP_FENV", e.fEnvOct);
    if (getenv("EP_FMDEC_MS")) e.fmDec = 1.0f / ((float)atof(getenv("EP_FMDEC_MS")) * SAMPLE_RATE / 1000.0f);
    if (getenv("EP_TINEDEC_MS")) e.fmDec2 = 1.0f / ((float)atof(getenv("EP_TINEDEC_MS")) * SAMPLE_RATE / 1000.0f);
    if (getenv("EP_PERC")) e.perc = atoi(getenv("EP_PERC")) != 0;
    dspEnvSet(T_EPIANO, e);
  }
  if (chorus) dspSetChorus(true, 0.55f, 6.0f, 0.35f);
  if (body)   dspSetBody(true, 0.9f);
  if (getenv("FXMODE")) dspSetFxMode((uint8_t)atoi(getenv("FXMODE")));

  // Acorde de la referencia: Cm9 desplegado con BAJO C2 dominante.
  //   bajo C2=36 (voz propia) ; acorde C3=48 G3=55 C4=60 Eb4=63 G4=67 Bb4=70
  int chord[] = { 48, 55, 60, 63, 67, 70 };
  for (int note : chord) dspNoteOn((uint8_t)note, (uint8_t)timbre, 0.85f, 0);
  // Bajo: timbre y ganancia por env var (BASST, BASSG) para barrer.
  int   bt = getenv("BASST") ? atoi(getenv("BASST")) : timbre;
  float bg = getenv("BASSG") ? (float)atof(getenv("BASSG")) : 0.85f;
  dspNoteOn(36, (uint8_t)bt, bg, 0, VG_BASS);

  const int SR = SAMPLE_RATE, BLK = AUDIO_BLOCK;
  std::vector<float> L, R;
  float bl[AUDIO_BLOCK], br[AUDIO_BLOCK];
  float duration = getenv("DURATION") ? (float)atof(getenv("DURATION")) : 2.5f;
  int releaseAt = getenv("RELEASE_MS") ? (int)(atof(getenv("RELEASE_MS")) * SR / 1000.0) : -1;
  bool released = false;
  for (int b=0; b < (int)(duration*SR)/BLK; b++) {
    if (!released && releaseAt >= 0 && b * BLK >= releaseAt) { dspAllOff(); released = true; }
    dspRenderStereo(bl, br, BLK);
    for (int i=0;i<BLK;i++){ L.push_back(bl[i]); R.push_back(br[i]); }
  }
  writeWavStereo(out, L, R, SR);
  printf("render: timbre=%d chorus=%d body=%d -> %s (%zu muestras)\n",
         timbre, chorus, body, out, L.size());
  return 0;
}
