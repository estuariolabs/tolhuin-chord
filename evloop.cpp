// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   evloop.cpp  -  Looper de eventos (ver evloop.h). Puro: compila en host.
   ============================================================================ */
#include "evloop.h"
#include <math.h>

static int         gState = EVL_EMPTY;
static EvLoopEvent gEv[EVLOOP_MAX_EVENTS];
static int         gN = 0;
static uint16_t    gLen = 0;          // largo del loop en pasos (0 = sin loop)
static float       gOrigin = 0.0f;    // pos absoluta del paso 0 del loop
static bool        gRecStarted = false; // ya llegó el primer evento de la toma
static long        gLastTotal = -1;   // último paso entero procesado por el tick
static bool        gOverdub = false;  // PLAY + grabando capas nuevas encima

void evloopReset() {
  gState = EVL_EMPTY; gN = 0; gLen = 0; gRecStarted = false; gLastTotal = -1;
  gOverdub = false;
}

int      evloopState() { return gState; }
uint16_t evloopLen()   { return gLen; }
int      evloopCount() { return gN; }

// Paso entero más cercano a `pos` (cuantización).
static long qstep(float pos) { return (long)floorf(pos + 0.5f); }

// Cierra la toma: fija el largo (compás entero más cercano, mínimo 1), envuelve
// los pasos al largo, ordena (estable) y pasa a PLAY sin cortar la fase.
static void recClose(float pos) {
  if (!gRecStarted || gN == 0) { evloopReset(); return; }   // toma vacía
  long bars = (long)floorf((pos - gOrigin) / (float)EVLOOP_BAR_STEPS + 0.5f);
  if (bars < 1) bars = 1;
  if (bars > EVLOOP_MAX_BARS) bars = EVLOOP_MAX_BARS;
  gLen = (uint16_t)(bars * EVLOOP_BAR_STEPS);
  for (int i = 0; i < gN; i++) gEv[i].step = (uint16_t)(gEv[i].step % gLen);
  // Insertion sort ESTABLE por paso: si un off y el on siguiente cayeron en el
  // mismo paso, conserva el orden temporal (off primero -> el on re-dispara).
  for (int i = 1; i < gN; i++) {
    EvLoopEvent e = gEv[i]; int j = i - 1;
    while (j >= 0 && gEv[j].step > e.step) { gEv[j + 1] = gEv[j]; j--; }
    gEv[j + 1] = e;
  }
  gState = EVL_PLAY;
  // La fase sigue de largo (el loop queda alineado a SU propia grilla): el tick
  // arranca desde el paso actual, sin re-disparar lo que quedó atrás.
  gLastTotal = (long)floorf(pos - gOrigin);
}

void evloopRecToggle(float pos) {
  if (gState == EVL_EMPTY) { gState = EVL_REC; gRecStarted = false; gN = 0; gLen = 0; }
  else if (gState == EVL_REC) recClose(pos);
  else if (gState == EVL_PLAY) gOverdub = !gOverdub;   // capas nuevas sobre el loop
}

bool evloopOverdub() { return gState == EVL_PLAY && gOverdub; }

void evloopPlayToggle(float pos) {
  if (gState == EVL_PLAY) gState = EVL_STOP;
  else if (gState == EVL_STOP) {
    gState = EVL_PLAY;
    gOrigin = pos;             // reanuda desde el paso 0
    gLastTotal = -1;           // -1 -> el primer tick dispara el paso 0
  }
}

void evloopClear() { evloopReset(); }

void evloopRecord(float pos, uint8_t type, uint8_t degree, uint8_t zone,
                  uint8_t qual, int8_t bassDegree) {
  // OVERDUB: con el loop sonando, el evento entra cuantizado al largo existente
  // (insertado en orden). El tick lo dispara desde la próxima vuelta.
  if (gState == EVL_PLAY && gOverdub && gLen > 0) {
    if (gN >= EVLOOP_MAX_EVENTS) return;
    long q = qstep(pos);
    uint16_t rel = (uint16_t)((((q - (long)gOrigin) % (long)gLen) + gLen) % gLen);
    int j = gN++;
    while (j > 0 && gEv[j - 1].step > rel) { gEv[j] = gEv[j - 1]; j--; }
    gEv[j] = { rel, type, degree, zone, qual, bassDegree };
    return;
  }
  if (gState != EVL_REC) return;
  long q = qstep(pos);
  if (!gRecStarted) { gOrigin = (float)q; gRecStarted = true; }
  long rel = q - (long)gOrigin;
  if (rel < 0) rel = 0;
  if (rel >= (long)(EVLOOP_MAX_BARS * EVLOOP_BAR_STEPS) || gN >= EVLOOP_MAX_EVENTS) {
    recClose(pos);             // tope de largo o de eventos: cierra sola
    return;
  }
  gEv[gN++] = { (uint16_t)rel, type, degree, zone, qual, bassDegree };
}

int evloopTick(float pos, EvLoopEvent* out, int maxOut) {
  if (gState != EVL_PLAY || gLen == 0) return 0;
  long total = (long)floorf(pos - gOrigin);
  if (total - gLastTotal > (long)gLen) gLastTotal = total - (long)gLen;  // salto grande: 1 vuelta
  int w = 0;
  for (long t = gLastTotal + 1; t <= total && w < maxOut; t++) {
    uint16_t s = (uint16_t)(((t % (long)gLen) + gLen) % gLen);
    for (int i = 0; i < gN && w < maxOut; i++)
      if (gEv[i].step == s) out[w++] = gEv[i];
  }
  gLastTotal = total;
  return w;
}

uint16_t evloopRecSteps(float pos) {
  if (gState != EVL_REC || !gRecStarted) return 0;
  float d = pos - gOrigin;
  return (d > 0.0f) ? (uint16_t)d : 0;
}

float evloopPhase(float pos) {
  if (gLen == 0 || gState == EVL_EMPTY) return 0.0f;
  float ph = fmodf(pos - gOrigin, (float)gLen) / (float)gLen;
  if (ph < 0) ph += 1.0f;
  return ph;
}
