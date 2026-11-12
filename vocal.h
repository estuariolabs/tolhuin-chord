/* ============================================================================
   vocal.h  -  Voz sintética por MODELADO FÍSICO (fuente glótica + tracto vocal).
   DSP PURO: sin Arduino, FreeRTOS ni I2S. No depende de dsp.h; compila en host.

   Modelo (etapa 1: vocales A E I O U sostenidas, sin texto ni consonantes):
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

  // sr = frecuencia de muestreo; lengthCm = largo del tubo (17.5 = adulto).
  void init(float sr, float lengthCm);
  void reset();                          // silencia las ondas (no toca las áreas)
  // morph 0..1 (A..U, interpola vocales vecinas). snap = sin transición.
  void setVowel(float morph, bool snap = false);
  // Áreas de control arbitrarias (tests / ajuste). gainOut = nivelado.
  void setAreas(const float* ctrl, float gainOut, bool snap = false);
  void control(float dtSec);             // tasa de bloque: desliza áreas y recalcula k

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
