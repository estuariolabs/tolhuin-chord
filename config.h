/* ============================================================================
   config.h  -  Pines, direcciones y constantes globales. Sin lógica.
   Host-safe: sólo #defines y stdint (lo incluye también el núcleo DSP en host).
   ============================================================================ */
#pragma once
#include <stdint.h>

// ---- I2S (PCM5102A) ----  (SCK del DAC físicamente a GND)
// ESP32-S3 SuperMini: 38/39/40 estan en la fila inferior (pads accesibles para soldar).
#define PIN_I2S_BCK    38
#define PIN_I2S_WS     39
#define PIN_I2S_DOUT   40

// ---- I2C (OLED 0x3C + ADS1115 0x48) ----
// ESP32-S3 SuperMini: 41/42 en la fila inferior (accesibles).
#define PIN_I2C_SDA    41
#define PIN_I2C_SCL    42
#define OLED_ADDR_A    0x3C
#define OLED_ADDR_B    0x3D
#define OLED_W         128
#define OLED_H         64

// ---- Botones (INPUT_PULLUP, activo-bajo) ----
#define PIN_CHORD_COUNT 7
// ESP32-S3 SuperMini (Waveshare). Audio (38/39/40) e I2C (41/42) NO cambian.
// Teclado según el cableado FÍSICO confirmado por el usuario:
//   I=8, II=9, III=17, IV=45, V=5, VI=14, VII=10 ; HOLD=18.
// GPIO45 (Tecla IV) es pin de strapping: SEGURO como botón a GND (sin pull-up externo).
// HOLD está en GPIO18 (cableado original) en la placa vigente: la REV 1 es el
// build por defecto. La REV 2 (desvío a GPIO6) quedó para el prototipo del case
// nuevo que tenía el pad/pista de GPIO18 roto; se selecciona por flag de build:
//   compiler.cpp.extra_flags=-DTOLHUIN_HW_REV=2
#ifndef TOLHUIN_HW_REV
#define TOLHUIN_HW_REV 1
#endif
static const uint8_t PIN_CHORD[PIN_CHORD_COUNT] = {8, 9, 17, 45, 5, 14, 10}; // grados I..vii
#define PIN_NAV_MINUS  11      // NAV- : tónica -1 semitono
#define PIN_NAV_PLUS   12      // NAV+ : tónica +1 semitono
#if TOLHUIN_HW_REV == 1
#define PIN_HOLD       18      // prototipo 1: cableado físico original
#else
#define PIN_HOLD       6       // prototipo 2: pad GPIO18 dañado, desvío a GPIO6
#endif
#define PIN_VOICING    7       // VOICING: cicla disposición (inversiones)
#define PIN_JOY_SW     13      // click del joystick (TAP=timbre / sostenido+gesto=modo)

// ---- Joystick (HW-504 vía ADS1115: A0=VRx, A1=VRy) ----
// En esta placa el pin ADDR del ADS está a VDD -> dirección 0x49 (no 0x48).
#define ADS_ADDR           0x49
#define JOY_CENTER_DEFAULT 13200 // mitad de escala (fallback si falla la calibración)
#define JOY_DEADZONE_IN    3000  // radio para VOLVER a centro
#define JOY_DEADZONE_OUT   5500  // radio para SALIR de centro (histéresis radial)
#define JOY_STABLE_READS   3     // lecturas consecutivas para confirmar zona
// Orientación física (la auto-calibración sólo ajusta centro/escala, NO la
// dirección). Poné a 1 si una dirección quedó invertida en la placa; SWAP si los
// ejes X/Y están cruzados. Se corrige por software sin recablear.
// Montaje actual: pines hacia abajo -> hay que "girar" el stick 90°. Se corrige
// con SWAP (ejes cruzados) + una inversión. Si tras flashear una dirección queda
// espejada, alterná cuál INVERT está en 1 (ver nota abajo).
#define JOY_INVERT_X       0     // 1 = invierte izquierda/derecha (izq/der estaban al revés -> 0)
#define JOY_INVERT_Y       0     // 1 = invierte arriba/abajo (arriba/abajo OK)
#define JOY_SWAP_XY        1     // 1 = intercambia los ejes X e Y (rotación 90°)

// ---- Audio / motor propio (DSP) ----
#define SAMPLE_RATE     44100u   // mayor brillo / mejor para anti-aliasing (Fase 1)
#define AUDIO_BLOCK     256u     // muestras por bloque de render
#define MAX_VOICES      12       // polifonía real (con cola de release)
#define MASTER_GAIN     0.20f    // headroom global (evita saturar acordes densos)

// ---- Armonía ----
#define MAX_CHORD_NOTES 8        // tope de notas simultáneas por acorde

// ---- Looper (audio, buffers en PSRAM) ----
#define LOOPER_LAYERS      4     // capas independientes
#define LOOPER_MAX_SECONDS 8     // duración máxima por capa (8 s x 4 x int16 ≈ 2.7 MB)

// ---- Registro útil (MIDI) ----
#define MIDI_LO         36       // C2
#define MIDI_HI         91       // G6
