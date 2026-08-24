/* ============================================================================
   input.h  -  Lectura de controles físicos (capa de hardware).
   7 botones de grado diatónico + HOLD + VOICING + NAV-/NAV+ (navegación y
   edición del menú de config) + joystick (color) con su click:
     - click sin grado apretado  -> alterna modo mayor/menor
     - click con un grado apretado -> mayoriza ese grado (dom7 <-> diatónico)
   Botones activo-bajo (INPUT_PULLUP). Joystick HW-504 vía ADS1115 (opcional:
   si el ADS no está, el color queda en CENTRO y todo lo demás funciona igual).
   ============================================================================ */
#pragma once
#include <stdint.h>
#include "state.h"   // ColorZone

void inputInit();
void inputUpdate();   // llamar una vez por loop()

int     inputPressedDegree(); // grado 0..6 con flanco de pulsación, o -1
bool    inputAllReleased();   // true cuando se sueltan TODOS los botones de grado
uint8_t inputHeldMask();      // bitmask de los grados sostenidos (bit i = grado i)

ColorZone inputColor();       // zona de color actual del joystick
bool inputColorChanged();     // true una vez cuando la zona cambia

int  inputHoldEvent();        // 0 nada, 1 TAP (toggle HOLD), 2 LARGO (toggle STRUM)
int  inputVoicingEvent();     // 0 nada, 1 TAP (inversión), 2 LARGO (arp)  [libre del menú]
int  inputNavDelta();         // -1 (NAV-), +1 (NAV+), 0  -> mueve la selección del menú
bool inputSwHeld();           // joystick SW apretado (mientras tanto el stick no colorea)
bool inputJoySwPressed();     // click del SW (press+release): mayoriza grado / alterna modo

// --- Navegación del menú v3 (NAV con funciones fijas + D-pad con el joystick) ---
// NAV+ (derecha) entra/sale del menú ; NAV- (izquierda) togglea drums. Dentro del
// menú, el joystick es un D-pad: ↑/↓ mueven la selección, ←/→ editan (auto-repeat).
void inputSetMenuMode(bool on);   // el .ino avisa si el menú está visible
bool inputNavMenuToggle();        // true una vez al apretar NAV+ (entra/sale del menú)
bool inputNavDrumsToggle();       // true una vez al apretar NAV- (drums on/off)
int  inputMenuNav();              // joystick ↑/↓ en menú: -1 sube / +1 baja / 0 (auto-repeat)
int  inputMenuEdit();             // joystick ←/→ en menú: -1 / +1 / 0 (auto-repeat)
