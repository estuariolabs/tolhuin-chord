/* ============================================================================
   vocal.cpp  -  Tablas de vocales y control (tasa de bloque) del tracto vocal.
   El camino por muestra está inline en vocal.h. Sin Arduino / FreeRTOS / I2S.
   ============================================================================ */
#include <math.h>
#include "vocal.h"

// Áreas de control por vocal (glotis -> labios, unidad arbitraria: sólo importan
// los cocientes entre secciones vecinas). GENERADAS por tools/vocal_fit.cpp:
// parte de una forma articulatoria cualitativa (faringe / constricción / labios)
// y la ajusta hasta que los tres primeros formantes MEDIDOS del propio modelo
// caen en los objetivos de docs/VOCAL.md. No son datos de MRI ni de CHIMERA.
// VOCAL_FIT_BEGIN
const float VOCAL_AREA[VOCAL_VOWELS][VOCAL_CTRL_POINTS] = {
  { 0.600f, 0.437f, 0.344f, 0.533f, 1.267f, 2.603f, 2.081f, 1.359f },   // A
  { 0.600f, 0.961f, 2.091f, 1.683f, 0.782f, 0.616f, 1.155f, 2.386f },   // E
  { 0.600f, 1.444f, 4.910f, 1.882f, 0.549f, 0.277f, 0.542f, 2.166f },   // I
  { 0.600f, 0.239f, 0.150f, 0.154f, 0.338f, 1.293f, 0.385f, 0.154f },   // O
  { 0.600f, 4.650f, 0.925f, 0.365f, 0.943f, 6.492f, 0.965f, 0.253f },   // U
};
const float VOCAL_GAIN[VOCAL_VOWELS] = { 1.0112f, 1.1879f, 1.9919f, 0.2505f, 0.2215f };
// VOCAL_FIT_END

static const float VOCAL_SOUND_CM_S = 35000.0f;   // velocidad del sonido (aire tibio)
static const float VOCAL_SLEW_MS    = 45.0f;      // constante de tiempo del cambio de vocal
static const float VOCAL_LIP_HZ     = 5000.0f;    // corte del pasa-bajos de la reflexión labial

void VocalTract::init(float sr, float lengthCm) {
  // Una sección = lo que el sonido recorre en una muestra.
  int sec = (int)(lengthCm * sr / VOCAL_SOUND_CM_S + 0.5f);
  if (sec < 2) sec = 2;
  if (sec > VOCAL_MAX_SECTIONS) sec = VOCAL_MAX_SECTIONS;
  n = sec;
  lipA = 1.0f - expf(-6.2831853f * VOCAL_LIP_HZ / sr);
  if (lipA > 1.0f) lipA = 1.0f;
  slew = 1000.0f / VOCAL_SLEW_MS;
  for (int i = 0; i < VOCAL_MAX_SECTIONS; i++) { area[i] = targ[i] = 1.0f; k[i] = 0.0f; }
  gain = targGain = 1.0f;
  reset();
  setVowel(VOCAL_MORPH_A, true);
}

void VocalTract::reset() {
  for (int i = 0; i < VOCAL_MAX_SECTIONS; i++) r[i] = l[i] = 0.0f;
  lipLp = 0.0f;
}

void VocalTract::setAreas(const float* ctrl, float gainOut, bool snap) {
  // Reparte los puntos de control sobre las n secciones (interpolación lineal).
  for (int i = 0; i < n; i++) {
    float x = ((float)i + 0.5f) / (float)n * (float)(VOCAL_CTRL_POINTS - 1);
    int   c = (int)x; if (c > VOCAL_CTRL_POINTS - 2) c = VOCAL_CTRL_POINTS - 2;
    float fr = x - (float)c;
    float a = ctrl[c] + (ctrl[c + 1] - ctrl[c]) * fr;
    targ[i] = (a < 0.02f) ? 0.02f : a;             // nunca se cierra del todo
  }
  targGain = gainOut;
  settled = false;
  if (snap) {
    for (int i = 0; i < n; i++) area[i] = targ[i];
    gain = targGain;
    control(0.0f);
  }
}

void VocalTract::setVowel(float morph, bool snap) {
  if (!(morph > 0.0f)) morph = 0.0f;               // también atrapa NaN
  if (morph > 1.0f) morph = 1.0f;
  float x = morph * (float)(VOCAL_VOWELS - 1);
  int   v = (int)x; if (v > VOCAL_VOWELS - 2) v = VOCAL_VOWELS - 2;
  float fr = x - (float)v;
  float ctrl[VOCAL_CTRL_POINTS];
  for (int c = 0; c < VOCAL_CTRL_POINTS; c++)
    ctrl[c] = VOCAL_AREA[v][c] + (VOCAL_AREA[v + 1][c] - VOCAL_AREA[v][c]) * fr;
  setAreas(ctrl, VOCAL_GAIN[v] + (VOCAL_GAIN[v + 1] - VOCAL_GAIN[v]) * fr, snap);
}

void VocalTract::control(float dtSec) {
  if (settled) return;
  float c = dtSec * slew; if (c > 1.0f) c = 1.0f;
  float far = 0.0f;
  for (int i = 0; i < n; i++) {
    float d = targ[i] - area[i];
    area[i] += c * d;
    if (d < 0.0f) d = -d;
    if (d > far) far = d;
  }
  gain += c * (targGain - gain);
  if (far < 1e-4f) {                               // llegó: clava y deja de recalcular
    for (int i = 0; i < n; i++) area[i] = targ[i];
    gain = targGain;
    settled = true;
  }
  // Reflexión de presión en cada juntura: k = (A[i-1]-A[i]) / (A[i-1]+A[i]).
  for (int i = 1; i < n; i++) k[i] = (area[i - 1] - area[i]) / (area[i - 1] + area[i]);
}
