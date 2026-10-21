// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   test_evloop.cpp  -  Tests del looper de eventos (evloop.cpp) en host.
   Deterministas: el tiempo es un float en PASOS que controla el test.
   ============================================================================ */
#include <cstdio>
#include <cstring>
#include "evloop.h"

static int checks = 0, fails = 0;
static void ok(bool cond, const char* msg) {
  checks++;
  if (!cond) { fails++; printf("  FALLA: %s\n", msg); }
}

// Junta todos los eventos que dispara el tick avanzando de a incrementos chicos
// entre pos0 y pos1 (simula el poll del loop() del firmware).
static int collect(float pos0, float pos1, EvLoopEvent* out, int maxOut) {
  int n = 0;
  for (float p = pos0; p <= pos1 + 1e-4f; p += 0.05f) {
    EvLoopEvent tmp[16];
    int k = evloopTick(p, tmp, 16);
    for (int i = 0; i < k && n < maxOut; i++) out[n++] = tmp[i];
  }
  return n;
}

int main() {
  // --- estado inicial y máquina de estados básica ---
  evloopReset();
  ok(evloopState() == EVL_EMPTY, "arranca EMPTY");
  ok(evloopLen() == 0, "sin largo al arrancar");
  ok(evloopTick(10.0f, nullptr, 0) == 0, "tick en EMPTY no dispara nada");

  // REC sin eventos y cerrar -> vuelve a EMPTY (toma vacía)
  evloopRecToggle(0.0f);
  ok(evloopState() == EVL_REC, "RecToggle desde EMPTY -> REC");
  evloopRecToggle(20.0f);
  ok(evloopState() == EVL_EMPTY, "cerrar toma vacía -> EMPTY");

  // --- grabación con cuantización ---
  // Toma: I en 100.1 (paso 0), IV en 104.6 (paso ~105-100=5... redondea a 5),
  // off en 107.9 (paso 8), V en 111.2 (paso 11), off en 114.8 (paso 15).
  // Cierre en 116.2 -> 16.2 pasos = 1 compás (16).
  evloopRecToggle(99.0f);
  evloopRecord(100.1f, EV_CHORD_ON, 0, 0, 0, -1);
  evloopRecord(104.6f, EV_CHORD_ON, 3, 2, 0, -1);
  evloopRecord(107.9f, EV_CHORD_OFF, 0, 0, 0, -1);
  evloopRecord(111.2f, EV_CHORD_ON, 4, 0, 1, 2);
  evloopRecord(114.8f, EV_CHORD_OFF, 0, 0, 0, -1);
  ok(evloopCount() == 5, "5 eventos grabados");
  evloopRecToggle(116.2f);
  ok(evloopState() == EVL_PLAY, "cerrar toma -> PLAY");
  ok(evloopLen() == 16, "largo redondeado a 1 compás (16 pasos)");

  // Los pasos quedaron cuantizados al más cercano. Tras el cierre la fase SIGUE
  // de largo (no re-dispara lo que quedó atrás): la vuelta va del paso 17 al 32
  // absolutos, así que los eventos salen en orden de fase (5,8,11,15 y luego 0).
  {
    EvLoopEvent ev[16];
    int n = collect(116.3f, 132.6f, ev, 16);
    ok(n == 5, "una vuelta dispara los 5 eventos");
    // Verificación por contenido (búsqueda por paso, no por posición).
    const EvLoopEvent* by[16] = { nullptr };
    for (int i = 0; i < n; i++) by[ev[i].step] = &ev[i];
    ok(by[0] && by[0]->type == EV_CHORD_ON && by[0]->degree == 0,
       "paso 0: ON grado I (100.1 -> paso 0)");
    ok(by[5] && by[5]->degree == 3, "paso 5: ON grado IV (104.6 -> paso 5)");
    ok(by[8] && by[8]->type == EV_CHORD_OFF, "paso 8: OFF (107.9 -> paso 8)");
    ok(by[11] && by[11]->degree == 4 && by[11]->qual == 1 && by[11]->bassDegree == 2,
       "paso 11: ON V con calidad y bajo slash");
    ok(by[15] && by[15]->type == EV_CHORD_OFF, "paso 15: OFF (114.8 -> paso 15)");
  }

  // --- la vuelta siguiente repite lo mismo (loop) ---
  {
    EvLoopEvent ev[16];
    int n = collect(132.7f, 148.6f, ev, 16);
    ok(n == 5, "la vuelta siguiente repite los 5 eventos");
  }

  // --- STOP / PLAY ---
  evloopPlayToggle(150.0f);
  ok(evloopState() == EVL_STOP, "PlayToggle en PLAY -> STOP");
  ok(evloopTick(160.0f, nullptr, 0) == 0, "en STOP el tick no dispara");
  evloopPlayToggle(200.0f);
  ok(evloopState() == EVL_PLAY, "PlayToggle en STOP -> PLAY");
  {
    EvLoopEvent ev[16];
    int n = collect(200.0f, 215.9f, ev, 16);
    ok(n == 5 && ev[0].step == 0, "reanudar arranca desde el paso 0");
  }

  // --- CLEAR ---
  evloopClear();
  ok(evloopState() == EVL_EMPTY && evloopCount() == 0 && evloopLen() == 0,
     "Clear vuelve a EMPTY");

  // --- redondeo del largo a compás más cercano (2 compases) ---
  evloopReset();
  evloopRecToggle(0.0f);
  evloopRecord(10.0f, EV_CHORD_ON, 1, 0, 0, -1);
  evloopRecord(34.0f, EV_CHORD_ON, 2, 0, 0, -1);   // paso 24 (2do compás)
  evloopRecToggle(10.0f + 30.5f);                   // 30.5 pasos -> 2 compases
  ok(evloopLen() == 32, "30.5 pasos redondea a 2 compases (32)");
  {
    EvLoopEvent ev[8];
    int n = collect(40.6f, 74.0f, ev, 8);           // desde el cierre, 2 vueltas aprox
    ok(n >= 2, "los 2 eventos suenan al menos una vez tras el cierre");
  }

  // --- orden estable: OFF y ON cuantizados al mismo paso -> OFF primero ---
  evloopReset();
  evloopRecToggle(0.0f);
  evloopRecord(0.1f,  EV_CHORD_ON,  0, 0, 0, -1);
  evloopRecord(7.6f,  EV_CHORD_OFF, 0, 0, 0, -1);  // paso 8
  evloopRecord(8.2f,  EV_CHORD_ON,  5, 0, 0, -1);  // paso 8 también
  evloopRecToggle(16.0f);
  {
    EvLoopEvent ev[8];
    int n = collect(16.1f, 32.4f, ev, 8);   // vuelta completa tras el cierre
    ok(n == 3, "3 eventos en la vuelta");
    bool ordenOk = n == 3 && ev[0].step == 8 && ev[0].type == EV_CHORD_OFF &&
                   ev[1].step == 8 && ev[1].type == EV_CHORD_ON;
    ok(ordenOk, "mismo paso: OFF antes que ON (orden temporal estable)");
  }

  // --- fase para la UI ---
  evloopReset();
  evloopRecToggle(0.0f);
  evloopRecord(0.0f, EV_CHORD_ON, 0, 0, 0, -1);
  evloopRecToggle(16.0f);
  {
    float ph = evloopPhase(20.0f);   // paso 4 de 16 -> 0.25
    ok(ph > 0.24f && ph < 0.26f, "phase = 0.25 en el paso 4 de 16");
  }

  // --- tope de eventos: cierra sola sin desbordar ---
  evloopReset();
  evloopRecToggle(0.0f);
  for (int i = 0; i < EVLOOP_MAX_EVENTS + 10; i++)
    evloopRecord((float)i * 0.5f, EV_CHORD_ON, (uint8_t)(i % 7), 0, 0, -1);
  ok(evloopState() == EVL_PLAY, "al llenarse la tabla cierra sola y pasa a PLAY");
  ok(evloopCount() <= EVLOOP_MAX_EVENTS, "no desborda la tabla de eventos");

  // --- tope de largo: 8 compases ---
  evloopReset();
  evloopRecToggle(0.0f);
  evloopRecord(0.0f, EV_CHORD_ON, 0, 0, 0, -1);
  evloopRecord(500.0f, EV_CHORD_ON, 1, 0, 0, -1);   // más allá del tope
  ok(evloopState() == EVL_PLAY, "pasarse de 8 compases cierra sola");
  ok(evloopLen() <= EVLOOP_MAX_BARS * EVLOOP_BAR_STEPS, "largo <= 8 compases");

  // --- OVERDUB: capas nuevas sobre el loop en PLAY ---
  evloopReset();
  evloopRecToggle(0.0f);
  evloopRecord(0.0f,  EV_CHORD_ON,  0, 0, 0, -1);
  evloopRecord(15.6f, EV_CHORD_OFF, 0, 0, 0, -1);
  evloopRecToggle(16.0f);                            // cierra: 1 compás, PLAY
  ok(evloopState() == EVL_PLAY && !evloopOverdub(), "PLAY sin overdub tras cerrar");
  evloopRecToggle(20.0f);                            // PLAY -> arma overdub
  ok(evloopOverdub(), "RecToggle en PLAY arma el overdub");
  evloopRecord(20.2f, EV_MEL_ON, 64, 0, 0, -1);      // nota MIDI en el paso 4
  evloopRecord(23.9f, EV_MEL_OFF, 64, 0, 0, -1);     // off en el paso 8 (24 % 16)
  ok(evloopCount() == 4, "el overdub agrega eventos (4 en total)");
  evloopRecToggle(25.0f);                            // desarma el overdub
  ok(evloopState() == EVL_PLAY && !evloopOverdub(), "RecToggle de nuevo lo desarma");
  {
    EvLoopEvent ev[8];
    // Ventana t=17..41: MEL@20, MEL_OFF@24, CHORD_OFF@31, CHORD_ON@32,
    // MEL@36, MEL_OFF@40 -> 6 eventos (vuelta y media desde el cierre).
    int n = collect(25.1f, 41.4f, ev, 8);
    ok(n == 6, "acordes + melodía suenan juntos (6 eventos en t17..41)");
    bool melOk = false;
    for (int i = 0; i < n; i++)
      if (ev[i].type == EV_MEL_ON && ev[i].step == 4 && ev[i].degree == 64) melOk = true;
    ok(melOk, "la nota de melodía quedó en el paso 4 con su MIDI (64)");
  }

  printf("\n%d checks, %d fallas\n", checks, fails);
  return fails ? 1 : 0;
}
