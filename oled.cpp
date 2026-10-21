// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   oled.cpp  -  Pantalla de estado (SSD1306 128x64, I2C). Capa de hardware.
   Layout v5 — DOS COLUMNAS (scope protagonista):
     IZQUIERDA (x0..47):
       · TARJETA DE ACORDE arriba (caja blanca, letra CENTRADA, ancho justo;
         FreeSansBold9pt -> default x2 -> x1 según el largo del nombre).
       · ICONOS DINÁMICOS debajo (SOLO funciones activas, grilla de 2 columnas):
         núcleo REC/PLAY/DRUMS/ARP/HOLD/MONO mientras están activos; las
         secundarias SUSTAIN/GLIDE/TREMOLO/CHORUS/BODY FLASHEAN ~1.2 s al
         togglear y luego se ocultan.
       · FASE del loop: barra fina en la base (y63) cuando el looper anda.
       · Punto BLE en la esquina inf-izq.
     DERECHA (x48..127): OSCILOSCOPIO "OVILLO" (Lissajous XY por time-delay
       embedding) dentro de un MARCO redondeado, ocupando todo el alto.
   Todo lo demás (timbre/KEY/BPM/grados/pasos) vive en el MENÚ.
   ============================================================================ */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold12pt7b.h>   // títulos del menú / boot (splash de pantalla)
#include <Fonts/FreeSansBold9pt7b.h>    // nombre del acorde (entra completo en la fila 0)
#include <string.h>
#include "config.h"
#include "oled.h"
#include "harmony.h"
#include "synth.h"      // synthGetScope: forma de onda para el osciloscopio
#include "evloop.h"     // estado del looper de eventos (iconos REC/LOOP en la fila 0)
#include "icons.h"      // pixel-art 1-bit (generado por tools/gen_icons.py)

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool ok = false;

// --- Texto con FreeFont (baseline en y). IMPORTANTE: setTextSize(1) porque GFX
// también escala las FreeFonts -> si quedó en 2 de otra impresión, salían gigantes.
// Vuelven a la fuente default (size 1) al salir.
static void printCentered(const char* s, int baselineY, const GFXfont* font) {
  oled.setTextSize(1); oled.setFont(font);
  int16_t x1, y1; uint16_t w, h;
  oled.getTextBounds(s, 0, baselineY, &x1, &y1, &w, &h);
  int x = (OLED_W - (int)w) / 2 - x1; if (x < 0) x = 0;
  oled.setCursor(x, baselineY); oled.print(s);
  oled.setFont(NULL);
}
// Alineado a la izquierda en (x, baseline).
static void printLeft(const char* s, int x, int baselineY, const GFXfont* font) {
  oled.setTextSize(1); oled.setFont(font);
  oled.setCursor(x, baselineY); oled.print(s);
  oled.setFont(NULL);
}

bool oledInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);          // I2C rápido: refresco fluido para la animación del VU
  // Sondeo por ACK ANTES de begin(): la librería devuelve true aunque no haya
  // panel (sólo falla si no puede reservar el buffer), y sin esto un armado SIN
  // OLED dibujaría cada cuadro contra el vacío. Así `ok` queda en false y todas
  // las funciones de dibujo salen por su guarda.
  uint8_t addr = 0;
  Wire.beginTransmission(OLED_ADDR_A);
  if (Wire.endTransmission() == 0) addr = OLED_ADDR_A;
  if (!addr) {
    Wire.beginTransmission(OLED_ADDR_B);
    if (Wire.endTransmission() == 0) addr = OLED_ADDR_B;
  }
  ok = addr && oled.begin(SSD1306_SWITCHCAPVCC, addr);
  if (ok) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    printCentered("TOLHUIN", 38, &FreeSansBold12pt7b);
    oled.display();
  }
  Serial.printf("[oled] panel %s\n", ok ? "OK" : "NO detectado");
  return ok;
}

// Tarjeta de acorde v5: caja blanca redondeada en la esquina sup-izq, ancho
// "justo lo necesario" (acotado a maxW) y la letra CENTRADA dentro. Elige la
// fuente para que entre: FreeSansBold9pt -> default x2 -> default x1 (nombres
// largos con slash). Alto fijo 20 px.
static void drawChordCard(const char* sym, int maxW) {
  const int Hc = 20;
  int16_t x1, y1; uint16_t w, h;
  oled.setTextSize(1); oled.setFont(&FreeSansBold9pt7b);
  oled.getTextBounds(sym, 0, 0, &x1, &y1, &w, &h);
  oled.setFont(NULL);
  int len = (int)strlen(sym);
  int fitMode, textW;
  if      ((int)w <= maxW - 4)   { fitMode = 0; textW = (int)w; }      // FreeFont 9pt
  else if (len * 12 <= maxW - 2) { fitMode = 2; textW = len * 12; }    // default x2
  else                           { fitMode = 1; textW = len * 6;  }    // default x1
  int cardW = textW + 6; if (cardW > maxW) cardW = maxW; if (cardW < 16) cardW = 16;
  oled.fillRoundRect(0, 0, cardW, Hc, 3, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  if (fitMode == 0) {                                   // FreeFont: baseline en y=15
    int bx = (cardW - (int)w) / 2 - x1; if (bx < 1) bx = 1;
    printLeft(sym, bx, 15, &FreeSansBold9pt7b);
  } else {                                              // default: cursor arriba-izq
    int chh = (fitMode == 2) ? 16 : 8;
    int bx = (cardW - textW) / 2; if (bx < 1) bx = 1;
    oled.setFont(NULL); oled.setTextSize(fitMode);
    oled.setCursor(bx, (Hc - chh) / 2); oled.print(sym);
    oled.setTextSize(1);
  }
  oled.setTextColor(SSD1306_WHITE);
}

// Osciloscopio v5 en modo XY (Lissajous) — "ovillo de UN período": estima el
// período por dos cruces por cero ASCENDENTES y dibuja UN solo lazo cerrado
// (x = s[i], y = s[i+P/4]) -> figura limpia que cambia de forma con el acorde,
// sin la maraña de varios períodos superpuestos. TUMBA despacio (rotación fija),
// auto-ganancia para llenar el marco, y en silencio un aro chico que respira.
// Se dibuja dentro de un MARCO redondeado (x=sx) a todo lo alto.
static void drawTangle(int sx, float level) {
  const int N = 256, MINP = 24;                        // ring del scope = 256 muestras
  static float buf[256];
  // Marco 1 px hacia adentro arriba: en este panel la fila y=0 no se ve.
  oled.drawRoundRect(sx, 1, OLED_W - sx, OLED_H - 1, 3, SSD1306_WHITE);
  const float cx  = sx + (OLED_W - sx) / 2.0f;
  const float cy  = OLED_H / 2.0f;
  const float amp = 24.0f;
  const int   xlo = sx + 2, xhi = OLED_W - 3, ylo = 3, yhi = OLED_H - 3;

  int P;                                               // muestras del lazo (un período)
  if (level < 0.012f) {                                // idle: aro chico que respira
    float br = 0.30f + 0.08f * sinf((float)millis() * 0.002f);
    for (int i = 0; i < N; i++) buf[i] = br * sinf(i * 0.13f + (float)millis() * 0.003f);
    P = (int)(2.0f * PI / 0.13f);                      // un período del seno idle
  } else {
    synthGetScope(buf, N);
    float peak = 0.05f;
    for (int i = 0; i < N; i++) { float a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > peak) peak = a; }
    float g = (peak > 0.9f) ? 1.0f : 0.9f / peak;
    for (int i = 0; i < N; i++) { buf[i] *= g; if (buf[i] > 1) buf[i] = 1; if (buf[i] < -1) buf[i] = -1; }
    // PERÍODO = distancia entre dos cruces por cero ascendentes (guarda MINP para
    // no engancharse en armónicos). Sin período claro -> ventana fija (~nota grave).
    int c0 = -1; P = 0;
    for (int i = 1; i < N; i++)
      if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) {
        if (c0 < 0) c0 = i;
        else if (i - c0 >= MINP) { P = i - c0; break; }
      }
    if (P < MINP) P = 128;
  }
  int tau = P / 4; if (tau < 3) tau = 3;
  if (P + tau > N - 1) P = N - 1 - tau;                // que el lazo entre en el buffer

  float ang = (float)millis() * 0.00030f;              // tumbado lento del ovillo
  float ca = cosf(ang), sa = sinf(ang);
  int lx = 0, ly = 0, fx = 0, fy = 0; bool first = true;
  for (int i = 0; i <= P; i++) {
    float a = buf[i], b = buf[i + tau];
    float rx = a * ca - b * sa, ry = a * sa + b * ca;
    int x = (int)(cx + rx * amp), y = (int)(cy + ry * amp);
    if (x < xlo) x = xlo; if (x > xhi) x = xhi;
    if (y < ylo) y = ylo; if (y > yhi) y = yhi;
    if (first) { fx = x; fy = y; first = false; }
    else oled.drawLine(lx, ly, x, y, SSD1306_WHITE);
    lx = x; ly = y;
  }
  if (!first) oled.drawLine(lx, ly, fx, fy, SSD1306_WHITE);   // cierra el lazo
}

void oledRender(const AppState& app, const char* timbre, bool bleConnected,
                float level, float stepPos, bool chordActive) {
  if (!ok) return;
  (void)timbre; (void)chordActive;   // v4 minimal: viven en el menú / no se usan

  char sym[20];
  harmonyChordSymbolJoy(app.degree, app.zone, app.tonic, app.mode, app.preset,
                        app.chordQual[app.degree % 7], app.joyMap, sym, sizeof(sym));
  if (app.bassDegree >= 0) {                       // bajo / slash: "<acorde>/<bajo>"
    char b[24];
    snprintf(b, sizeof(b), "%s/%s", sym,
             harmonyRootName((uint8_t)app.bassDegree, app.tonic, app.mode));
    strncpy(sym, b, sizeof(sym)); sym[sizeof(sym) - 1] = 0;
  }
  // La tarjeta muestra el CONTEXTO del canal: en PATRN va el NOMBRE del preset
  // de batería (no un número); en DRUMS, el rótulo del modo.
  if (app.channel == CH_DRUMPAT) {
    strncpy(sym, synthPresetName(0, app.drumPat), sizeof(sym) - 1);
    sym[sizeof(sym) - 1] = 0;
  } else if (app.channel == CH_DRUMS) {
    strcpy(sym, "DRUMS");
  }

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  int st = evloopState();
  const int SX = 48;                       // el marco del scope arranca en x=48

  // === IZQUIERDA — tarjeta de acorde (arriba) =================================
  drawChordCard(sym, SX - 2);

  // === IZQUIERDA — iconos DINÁMICOS (solo funciones activas) ==================
  // Núcleo (visible mientras esté activo): REC, PLAY, DRUMS, ARP, HOLD, MONO.
  // Secundarias (SUSTAIN/GLIDE/TREMOLO/CHORUS/BODY): FLASHEAN ~1.2 s al togglear
  // y luego se ocultan (son "set-and-forget", se escuchan).
  {
    static uint32_t flashUntil[5] = { 0, 0, 0, 0, 0 };
    static bool     secPrev[5]    = { false, false, false, false, false };
    const bool sec[5]   = { app.sustain, app.glide, app.tremMode != 0, app.chorus, app.body };
    const int  secIc[5] = { ICS_SUSTAIN, ICS_GLIDE, ICS_TREMOLO, ICS_CHORUS, ICS_BODY };
    uint32_t now = millis();
    for (int i = 0; i < 5; i++)
      if (sec[i] != secPrev[i]) { flashUntil[i] = now + 1200; secPrev[i] = sec[i]; }

    const uint8_t* ic[12]; int nic = 0;
    if (st == EVL_REC || evloopOverdub()) ic[nic++] = IC_STATE[ICS_REC];
    if (st == EVL_PLAY)                   ic[nic++] = IC_STATE[ICS_LOOP];
    if (app.drumsOn)                      ic[nic++] = IC_STATE[ICS_DRUMS];
    if (app.arpMode != 0)                 ic[nic++] = IC_STATE[ICS_ARP];
    if (app.hold)                         ic[nic++] = IC_STATE[ICS_HOLD];
    if (app.mono)                         ic[nic++] = IC_STATE[ICS_MONO];
    for (int i = 0; i < 5 && nic < 12; i++)
      if (now < flashUntil[i]) ic[nic++] = IC_STATE[secIc[i]];

    for (int i = 0; i < nic; i++) {
      int x = 3 + (i & 1) * 15;            // 2 columnas: x=3 y x=18
      int y = 23 + (i >> 1) * 13;          // filas de 13 px bajo la tarjeta
      if (y + IC_STATE_H > 63) break;      // no pintar fuera de pantalla
      oled.drawBitmap(x, y, ic[i], IC_STATE_W, IC_STATE_H, SSD1306_WHITE);
    }
  }

  // === DERECHA — osciloscopio "ovillo" enmarcado (protagonista) ==============
  drawTangle(SX, level);

  // === FASE del loop: barra fina en la base de la columna izquierda ==========
  if (st == EVL_PLAY || st == EVL_REC) {
    float ph = (st == EVL_PLAY) ? evloopPhase(stepPos)
                                : (float)(evloopRecSteps(stepPos) % 16) / 16.0f;
    oled.drawFastHLine(6, 63, (int)(ph * (float)(SX - 8)) + 1, SSD1306_WHITE);
  }
  // Punto de conexión BLE (esquina inf-izq).
  if (bleConnected) oled.fillCircle(2, 61, 1, SSD1306_WHITE);

  oled.display();
}

// --- Animación de arranque -------------------------------------------------
// Zoom Argentina -> Tierra del Fuego: 9 bitmaps pre-renderizados (boot_zoom.h,
// generados por tools/gen_boot_zoom.py; 333 ms por frame = 3 zooms por segundo).
// El rombo de Tolhuin viene horneado en los frames finales.
#include "boot_zoom.h"

// Frame `f` (0..8) del zoom.
void oledBootZoom(uint8_t f) {
  if (!ok) return;
  if (f > 8) f = 8;
  oled.clearDisplay();
  oled.drawBitmap(0, 0, ZOOM[f], OLED_W, OLED_H, SSD1306_WHITE);
  oled.display();
}

// Cartel final: TOLHUIN grande + "mk1" en caja invertida + "v2026" abajo.
void oledBootTitle() {
  if (!ok) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  printCentered("TOLHUIN", 20, &FreeSansBold12pt7b);
  oled.setFont(NULL); oled.setTextSize(2);            // mk1: texto negro en caja blanca
  const int vw = 3 * 12 + 8;                          // 3 chars size-2 + padding
  int bx = (OLED_W - vw) / 2, by = 27;
  oled.fillRect(bx, by, vw, 20, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(bx + 4, by + 3); oled.print("mk1");
  oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  oled.setCursor((OLED_W - 5 * 6) / 2, 56); oled.print("v2026");
  oled.display();
}

// Menú de config AGRUPADO por páginas. Layout 128x64:
//   y=0..9  : título de la página (izq) + "n/N" (der), fuente chica
//   y=12..  : hasta 3 filas en fuente default size 2 (12x16 px, ~10 chars/fila):
//             "NOM valor" — la fila seleccionada va INVERTIDA (banda blanca,
//             texto negro), que es lo más legible de un vistazo en vivo.
// NAV mueve la selección (cruza de página solo); VOICING edita la fila.
void oledFxMenuList(const char* title, int page, int pages,
                    const char* const* names, const char* const* vals,
                    int nRows, int sel, const uint8_t* icon) {
  if (!ok) return;
  oled.clearDisplay();
  oled.setFont(NULL); oled.setTextSize(1);

  // --- Barra de TÍTULO invertida (y0..13): icono + nombre + "n/N" ---
  oled.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  int tx = 3;
  if (icon) {
    oled.drawBitmap(1, 1, icon, IC_PAGE_W, IC_PAGE_H, SSD1306_BLACK);
    tx = IC_PAGE_W + 4;
  }
  oled.setCursor(tx, 3); oled.print(title);
  char pg[8];
  snprintf(pg, sizeof(pg), "%d/%d", page + 1, pages);
  oled.setCursor(126 - (int)strlen(pg) * 6, 3); oled.print(pg);

  // --- Filas (y16..): nombre a la IZQ, valor alineado a la DER; la fila
  //     seleccionada va en banda invertida. ---
  if (nRows > 3) nRows = 3;
  for (int i = 0; i < nRows; i++) {
    int y = 17 + i * 16;
    bool s = (i == sel);
    if (s) oled.fillRect(0, y - 1, 128, 16, SSD1306_WHITE);
    oled.setTextColor(s ? SSD1306_BLACK : SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(2, y);
    for (int k = 0; k < 3 && names[i][k]; k++) oled.write(names[i][k]);
    char v[8];
    snprintf(v, sizeof(v), "%.6s", vals[i]);
    oled.setCursor(126 - (int)strlen(v) * 12, y);
    oled.print(v);
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.display();
}
