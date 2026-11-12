/* ============================================================================
   vocal.h  -  Voz sintética por MODELADO FÍSICO (fuente glótica + tracto vocal).
   DSP PURO: sin Arduino, FreeRTOS ni I2S. No depende de dsp.h; compila en host.

   Etapa 2 (abajo, VocalArtic): consonantes como GESTOS sobre el mismo tubo
   (constricción + ruido en la constricción + rama nasal) para cantar sílabas.

   Modelo de base (etapa 1: vocales A E I O U sostenidas):
     FUENTE   derivada del flujo glótico, pulso polinómico (familia KLGLOTT88 /
              Rosenberg): fase abierta u(x)=x²(1-x), cierre abrupto. El salto del
              cierre se corrige con PolyBLEP (anti-alias en agudos).
     TRACTO   guía de ondas de Kelly-Lochbaum: N secciones cilíndricas de área
              configurable, una muestra de viaje por sección y sentido. A 44.1 kHz
              22 secciones = 17.5 cm (tracto adulto). Reflexión parcial en glotis
              y labios (la de labios con pasa-bajos: los formantes altos se
              amortiguan más, como en la radiación real).
     VOCAL    cada vocal es una función de área (8 puntos de control glotis ->
              labios, ver vocal.cpp). Cambiar de vocal = interpolar áreas; los
              formantes salen de la geometría, no se fijan a mano.

   El tracto es LINEAL: varias fuentes glóticas (un acorde) pueden compartir un
   mismo tracto y el resultado equivale a N cantantes con la misma vocal. dsp.cpp
   usa eso para que la polifonía cueste un oscilador por nota y no un tracto.

   Tiempo real: tick() y las funciones de la glotis no reservan memoria, no
   bloquean y no llaman a libm. control() va a tasa de bloque (N divisiones).

   Referencias conceptuales (NO se copió código de ninguna): docs/VOCAL.md.
   ============================================================================ */
#pragma once
#include <stdint.h>
#include "vocal_text.h"           // fonemas y sílabas (struct planos, sin lógica)

#define VOCAL_NASAL_SECTIONS 13   // rama nasal: ~10 cm, áreas fijas
#define VOCAL_NASAL_OUT 0.16f     // nivel de lo que irradian las narinas frente a los labios
#define VOCAL_MAX_SECTIONS 24     // tope de secciones del tracto (RAM fija)
#define VOCAL_CTRL_POINTS  8      // puntos de control del área (glotis -> labios)
#define VOCAL_VOWELS       5      // A E I O U

// Posición de cada vocal en el eje "morph" 0..1 (lo que guarda EnvCfg.wtMorph).
#define VOCAL_MORPH_A 0.00f
#define VOCAL_MORPH_E 0.25f
#define VOCAL_MORPH_I 0.50f
#define VOCAL_MORPH_O 0.75f
#define VOCAL_MORPH_U 1.00f

// Cociente abierto del ciclo glótico (fracción del período con la glotis abierta).
#define VOCAL_OQ      0.60f
#define VOCAL_INV_OQ  (1.0f / VOCAL_OQ)

// Reflexión en los extremos del tubo (pérdidas). La glotis NO es una pared rígida:
// durante la fase abierta parte de la onda se va hacia la tráquea, y eso es lo que
// ensancha el primer formante. Con valores más cercanos a 1 los formantes quedan
// tan angostos que una nota cuyo armónico cae justo encima suena mucho más fuerte.
#define VOCAL_REFL_GLOTTIS  0.70f
#define VOCAL_REFL_LIPS    -0.85f

// Áreas de control de fábrica por vocal y ganancia de nivelado (vocal.cpp).
extern const float VOCAL_AREA[VOCAL_VOWELS][VOCAL_CTRL_POINTS];
extern const float VOCAL_GAIN[VOCAL_VOWELS];

// ----------------------------------------------------------------------------
// Tracto vocal (guía de ondas). Un struct = un tubo; sin memoria dinámica.
// ----------------------------------------------------------------------------
struct VocalTract {
  int   n = 0;                          // secciones en uso (2..VOCAL_MAX_SECTIONS)
  float r[VOCAL_MAX_SECTIONS];          // onda de presión que viaja hacia los labios
  float l[VOCAL_MAX_SECTIONS];          // onda de presión que viaja hacia la glotis
  float k[VOCAL_MAX_SECTIONS];          // reflexión en la juntura (i-1 | i); k[0] sin uso
  float area[VOCAL_MAX_SECTIONS];       // área actual por sección (unidad arbitraria)
  float targ[VOCAL_MAX_SECTIONS];       // área objetivo (vocal pedida)
  float lipLp   = 0.0f;                 // estado del pasa-bajos de la reflexión labial
  float lipA    = 0.5f;                 // coeficiente de ese pasa-bajos
  float gain    = 1.0f;                 // nivelado de salida actual (por vocal)
  float targGain = 1.0f;
  float slew    = 0.0f;                 // 1/tau del cambio de vocal (1/s)
  bool  settled = true;                 // área == objetivo (control() no recalcula)

  // --- Etapa 2: ruido en la constricción y rama nasal (sólo las usa tickFull) ---
  int   nzPos   = 0;                    // sección donde entra el ruido de fricción/explosión
  float lipOpen = 1.0f;                 // cuánto irradian los labios (0 = cerrados: p, b, m)
  float nzOut   = 0.0f;                 // ruido que sale directo (constricción EN los labios: f)
  int   velumIx = 1;                    // juntura donde se abre la rama nasal (velo)
  float g1 = 1, g2 = 1, g3 = 0;         // juntura de 3 puertos: 2*A/suma (faringe, boca, nariz)
  bool  nasalOn = false;                // la rama nasal se está calculando
  int   nasalHold = 0;                  // muestras de cola de la rama tras cerrar el velo
  float nr[VOCAL_NASAL_SECTIONS];       // ondas de la rama nasal (hacia las narinas)
  float nl[VOCAL_NASAL_SECTIONS];       // (hacia el velo)
  float nk[VOCAL_NASAL_SECTIONS];       // reflexiones de la rama (nk[1] depende del velo)
  float nasLp   = 0.0f;                 // pasa-bajos de la reflexión en las narinas

  // sr = frecuencia de muestreo; lengthCm = largo del tubo (17.5 = adulto).
  void init(float sr, float lengthCm);
  void reset();                          // silencia las ondas (no toca las áreas)
  // morph 0..1 (A..U, interpola vocales vecinas). snap = sin transición.
  void setVowel(float morph, bool snap = false);
  // Áreas de control arbitrarias (tests / ajuste). gainOut = nivelado.
  void setAreas(const float* ctrl, float gainOut, bool snap = false);
  void control(float dtSec);             // tasa de bloque: desliza áreas y recalcula k
  // Etapa 2: fija la forma YA (sin deslizar; el deslizamiento lo hace VocalArtic).
  //   ctrl = 8 áreas de la vocal, gainOut = nivelado; place 0..1 (glotis..labios),
  //   consArea = área de la constricción, c = cuánto actúa (0..1), velum = 0..1.
  void setShape(const float* ctrl, float gainOut, float place, float consArea,
                float c, float velum);

  // Una muestra: entra la excitación glótica, sale la presión radiada en labios.
  inline float tick(float exc) {
    // Extremo glotis: casi cerrado -> refleja con el mismo signo (pérdida leve).
    float prev = r[0];
    r[0] = exc + VOCAL_REFL_GLOTTIS * l[0];
    // Junturas: una multiplicación cada una. f/b = ondas que LLEGAN a la juntura.
    for (int i = 1; i < n; i++) {
      float f = prev, b = l[i];
      float w = k[i] * (f - b);
      prev = r[i];
      r[i]     = f + w;                  // transmitida hacia los labios
      l[i - 1] = b + w;                  // reflejada/transmitida hacia la glotis
    }
    // Extremo labios: tubo abierto -> refleja invertido; el pasa-bajos deja
    // escapar más los agudos (ancho de banda creciente de los formantes altos).
    lipLp += lipA * (prev - lipLp);
    l[n - 1] = VOCAL_REFL_LIPS * lipLp;
    return prev * gain;
  }

  // Igual que tick() más: ruido `nz` inyectado en la constricción y rama nasal
  // (juntura de 3 puertos en el velo). Se usa mientras hay letra/consonantes.
  inline float tickFull(float exc, float nz) {
    float prev = r[0];
    r[0] = exc + VOCAL_REFL_GLOTTIS * l[0];
    const int V = velumIx;
    for (int i = 1; i < V; i++) {
      float f = prev, b = l[i];
      float w = k[i] * (f - b);
      prev = r[i]; r[i] = f + w; l[i - 1] = b + w;
    }
    float nIn = 0.0f;
    {                                    // velo: faringe | boca | nariz
      float f = prev, b = l[V];
      prev = r[V];
      if (nasalOn) {
        float bn = nl[0];
        float p = g1 * f + g2 * b + g3 * bn;       // presión común en la juntura
        r[V] = p - b; l[V - 1] = p - f; nIn = p - bn;
      } else {
        float w = k[V] * (f - b);
        r[V] = f + w; l[V - 1] = b + w;
      }
    }
    for (int i = V + 1; i < n; i++) {
      float f = prev, b = l[i];
      float w = k[i] * (f - b);
      prev = r[i]; r[i] = f + w; l[i - 1] = b + w;
    }
    lipLp += lipA * (prev - lipLp);
    l[n - 1] = VOCAL_REFL_LIPS * lipLp;
    r[nzPos] += nz;                      // turbulencia aguas abajo de la constricción
    float out = prev * lipOpen + nz * nzOut;   // labios cerrados: no sale sonido por la boca
    if (nasalOn) {
      float np = nr[0];
      nr[0] = nIn;
      for (int j = 1; j < VOCAL_NASAL_SECTIONS; j++) {
        float f = np, b = nl[j];
        float w = nk[j] * (f - b);
        np = nr[j]; nr[j] = f + w; nl[j - 1] = b + w;
      }
      nasLp += lipA * (np - nasLp);
      nl[VOCAL_NASAL_SECTIONS - 1] = -0.80f * nasLp;   // narinas: extremo abierto con pérdida
      out += VOCAL_NASAL_OUT * np;
      if (nasalHold > 0 && --nasalHold == 0) {         // velo cerrado y cola agotada
        nasalOn = false;
        for (int j = 0; j < VOCAL_NASAL_SECTIONS; j++) nr[j] = nl[j] = 0.0f;
        nasLp = 0.0f;
      }
    }
    return out * gain;
  }
};

// ----------------------------------------------------------------------------
// Etapa 2: articulación. Una sílaba se convierte en una cola de TRAMOS (gestos):
// cada tramo fija objetivos de forma vocal, constricción, sonoridad, ruido y
// velo, con una duración. control() avanza la cola a tasa de control y desliza
// los valores; apply() vuelca la forma a un tracto. Sin memoria dinámica.
// ----------------------------------------------------------------------------
struct VocalSeg {
  uint8_t  shape;      // VP_A..VP_U = mover la lengua hacia esa vocal; 0 = no cambia
  float    place;      // lugar de la constricción (0 glotis .. 1 labios)
  float    area;       // área de la constricción
  float    c;          // cuánto actúa la constricción (0..1)
  float    voice;      // sonoridad: ganancia de la fuente glótica (0..1)
  float    noise;      // ruido en la constricción (fricción / explosión)
  float    velum;      // apertura de la rama nasal (0..1)
  float    ms;         // duración del tramo
  float    tauMs;      // rapidez con que la constricción llega a su objetivo
  float    color;      // color del ruido: 1 = blanco, 0..1 = más grave, <0 = sólo agudos (silbante)
};
#define VOCAL_SEG_MAX 24

struct VocalArtic {
  float ctrl[VOCAL_CTRL_POINTS + 1];    // forma vocal actual (8 áreas + nivelado)
  float tctrl[VOCAL_CTRL_POINTS + 1];   // forma objetivo
  float place = 1.0f, area = 1.0f;      // constricción vigente
  float c = 0.0f, cT = 0.0f, cTau = 0.006f;
  float velum = 0.0f, velumT = 0.0f;
  float voiceT = 1.0f, noiseT = 0.0f;   // objetivos que lee el motor (los suaviza por muestra)
  float vowelTau = 0.045f;              // rapidez del cambio de vocal
  VocalSeg q[VOCAL_SEG_MAX];
  uint8_t  qh = 0, qn = 0;              // cola circular
  float    segLeft = 0.0f;              // segundos que le quedan al tramo en curso
  bool     dirty = true;                // la forma cambió: hay que llamar a apply()

  void reset(float morph);              // estado limpio en una vocal (sin transición)
  void setMorph(float morph);           // objetivo por morph 0..1 (modo sin letra)
  // soft = b/d/g suaves (aproximantes, como entre vocales) en vez de oclusivas.
  void startSyllable(const VocalSyl& s, bool soft);
  void closeSyllable(const VocalSyl& s);  // deslizadas finales + coda
  bool busy() const { return qn > 0 || segLeft > 0.0f; }
  void flush() { qh = qn = 0; segLeft = 0.0f; }   // descarta gestos pendientes (sílaba nueva antes de tiempo)
  float noiseColor = 1.0f;              // color del ruido vigente (ver VocalSeg.color)
  void control(float dtSec);            // avanza la cola y desliza
  void apply(VocalTract& t) { t.setShape(ctrl, ctrl[VOCAL_CTRL_POINTS], place, area, c, velum); }

 private:
  void push(const VocalSeg& s);
  void pushCons(uint8_t ph, bool soft);
  void setShapeTarget(uint8_t vp);
};

// ----------------------------------------------------------------------------
// Fuente glótica. ph = fase 0..1 del ciclo, dt = incremento por muestra (f0/sr).
// ----------------------------------------------------------------------------
// Residuo PolyBLEP de un escalón en t=0 (t ya envuelto a 0..1).
static inline float vocalPolyBlep(float t, float dt) {
  if (t < dt)        { float x = t / dt;          return x + x - x * x - 1.0f; }
  if (t > 1.0f - dt) { float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
  return 0.0f;
}

// Derivada del flujo glótico, normalizada (pico negativo -1 en el cierre). Es la
// excitación del tracto: usar la derivada incluye la radiación de labios (+6
// dB/oct) sin un filtro aparte. Media cero por construcción.
static inline float vocalGlottisDeriv(float ph, float dt) {
  float s = 0.0f;
  if (ph < VOCAL_OQ) { float x = ph * VOCAL_INV_OQ; s = x * (2.0f - 3.0f * x); }
  float d = ph - VOCAL_OQ; if (d < 0.0f) d += 1.0f;   // distancia al cierre
  return s + 0.5f * vocalPolyBlep(d, dt);              // el cierre salta -1 -> 0
}

// Flujo glótico 0..1 (cuánto aire pasa): modula el ruido de aspiración.
static inline float vocalGlottisFlow(float ph) {
  if (ph >= VOCAL_OQ) return 0.0f;
  float x = ph * VOCAL_INV_OQ;
  return 6.75f * x * x * (1.0f - x);
}
