// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   oled.h  -  Pantalla de estado en OLED SSD1306 128x64 (I2C, comparte bus con
   el ADS del joystick). Muestra tonalidad/modo, acorde actual (con color y
   bajo/slash), timbre, voicing, preset, octava, BPM y flags (HOLD/GLIDE/MONO/ARP).
   Capa de hardware. NADIE más incluye Adafruit_SSD1306.
   ============================================================================ */
#pragma once
#include <stdint.h>
#include "state.h"

bool oledInit();                                          // false si no responde el panel
// level 0..1 (silencio del osciloscopio). stepPos = posición musical en pasos
// de semicorchea (loopPos() del .ino). chordActive = hay acorde sonando
// (invierte su casillero en la fila de grados).
void oledRender(const AppState& app, const char* timbre,
                bool bleConnected, float level, float stepPos, bool chordActive);
// Menú de config AGRUPADO: título de página + hasta 3 filas "NOMBRE valor";
// la fila `sel` se dibuja invertida (banda blanca). names[i] son etiquetas
// cortas (<=3 chars) y vals[i] valores (<=6 chars visibles).
// `icon` (opcional): bitmap 12x12 de la página (icons.h, IC_PAGE[pg]).
void oledFxMenuList(const char* title, int page, int pages,
                    const char* const* names, const char* const* vals,
                    int nRows, int sel, const uint8_t* icon = nullptr);
// --- Animación de arranque (llamar en un loop corto no bloqueante) ---
// Frame `f` (0..8) del zoom Argentina -> Tierra del Fuego (333 ms por frame).
void oledBootZoom(uint8_t f);
// Cartel final: TOLHUIN grande + "mk1" en caja invertida + "v2026".
void oledBootTitle();
