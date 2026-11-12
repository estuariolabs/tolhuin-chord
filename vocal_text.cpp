/* ============================================================================
   vocal_text.cpp  -  Reglas de pronunciación y silabeo del español (puro).
   ============================================================================ */
#include <string.h>
#include "vocal_text.h"

// Letras internas tras normalizar el UTF-8: a-z más unos códigos propios.
enum : uint8_t { L_SEP = 0, L_NY = 1, L_UU = 2,          // ñ, ü
                 L_AA = 3, L_EA = 4, L_IA = 5, L_OA = 6, L_UA = 7 };   // á é í ó ú

// Lee un carácter UTF-8 y lo normaliza. Avanza *p. Devuelve L_* o 'a'..'z'.
static uint8_t nextLetter(const char*& p) {
  uint8_t c = (uint8_t)*p++;
  // Escapes ASCII (el SysEx MIDI del editor web es de 7 bits): ~n = ñ, 'a = á, :u = ü.
  if (c == '~' && (*p == 'n' || *p == 'N')) { p++; return L_NY; }
  if (c == ':' && (*p == 'u' || *p == 'U')) { p++; return L_UU; }
  if (c == '\'') {
    switch (*p) {
      case 'a': case 'A': p++; return L_AA;  case 'e': case 'E': p++; return L_EA;
      case 'i': case 'I': p++; return L_IA;  case 'o': case 'O': p++; return L_OA;
      case 'u': case 'U': p++; return L_UA;
    }
    return L_SEP;
  }
  if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
  if (c >= 'a' && c <= 'z') return c;
  if (c == 0xC3 && *p) {                       // Latin-1 suplementario (2 bytes)
    uint8_t d = (uint8_t)*p++;
    switch (d) {
      case 0xA1: case 0x81: return L_AA;       // á Á
      case 0xA9: case 0x89: return L_EA;       // é É
      case 0xAD: case 0x8D: return L_IA;       // í Í
      case 0xB3: case 0x93: return L_OA;       // ó Ó
      case 0xBA: case 0x9A: return L_UA;       // ú Ú
      case 0xBC: case 0x9C: return L_UU;       // ü Ü
      case 0xB1: case 0x91: return L_NY;       // ñ Ñ
    }
    return L_SEP;
  }
  if (c >= 0x80) {                             // otro multibyte: saltar las continuaciones
    while (((uint8_t)*p & 0xC0) == 0x80) p++;
  }
  return L_SEP;                                // espacio, dígito, puntuación...
}

static uint8_t vowelOf(uint8_t l) {            // letra -> fonema vocal (con tilde) o 0
  switch (l) {
    case 'a': return VP_A; case 'e': return VP_E; case 'i': return VP_I;
    case 'o': return VP_O; case 'u': return VP_U;
    case L_AA: return VP_A | VOCAL_PH_ACCENT; case L_EA: return VP_E | VOCAL_PH_ACCENT;
    case L_IA: return VP_I | VOCAL_PH_ACCENT; case L_OA: return VP_O | VOCAL_PH_ACCENT;
    case L_UA: return VP_U | VOCAL_PH_ACCENT; case L_UU: return VP_U;
  }
  return 0;
}
static bool isFront(uint8_t l) { return l == 'e' || l == 'i' || l == L_EA || l == L_IA; }

int vocalTextToPhonemes(const char* utf8, uint8_t* out, int max) {
  if (!utf8 || !out || max <= 0) return 0;
  int n = 0;
  const char* p = utf8;
  uint8_t word[40];
  while (*p) {
    // --- junta una palabra de letras normalizadas ---
    int wl = 0;
    while (*p) {
      uint8_t l = nextLetter(p);
      if (l == L_SEP) { if (wl) break; else continue; }
      if (wl < (int)sizeof(word) - 1) word[wl++] = l;
    }
    if (!wl) break;
    word[wl] = L_SEP;
    int start = n;
    auto emit = [&](uint8_t ph) { if (n < max) out[n++] = ph; };
    // --- reglas, de izquierda a derecha ---
    for (int i = 0; i < wl; i++) {
      uint8_t l = word[i], nx = word[i + 1], nx2 = (i + 2 <= wl) ? word[i + 2] : (uint8_t)L_SEP;
      uint8_t v = vowelOf(l);
      if (v) { emit(v); continue; }
      switch (l) {
        case 'h': break;                                          // muda
        case 'b': case 'v': emit(VP_B); break;
        case 'c':
          if (nx == 'h') { emit(VP_CH); i++; }
          else if (isFront(nx)) emit(VP_S);                       // ce, ci (seseo)
          else emit(VP_K);
          break;
        case 'z': case 's': emit(VP_S); break;
        case 'q': emit(VP_K); if (nx == 'u') i++; break;          // qu -> k (u muda)
        case 'k': emit(VP_K); break;
        case 'g':
          if (isFront(nx)) emit(VP_X);                            // ge, gi
          else { emit(VP_G); if (nx == 'u' && isFront(nx2)) i++; } // gue, gui: u muda
          break;                                                  // (güe: la ü es L_UU y suena)
        case 'j': emit(VP_X); break;
        case 'x':
          if (i == 0) emit(VP_S); else { emit(VP_K); emit(VP_S); }
          break;
        case 'l':
          if (nx == 'l') { emit(VP_Y); i++; } else emit(VP_L);
          break;
        case 'y':                                                 // consonante sólo ante vocal
          if (vowelOf(nx)) emit(VP_Y); else emit(VP_I);
          break;
        case 'r':
          if (nx == 'r') { emit(VP_RR); i++; }
          else if (i == 0 || word[i - 1] == 'n' || word[i - 1] == 'l' || word[i - 1] == 's') emit(VP_RR);
          else emit(VP_R);
          break;
        case 'w': emit(VP_U); break;
        case 'd': emit(VP_D); break;  case 'f': emit(VP_F); break;
        case 'm': emit(VP_M); break;  case 'n': emit(VP_N); break;
        case 'p': emit(VP_P); break;  case 't': emit(VP_T); break;
        case L_NY: emit(VP_NY); break;
        default: break;
      }
    }
    if (n > start) out[n - 1] |= VOCAL_PH_WORDEND;
    if (n >= max) break;
  }
  return n;
}

// ¿c1+c2 pueden empezar sílaba juntas? (pl pr bl br fl fr cl cr gl gr tr dr)
static bool isCluster(uint8_t c1, uint8_t c2) {
  if (c2 == VP_R) return c1 == VP_P || c1 == VP_B || c1 == VP_F || c1 == VP_K ||
                         c1 == VP_G || c1 == VP_T || c1 == VP_D;
  if (c2 == VP_L) return c1 == VP_P || c1 == VP_B || c1 == VP_F || c1 == VP_K || c1 == VP_G;
  return false;
}
static bool isWeak(uint8_t ph) {               // i/u SIN tilde: pueden ser deslizadas
  uint8_t b = ph & VOCAL_PH_MASK;
  return (b == VP_I || b == VP_U) && !(ph & VOCAL_PH_ACCENT);
}

int vocalSyllabify(const uint8_t* ph, int n, VocalLyrics* out) {
  if (!out) return 0;
  memset(out, 0, sizeof(*out));
  if (!ph) return 0;
  int i = 0;
  while (i < n) {
    // --- límites de la palabra [i, e) ---
    int e = i;
    while (e < n && !(ph[e] & VOCAL_PH_WORDEND)) e++;
    if (e < n) e++;
    // --- núcleos: grupos de vocales que forman una sola sílaba ---
    int nucS[24], nucE[24], nn = 0;
    for (int k = i; k < e && nn < 24; ) {
      if (!vocalIsVowel(ph[k] & VOCAL_PH_MASK)) { k++; continue; }
      int s = k; bool strong = !isWeak(ph[k]);
      k++;
      // suma vocales mientras siga siendo diptongo/triptongo (a lo sumo UNA fuerte)
      while (k < e && vocalIsVowel(ph[k] & VOCAL_PH_MASK) && (k - s) < VOCAL_NUC_MAX) {
        bool w = isWeak(ph[k]);
        if (!w && strong) break;               // dos fuertes (o débil con tilde) = hiato
        // débil + débil + ... : sólo se admite una tercera si hay una fuerte en medio
        if (w && !strong && (k - s) >= 2) break;
        if (!w) strong = true;
        k++;
      }
      nucS[nn] = s; nucE[nn] = k; nn++;
    }
    if (nn == 0) { i = e; continue; }          // palabra sin vocal ("y" suelta ya es vocal)
    // --- reparte las consonantes entre núcleos ---
    int onsetStart = i;
    for (int q = 0; q < nn && out->count < VOCAL_SYL_MAX; q++) {
      VocalSyl& sy = out->syl[out->count];
      int codaEnd;                             // fin (exclusivo) de la coda de esta sílaba
      if (q == nn - 1) codaEnd = e;
      else {
        int cs = nucE[q], ce = nucS[q + 1], nc = ce - cs;
        int toNext = 0;
        if (nc == 1) toNext = 1;
        else if (nc >= 2) toNext = isCluster(ph[ce - 2] & VOCAL_PH_MASK, ph[ce - 1] & VOCAL_PH_MASK) ? 2 : 1;
        codaEnd = ce - toNext;
      }
      int o = 0;
      for (int k = onsetStart; k < nucS[q]; k++)
        if (o < VOCAL_ONSET_MAX) sy.onset[o++] = ph[k] & VOCAL_PH_MASK;
      int v = 0, mainIx = 0; bool gotStrong = false;
      for (int k = nucS[q]; k < nucE[q]; k++, v++) {
        sy.nuc[v] = ph[k] & VOCAL_PH_MASK;
        if (!isWeak(ph[k]) && !gotStrong) { mainIx = v; gotStrong = true; }
      }
      if (!gotStrong) mainIx = v - 1;          // débil+débil: manda la segunda (rui-do, viu-da)
      sy.main = (uint8_t)mainIx;
      int c = 0;
      for (int k = nucE[q]; k < codaEnd; k++)
        if (c < VOCAL_CODA_MAX) sy.coda[c++] = ph[k] & VOCAL_PH_MASK;
      out->count++;
      onsetStart = codaEnd;
    }
    i = e;
  }
  return out->count;
}

int vocalTextToLyrics(const char* utf8, VocalLyrics* out) {
  uint8_t ph[VOCAL_SYL_MAX * 4];
  int n = vocalTextToPhonemes(utf8, ph, (int)sizeof(ph));
  return vocalSyllabify(ph, n, out);
}

const char* vocalPhonName(uint8_t p) {
  static const char* N[VP_COUNT] = { "", "a", "e", "i", "o", "u", "p", "b", "t", "d", "k", "g",
                                     "f", "s", "j", "ch", "m", "n", "ny", "l", "r", "rr", "y" };
  p &= VOCAL_PH_MASK;
  return (p < VP_COUNT) ? N[p] : "";
}

int vocalSylToText(const VocalSyl& s, char* buf, int max) {
  int n = 0;
  auto add = [&](uint8_t p) {
    for (const char* q = vocalPhonName(p); *q && n < max - 1; q++) buf[n++] = *q;
  };
  for (int i = 0; i < VOCAL_ONSET_MAX; i++) if (s.onset[i]) add(s.onset[i]);
  for (int i = 0; i < VOCAL_NUC_MAX; i++)   if (s.nuc[i])   add(s.nuc[i]);
  for (int i = 0; i < VOCAL_CODA_MAX; i++)  if (s.coda[i])  add(s.coda[i]);
  if (max > 0) buf[n] = 0;
  return n;
}
