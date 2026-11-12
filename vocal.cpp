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

// Áreas fijas de la rama nasal (velo -> narinas). La primera sección toma el
// área del velo (se abre/cierra); el resto es una cavidad ancha con salida angosta.
static const float VOCAL_NASAL_AREA[VOCAL_NASAL_SECTIONS] =
  { 1.0f, 1.6f, 2.2f, 2.6f, 2.6f, 2.4f, 2.2f, 2.0f, 1.6f, 1.2f, 0.9f, 0.6f, 0.5f };
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
  for (int j = 0; j < VOCAL_NASAL_SECTIONS; j++) nk[j] = 0.0f;
  for (int j = 2; j < VOCAL_NASAL_SECTIONS; j++)
    nk[j] = (VOCAL_NASAL_AREA[j - 1] - VOCAL_NASAL_AREA[j]) / (VOCAL_NASAL_AREA[j - 1] + VOCAL_NASAL_AREA[j]);
  lipOpen = 1.0f; nzOut = 0.0f; nzPos = sec - 1; velumIx = sec / 2; g1 = g2 = 1.0f; g3 = 0.0f; nasalOn = false; nasalHold = 0;
  gain = targGain = 1.0f;
  reset();
  setVowel(VOCAL_MORPH_A, true);
}

void VocalTract::reset() {
  for (int i = 0; i < VOCAL_MAX_SECTIONS; i++) r[i] = l[i] = 0.0f;
  for (int j = 0; j < VOCAL_NASAL_SECTIONS; j++) nr[j] = nl[j] = 0.0f;
  lipLp = 0.0f; nasLp = 0.0f; nasalOn = false; nasalHold = 0;
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

// ============================================================================
// Etapa 2: forma con constricción + rama nasal, y articulación de sílabas.
// ============================================================================
static const float VOCAL_VELUM_POS = 0.50f;   // el velo está a mitad del tubo

void VocalTract::setShape(const float* ctrl, float gainOut, float place, float consArea,
                          float c, float velum) {
  if (place < 0.0f) place = 0.0f; if (place > 1.0f) place = 1.0f;
  int ci = (int)(place * (float)n); if (ci > n - 1) ci = n - 1;   // sección de la constricción
  for (int i = 0; i < n; i++) {
    float x = ((float)i + 0.5f) / (float)n * (float)(VOCAL_CTRL_POINTS - 1);
    int   cp = (int)x; if (cp > VOCAL_CTRL_POINTS - 2) cp = VOCAL_CTRL_POINTS - 2;
    float a = ctrl[cp] + (ctrl[cp + 1] - ctrl[cp]) * (x - (float)cp);
    int   d = i - ci; if (d < 0) d = -d;
    float w = (d == 0) ? 1.0f : (d == 1 ? 0.45f : 0.0f);         // la lengua no cierra en un punto
    float open = a;                                              // área sin la constricción
    a += (consArea - a) * c * w;
    area[i] = targ[i] = (a < 0.001f) ? 0.001f : a;
    // Lo que irradia la boca es caudal: proporcional al área de los labios. Una
    // vocal queda en 1 (su nivel ya está en VOCAL_GAIN); un cierre labial lo anula.
    if (i == n - 1) { lipOpen = area[i] / open; if (lipOpen > 1.0f) lipOpen = 1.0f; }
  }
  gain = targGain = gainOut;
  settled = true;
  for (int i = 1; i < n; i++) k[i] = (area[i - 1] - area[i]) / (area[i - 1] + area[i]);
  nzPos = (ci + 1 < n) ? ci + 1 : n - 1;
  nzOut = (ci >= n - 1) ? 1.5f : 0.0f;                          // turbulencia en el borde de los labios
  // Velo: juntura de 3 puertos. Con el velo cerrado (a3 = 0) es la juntura normal.
  velumIx = (int)(VOCAL_VELUM_POS * (float)n);
  if (velumIx < 1) velumIx = 1; if (velumIx > n - 1) velumIx = n - 1;
  float a1 = area[velumIx - 1], a2 = area[velumIx], a3 = (velum > 0.0f) ? velum : 0.0f;
  float inv = 2.0f / (a1 + a2 + a3);
  g1 = a1 * inv; g2 = a2 * inv; g3 = a3 * inv;
  float an0 = (a3 > 0.0005f) ? a3 : 0.0005f;                     // 1ra sección nasal = velo
  nk[1] = (an0 - VOCAL_NASAL_AREA[1]) / (an0 + VOCAL_NASAL_AREA[1]);
  if (a3 > 0.001f) { nasalOn = true; nasalHold = 0; }
  else if (nasalOn && nasalHold == 0) nasalHold = 2048;          // deja apagarse la rama
}

// Definición de cada consonante: dónde y cuánto se cierra el tubo, y cómo suena.
enum : uint8_t { VK_STOP, VK_FRIC, VK_AFFR, VK_NASAL, VK_APPROX, VK_TAP, VK_TRILL };
// color = espectro de la turbulencia (parámetro por consonante; NO sale del tubo).
struct VocalConsDef { float place, area; uint8_t kind, voiced; float noise; float color; };
static const VocalConsDef VOCAL_CONS[VP_COUNT] = {
  {0,0,0,0,0,1}, {0,0,0,0,0,1}, {0,0,0,0,0,1}, {0,0,0,0,0,1}, {0,0,0,0,0,1}, {0,0,0,0,0,1},   // -, vocales
  { 1.00f, 0.001f, VK_STOP,   0, 0.90f, 0.30f },   // p  bilabial
  { 1.00f, 0.012f, VK_STOP,   1, 0.35f, 0.30f },   // b
  { 0.88f, 0.001f, VK_STOP,   0, 1.00f, 0.90f },   // t  dental
  { 0.88f, 0.012f, VK_STOP,   1, 0.40f, 0.90f },   // d
  { 0.60f, 0.001f, VK_STOP,   0, 0.90f, 0.40f },   // k  velar
  { 0.60f, 0.012f, VK_STOP,   1, 0.35f, 0.40f },   // g
  { 0.97f, 0.100f, VK_FRIC,   0, 0.45f, 0.70f },   // f  labiodental
  { 0.90f, 0.070f, VK_FRIC,   0, 0.80f, -0.35f },   // s  alveolar
  { 0.62f, 0.100f, VK_FRIC,   0, 0.60f, 0.22f },   // j  velar
  { 0.78f, 0.001f, VK_AFFR,   0, 0.90f, -0.60f },   // ch palatal
  { 1.00f, 0.001f, VK_NASAL,  1, 0.00f, 1.00f },   // m
  { 0.88f, 0.001f, VK_NASAL,  1, 0.00f, 1.00f },   // n
  { 0.76f, 0.001f, VK_NASAL,  1, 0.00f, 1.00f },   // ñ
  { 0.88f, 0.220f, VK_APPROX, 1, 0.00f, 1.00f },   // l
  { 0.88f, 0.060f, VK_TAP,    1, 0.00f, 1.00f },   // r  (vibrante simple: roza, no sella)
  { 0.88f, 0.060f, VK_TRILL,  1, 0.00f, 1.00f },   // rr (vibrante múltiple)
  { 0.76f, 0.140f, VK_APPROX, 1, 0.12f, 0.50f },   // y / ll
};

void VocalArtic::setShapeTarget(uint8_t vp) {
  if (!vocalIsVowel(vp)) return;
  int v = vp - VP_A;
  for (int i = 0; i < VOCAL_CTRL_POINTS; i++) tctrl[i] = VOCAL_AREA[v][i];
  tctrl[VOCAL_CTRL_POINTS] = VOCAL_GAIN[v];
}

void VocalArtic::setMorph(float morph) {
  if (!(morph > 0.0f)) morph = 0.0f;
  if (morph > 1.0f) morph = 1.0f;
  float x = morph * (float)(VOCAL_VOWELS - 1);
  int   v = (int)x; if (v > VOCAL_VOWELS - 2) v = VOCAL_VOWELS - 2;
  float fr = x - (float)v;
  for (int i = 0; i < VOCAL_CTRL_POINTS; i++)
    tctrl[i] = VOCAL_AREA[v][i] + (VOCAL_AREA[v + 1][i] - VOCAL_AREA[v][i]) * fr;
  tctrl[VOCAL_CTRL_POINTS] = VOCAL_GAIN[v] + (VOCAL_GAIN[v + 1] - VOCAL_GAIN[v]) * fr;
  dirty = true;
}

void VocalArtic::reset(float morph) {
  setMorph(morph);
  for (int i = 0; i <= VOCAL_CTRL_POINTS; i++) ctrl[i] = tctrl[i];
  place = 1.0f; area = 1.0f; c = cT = 0.0f; cTau = 0.006f;
  velum = velumT = 0.0f; voiceT = 1.0f; noiseT = 0.0f;
  qh = qn = 0; segLeft = 0.0f; dirty = true; noiseColor = 1.0f;
}

void VocalArtic::push(const VocalSeg& s) {
  if (qn >= VOCAL_SEG_MAX) return;                 // cola llena: se descarta el gesto
  q[(qh + qn) % VOCAL_SEG_MAX] = s;
  qn++;
}

// Gesto de una consonante. Tiempos en ms: valores típicos del habla cantada.
void VocalArtic::pushCons(uint8_t ph, bool soft) {
  if (ph <= VP_U || ph >= VP_COUNT) return;
  const VocalConsDef& d = VOCAL_CONS[ph];
  //                 shape place    area    c  voice noise  velum  ms   tau
  switch (d.kind) {
    case VK_STOP:
      if (d.voiced && soft) {                       // b/d/g entre vocales: no llegan a cerrar
        push({ 0, d.place, 0.10f, 1, 0.90f, 0,       0, 50, 10, d.color });
      } else if (d.voiced) {
        // Sonoridad baja durante el cierre: si no, la presión se acumula detrás
        // de la oclusión y al soltar sale un golpe desproporcionado.
        push({ 0, d.place, d.area, 1, 0.22f, 0,       0, 55,  5, d.color });   // cierre con barra de sonoridad
        push({ 0, d.place, d.area, 0, 0.85f, d.noise, 0, 12,  7, d.color });   // explosión
      } else {
        push({ 0, d.place, d.area, 1, 0.0f,  0,       0, 70,  5, d.color });   // cierre mudo
        push({ 0, d.place, d.area, 0, 0.0f,  d.noise, 0, 14,  3, d.color });   // explosión
        push({ 0, d.place, d.area, 0, 0.0f,  d.noise * 0.25f, 0, 16, 3, d.color });   // aspiración breve
      }
      break;
    case VK_FRIC:
      push({ 0, d.place, d.area, 1, 0.0f, d.noise, 0, 95, 10, d.color });
      break;
    case VK_AFFR:
      push({ 0, d.place, d.area, 1, 0.0f, 0,       0, 45,  5, d.color });      // oclusión
      push({ 0, d.place, 0.07f,  1, 0.0f, d.noise, 0, 70,  4, d.color });      // suelta en fricción
      break;
    case VK_NASAL:
      push({ 0, d.place, d.area, 1, 0.90f, 0, 1.0f, 80, 6, d.color });
      break;
    case VK_APPROX:
      push({ 0, d.place, d.area, 1, 1.0f, d.noise, 0, 70, 12, d.color });
      break;
    case VK_TAP:
      push({ 0, d.place, d.area, 1, 0.55f, 0, 0, 22, 5, d.color });
      break;
    case VK_TRILL:
      for (int i = 0; i < 3; i++) {
        push({ 0, d.place, d.area, 1, 0.55f, 0, 0, 20, 5, d.color });
        if (i < 2) push({ 0, d.place, d.area, 0, 0.90f, 0, 0, 16, 5, d.color });
      }
      break;
  }
}

void VocalArtic::startSyllable(const VocalSyl& s, bool soft) {
  // Coarticulación: la lengua va hacia la primera vocal DURANTE las consonantes.
  uint8_t first = s.nuc[0];
  bool any = false;
  for (int i = 0; i < VOCAL_ONSET_MAX; i++) {
    if (!s.onset[i]) continue;
    uint8_t before = qn;
    pushCons(s.onset[i], soft && !any);
    if (!any && qn > before) q[(qh + before) % VOCAL_SEG_MAX].shape = first;
    any = true;
  }
  // Núcleo: deslizadas previas (ia, ue...) y la vocal que se sostiene.
  for (int i = 0; i <= s.main && i < VOCAL_NUC_MAX; i++) {
    if (!s.nuc[i]) continue;
    push({ s.nuc[i], place, area, 0, 1.0f, 0, 0, (i < s.main) ? 70.0f : 30.0f, 8, 1 });
  }
}

void VocalArtic::closeSyllable(const VocalSyl& s) {
  for (int i = s.main + 1; i < VOCAL_NUC_MAX; i++)          // deslizadas finales (ai, eu...)
    if (s.nuc[i]) push({ s.nuc[i], place, area, 0, 1.0f, 0, 0, 80, 8, 1 });
  bool any = false;
  for (int i = 0; i < VOCAL_CODA_MAX; i++)
    if (s.coda[i]) { pushCons(s.coda[i], false); any = true; }
  if (any) push({ 0, place, area, 0, 1.0f, 0, 0, 20, 6, 1 });  // suelta la última constricción
}

void VocalArtic::control(float dtSec) {
  // --- avanza la cola de tramos ---
  segLeft -= dtSec;
  while (segLeft <= 0.0f && qn > 0) {
    const VocalSeg& s = q[qh];
    qh = (uint8_t)((qh + 1) % VOCAL_SEG_MAX); qn--;
    if (s.shape) setShapeTarget(s.shape);
    if (s.c > 0.0f) { place = s.place; area = s.area; }      // al soltar, queda donde estaba
    cT = s.c; cTau = s.tauMs * 0.001f;
    voiceT = s.voice; noiseT = s.noise; velumT = s.velum;
    if (s.noise > 0.0f) noiseColor = s.color;
    segLeft += s.ms * 0.001f;
    dirty = true;
  }
  if (segLeft <= 0.0f) {
    segLeft = 0.0f;
    // Cola vacía: SIEMPRE se vuelve al reposo (boca abierta, sonora, velo cerrado).
    // Así un gesto cortado o descartado nunca deja la voz muda o tapada.
    if (cT != 0.0f || voiceT != 1.0f || noiseT != 0.0f || velumT != 0.0f) {
      cT = 0.0f; voiceT = 1.0f; noiseT = 0.0f; velumT = 0.0f; cTau = 0.008f; dirty = true;
    }
  }
  // --- desliza hacia los objetivos ---
  float kv = dtSec / vowelTau; if (kv > 1.0f) kv = 1.0f;
  float kc = dtSec / cTau;     if (kc > 1.0f) kc = 1.0f;
  float kn = dtSec / 0.012f;   if (kn > 1.0f) kn = 1.0f;
  float far = 0.0f;
  for (int i = 0; i <= VOCAL_CTRL_POINTS; i++) {
    float d = tctrl[i] - ctrl[i];
    ctrl[i] += kv * d;
    if (d < 0.0f) d = -d; if (d > far) far = d;
  }
  float dc = cT - c;         c     += kc * dc; if (dc < 0.0f) dc = -dc; if (dc > far) far = dc;
  float dv = velumT - velum; velum += kn * dv; if (dv < 0.0f) dv = -dv; if (dv > far) far = dv;
  if (far > 1e-4f) dirty = true;
  else {                                                     // llegó: clava los valores
    for (int i = 0; i <= VOCAL_CTRL_POINTS; i++) ctrl[i] = tctrl[i];
    c = cT; velum = velumT;
  }
}
