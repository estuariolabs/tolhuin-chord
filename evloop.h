/* ============================================================================
   evloop.h  -  LOOPER DE EVENTOS (1 pista de acordes, cuantizado).
   Graba EVENTOS de acorde (grado/zona/calidad/bajo), no audio: un loop de
   4 compases son decenas de bytes -> anda en la S3 SuperMini SIN PSRAM.
   La cuantización sale gratis: cada evento se ajusta al paso de semicorchea
   más cercano de la misma grilla que la batería (16 pasos por compás 4/4).
   El playback re-dispara los acordes por el camino normal (playCurrent), así
   que se puede cambiar timbre/tonalidad/octava CON el loop andando.

   PURO: sin Arduino ni FreeRTOS; compila en host (g++) y se testea en
   test/test_evloop.cpp. El tiempo entra desde afuera como `pos` = posición
   monotónica en PASOS (float, semicorcheas), que el .ino deriva del contador
   de muestras del audio (mismo reloj que la batería -> quedan en fase).
   ============================================================================ */
#pragma once
#include <stdint.h>

#define EVLOOP_MAX_EVENTS 64   // eventos por loop (sobra: 2 por acorde)
#define EVLOOP_BAR_STEPS  16   // semicorcheas por compás (4/4)
#define EVLOOP_MAX_BARS   8    // tope de largo: 8 compases

// Estados del transporte.
enum EvLoopState { EVL_EMPTY = 0, EVL_REC, EVL_PLAY, EVL_STOP };
// Tipos de evento. Los MEL_* son la capa de MELODÍA (overdub): `degree` lleva
// la NOTA MIDI absoluta (capturada al grabar -> inmune a cambios de octava).
enum EvType { EV_CHORD_ON = 0, EV_CHORD_OFF, EV_MEL_ON, EV_MEL_OFF };

// Un evento del loop. `step` es relativo al inicio del loop (0..len-1).
struct EvLoopEvent {
  uint16_t step;
  uint8_t  type;        // EvType
  uint8_t  degree;      // 0..6 (acordes) o NOTA MIDI (melodía)
  uint8_t  zone;        // ColorZone
  uint8_t  qual;        // ChordQual del grado (mayorización)
  int8_t   bassDegree;  // -1 = sin bajo slash
};

void     evloopReset();               // estado de fábrica (EMPTY, sin eventos)
int      evloopState();               // EvLoopState actual
uint16_t evloopLen();                 // largo del loop en pasos (0 = sin loop)
int      evloopCount();               // cantidad de eventos grabados

// REC: desde EMPTY arma la grabación (el paso 0 será el PRIMER evento que
// llegue). Desde REC cierra: el largo se redondea al compás entero más
// cercano (mínimo 1) y pasa a PLAY arrancando en el paso 0.
// Desde PLAY alterna el OVERDUB: grabar capas nuevas (melodía / más acordes)
// SOBRE el loop que suena, cuantizadas al mismo largo.
void evloopRecToggle(float pos);
bool evloopOverdub();                 // true = PLAY con overdub armado
// PLAY <-> STOP (sólo con contenido). Al reanudar arranca desde el paso 0.
void evloopPlayToggle(float pos);
void evloopClear();                   // vuelve a EMPTY (descarta todo)

// Graba un evento en `pos` (cuantiza al paso más cercano). Sólo en REC.
// Si la grabación llegó al tope (EVLOOP_MAX_BARS), cierra sola y pasa a PLAY.
void evloopRecord(float pos, uint8_t type, uint8_t degree, uint8_t zone,
                  uint8_t qual, int8_t bassDegree);

// Avanza el playback hasta `pos` y devuelve (en `out`) los eventos cuyos pasos
// se cruzaron desde el último tick, en orden. Maneja la vuelta del loop.
// Devuelve la cantidad de eventos escritos (0 si no está en PLAY).
int evloopTick(float pos, EvLoopEvent* out, int maxOut);

// Posición dentro del loop 0..1 (para UI). 0 si no hay loop andando.
float evloopPhase(float pos);

// Pasos transcurridos de la toma en curso (0 si no está en REC o aún no llegó
// el primer evento). Para el feedback en vivo de la pantalla ("c2...").
uint16_t evloopRecSteps(float pos);
