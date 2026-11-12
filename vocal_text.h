/* ============================================================================
   vocal_text.h  -  Texto -> fonemas -> sílabas (español), para el timbre VOCAL.
   PURO: sin Arduino, sin memoria dinámica, sin diccionario. La ortografía del
   español es casi fonémica, así que alcanza con reglas (c/qu/g/gu/gü/ll/rr/x/y,
   h muda, diptongos e hiatos por tilde). Variante: seseo y yeísmo (rioplatense /
   latinoamericana): c,z -> s ; ll,y -> y.

   NO va en la tarea de audio: se llama desde la capa de control (serial/web) y
   el resultado (VocalLyrics, un struct plano) se copia al motor.
   ============================================================================ */
#pragma once
#include <stdint.h>

// Fonemas. Las vocales van primero (VP_A..VP_U = 1..5, mismo orden que el morph).
enum VocalPhon : uint8_t {
  VP_NONE = 0,
  VP_A, VP_E, VP_I, VP_O, VP_U,
  VP_P, VP_B, VP_T, VP_D, VP_K, VP_G,     // oclusivas
  VP_F, VP_S, VP_X,                        // fricativas (X = j / g de "gente")
  VP_CH,                                   // africada
  VP_M, VP_N, VP_NY,                       // nasales (NY = ñ)
  VP_L, VP_R, VP_RR,                       // líquidas (R = vibrante simple, RR = múltiple)
  VP_Y,                                    // y / ll
  VP_COUNT
};
static inline bool vocalIsVowel(uint8_t p) { return p >= VP_A && p <= VP_U; }

#define VOCAL_SYL_MAX   48    // sílabas por letra (lo que sobra se descarta)
#define VOCAL_ONSET_MAX 3
#define VOCAL_NUC_MAX   3
#define VOCAL_CODA_MAX  2
#define VOCAL_TEXT_MAX  96    // bytes del texto que guarda el firmware (con '\0')

// Una sílaba: ataque (consonantes), núcleo (vocal con sus deslizadas) y coda.
struct VocalSyl {
  uint8_t onset[VOCAL_ONSET_MAX];   // VP_* o VP_NONE
  uint8_t nuc[VOCAL_NUC_MAX];       // vocales en orden (diptongo/triptongo)
  uint8_t main;                     // índice en nuc[] de la vocal que se sostiene
  uint8_t coda[VOCAL_CODA_MAX];
};
struct VocalLyrics {
  uint8_t  count;                   // 0 = sin letra
  VocalSyl syl[VOCAL_SYL_MAX];
};

// Bits extra que vocalTextToPhonemes agrega a un fonema.
#define VOCAL_PH_MASK    0x1F
#define VOCAL_PH_ACCENT  0x40       // vocal con tilde (rompe el diptongo: pa-ís)
#define VOCAL_PH_WORDEND 0x80       // último fonema de la palabra

// Texto UTF-8 -> fonemas (con los bits de arriba). Devuelve la cantidad.
// También acepta escapes ASCII: ~n = ñ, 'a 'e 'i 'o 'u = vocal con tilde, :u = ü.
int vocalTextToPhonemes(const char* utf8, uint8_t* out, int max);
// Fonemas -> sílabas. Devuelve la cantidad de sílabas.
int vocalSyllabify(const uint8_t* ph, int n, VocalLyrics* out);
// Atajo: texto -> letra silabeada.
int vocalTextToLyrics(const char* utf8, VocalLyrics* out);
// Nombre corto del fonema ("a", "ch", "rr"...), para tests y UI.
const char* vocalPhonName(uint8_t p);
// Escribe una sílaba como texto ("tra", "kons"). Devuelve el largo.
int vocalSylToText(const VocalSyl& s, char* buf, int max);
