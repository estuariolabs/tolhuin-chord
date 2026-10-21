// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   input.cpp  -  Controles físicos (capa de hardware). Portado del MVP.
   Botones activo-bajo (INPUT_PULLUP) con antirrebote. Joystick HW-504 vía
   ADS1115 con zona muerta + histéresis radial + confirmación por estabilidad.
   Si el ADS no está presente, color = CENTRO (no se cuelga).
   Click del joystick (capa ARMÓNICA; el stick NO interviene, sólo el pulsador):
     - click SIN grado apretado  -> alterna modo MAYOR / menor
     - click CON un grado apretado -> mayoriza ese grado (dominante 7 <-> diatónico)
   (El menú de config se navega con NAV y se edita con VOICING, sin el SW.)
   ============================================================================ */
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_ADS1X15.h>
#include "config.h"
#include "input.h"

static const uint16_t DEBOUNCE_MS = 6;
static const uint16_t LONG_MS     = 450;   // umbral de pulsación larga
static const uint16_t NAV_REPEAT_DELAY_MS = 420;  // NAV sostenido: arranca a repetir
static const uint16_t NAV_REPEAT_MS       = 150;  // ...y repite a este ritmo

// ----------------------------- acordes -------------------------------------
static bool     pressed[PIN_CHORD_COUNT];
static bool     lastRaw[PIN_CHORD_COUNT];
static uint32_t lastChange[PIN_CHORD_COUNT];
static int      pressedEdge = -1;
static bool     allReleasedEdge = false;

// ----------------------- botón genérico con flanco -------------------------
struct Btn { bool stable = false; bool lastRaw = false; uint32_t change = 0, press = 0, rep = 0; };
static Btn bNavM, bNavP;

static bool edge(uint8_t pin, Btn& b, uint32_t now) {
  bool raw = !digitalRead(pin);
  if (raw != b.lastRaw) { b.lastRaw = raw; b.change = now; }
  bool e = false;
  if (raw != b.stable && (now - b.change) >= DEBOUNCE_MS) {
    b.stable = raw;
    if (raw) { e = true; b.press = now; b.rep = 0; }
  }
  return e;
}

// Flanco + AUTO-REPEAT: mantener apretado repite (para recorrer el menú rápido).
static bool edgeRepeat(uint8_t pin, Btn& b, uint32_t now) {
  if (edge(pin, b, now)) return true;
  if (b.stable && (now - b.press) >= NAV_REPEAT_DELAY_MS) {
    uint32_t since = now - b.press - NAV_REPEAT_DELAY_MS;
    uint32_t n = since / NAV_REPEAT_MS + 1;
    if (n > b.rep) { b.rep = n; return true; }
  }
  return false;
}

// -------- botón con TAP vs pulsación LARGA (HOLD y VOICING) --------
struct LpBtn { bool stable=false, lastRaw=false; uint32_t change=0, press=0; bool longFired=false; };
static LpBtn lpHold, lpVoic;

// Devuelve 0=nada, 1=tap (al soltar antes de LONG_MS), 2=largo (una vez al cruzarlo).
static int lpEvent(uint8_t pin, LpBtn& b, uint32_t now) {
  bool raw = !digitalRead(pin);
  if (raw != b.lastRaw) { b.lastRaw = raw; b.change = now; }
  int ev = 0;
  if (raw != b.stable && (now - b.change) >= DEBOUNCE_MS) {
    b.stable = raw;
    if (raw) { b.press = now; b.longFired = false; }                       // press
    else if (!b.longFired && (now - b.press) < LONG_MS) ev = 1;            // release = tap
  }
  if (b.stable && !b.longFired && (now - b.press) >= LONG_MS) { b.longFired = true; ev = 2; } // largo
  return ev;
}

static int  holdEv = 0, voicEv = 0;
static int  navDelta = 0;

// --- Navegación v3: NAV con funciones FIJAS (un botón por función, sin combos) +
//     menú por D-pad del joystick ---
static bool     menuMode = false;             // el menú está visible (joystick = D-pad)
static bool     navMenuEdge  = false;         // NAV+ (derecha): entra/sale del menú
static bool     navDrumsEdge = false;         // NAV- (izquierda): drums on/off
static int      menuNavStep = 0, menuEditStep = 0;   // pasos pendientes del D-pad
// Auto-repeat por eje del joystick (mismo criterio que los botones NAV).
struct AxisRep { int dir = 0; uint32_t press = 0, rep = 0; };
static AxisRep  vAxis, hAxis;
static int axisRepeat(int dir, AxisRep& a, uint32_t now) {
  if (dir == 0) { a.dir = 0; a.rep = 0; return 0; }
  if (dir != a.dir) { a.dir = dir; a.press = now; a.rep = 0; return dir; }   // 1er paso
  if (now - a.press >= NAV_REPEAT_DELAY_MS) {
    uint32_t since = now - a.press - NAV_REPEAT_DELAY_MS;
    uint32_t k = since / NAV_REPEAT_MS + 1;
    if (k > a.rep) { a.rep = k; return dir; }
  }
  return 0;
}

// ----------------------------- joystick ------------------------------------
static Adafruit_ADS1115 ads;
static bool      adsOk = false;
static int16_t   jcx = JOY_CENTER_DEFAULT, jcy = JOY_CENTER_DEFAULT;
// Auto-rango: min/max por eje se expanden al mover el stick -> centro y media
// escala se autocalibran (corrige un centro mal tomado en el boot).
static int16_t   jxMin, jxMax, jyMin, jyMax;
static uint32_t  joyPollMs = 0;
static ColorZone curColor = CZ_CENTER;
static bool      colorChangedFlag = false;
static ColorZone candZone = CZ_CENTER;
static int       candCount = 0;

// ------------------------------- SW ----------------------------------------
static bool      swHeld = false, swLastRaw = false;
static uint32_t  swChange = 0;
static bool      joyEdge = false;     // click del SW (press+release) -> lo consume el .ino
static bool      joyGuard = false;    // congela color un instante tras el click (hasta recentrar)

// Sectores de 45° del joystick (ángulo -> zona de color).
static const ColorZone SECTOR[8] = {
  CZ_RIGHT, CZ_UP_RIGHT, CZ_UP, CZ_UP_LEFT,
  CZ_LEFT,  CZ_DOWN_LEFT, CZ_DOWN, CZ_DOWN_RIGHT
};

void inputInit() {
  for (int i = 0; i < PIN_CHORD_COUNT; i++) {
    pinMode(PIN_CHORD[i], INPUT_PULLUP);
    pressed[i] = false; lastRaw[i] = false; lastChange[i] = 0;
  }
  pinMode(PIN_HOLD,      INPUT_PULLUP);
  pinMode(PIN_VOICING,   INPUT_PULLUP);
  pinMode(PIN_NAV_MINUS, INPUT_PULLUP);
  pinMode(PIN_NAV_PLUS,  INPUT_PULLUP);
  pinMode(PIN_JOY_SW,    INPUT_PULLUP);

  // ADS: detección segura por sondeo I2C (no se cuelga si está ausente).
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // --- Scanner I2C de diagnóstico: lista TODAS las direcciones que responden ---
  // (OLED debería aparecer en 0x3C; ADS1115 en 0x48 si ADDR->GND).
  Serial.print("[i2c] scan:");
  int i2cFound = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); i2cFound++; }
  }
  Serial.printf("  (%d dispositivos)\n", i2cFound);
  // La dirección del ADS depende de cómo quedó su pin ADDR en CADA módulo:
  // GND=0x48, VDD=0x49, SDA=0x4A, SCL=0x4B. Se prueban las cuatro (empezando por
  // la del prototipo 1) para que un armado nuevo no dependa de un strap puntual.
  static const uint8_t ADS_TRY[5] = { ADS_ADDR, 0x48, 0x49, 0x4A, 0x4B };
  uint8_t adsAddr = ADS_ADDR;
  adsOk = false;
  for (int t = 0; t < 5 && !adsOk; t++) {
    Wire.beginTransmission(ADS_TRY[t]);
    if (Wire.endTransmission() == 0) { adsAddr = ADS_TRY[t]; adsOk = true; }
  }
  ads.begin(adsAddr);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);     // restaurar pines (ads.begin los pisa)
  if (adsOk) {
    ads.setGain(GAIN_ONE);
    ads.setDataRate(RATE_ADS1115_860SPS);
    delay(20);
    for (int k = 0; k < 4; k++) { ads.readADC_SingleEnded(0); ads.readADC_SingleEnded(1); delay(3); }
    long sx = 0, sy = 0;
    for (int k = 0; k < 16; k++) { sx += ads.readADC_SingleEnded(0); sy += ads.readADC_SingleEnded(1); delay(2); }
    jcx = sx / 16; jcy = sy / 16;
    if (jcx < 5000 || jcx > 21000) jcx = JOY_CENTER_DEFAULT;
    if (jcy < 5000 || jcy > 21000) jcy = JOY_CENTER_DEFAULT;
  }
  // Sembrar el auto-rango alrededor del centro inicial (se expande al moverse).
  jxMin = jcx - 2000; jxMax = jcx + 2000;
  jyMin = jcy - 2000; jyMax = jcy + 2000;
  Serial.printf("[input] ADS present=%d addr=0x%02X  centro x=%d y=%d\n",
                adsOk ? 1 : 0, adsAddr, jcx, jcy);
}

static void pollJoystick() {
  if (!adsOk) return;
  if (millis() - joyPollMs < 12) return;
  joyPollMs = millis();

  int16_t rx = ads.readADC_SingleEnded(0);
  int16_t ry = ads.readADC_SingleEnded(1);
#if JOY_SWAP_XY
  { int16_t t = rx; rx = ry; ry = t; }          // ejes cruzados -> intercambia las lecturas
#endif
  // Auto-rango: expandir min/max -> centro y media escala se autocalibran.
  if (rx < jxMin) jxMin = rx;  if (rx > jxMax) jxMax = rx;
  if (ry < jyMin) jyMin = ry;  if (ry > jyMax) jyMax = ry;
  float cx = 0.5f * (jxMin + jxMax), cy = 0.5f * (jyMin + jyMax);
  float halfX = 0.5f * (jxMax - jxMin), halfY = 0.5f * (jyMax - jyMin);
  if (halfX < 500) halfX = 500;  if (halfY < 500) halfY = 500;
  float nx = (rx - cx) / halfX;            // deflexión normalizada -1..1
  float ny = (ry - cy) / halfY;
#if JOY_INVERT_X
  nx = -nx;                                // corrige polaridad izquierda/derecha
#endif
#if JOY_INVERT_Y
  ny = -ny;                                // corrige polaridad arriba/abajo
#endif
  float mag = sqrtf(nx * nx + ny * ny);
  const float IN_R = 0.30f, OUT_R = 0.50f; // zonas muertas relativas (histéresis)

  // MODO MENÚ: el stick es un D-pad (no colorea). Eje dominante para evitar
  // diagonales: ↑/↓ mueven la selección, ←/→ editan el valor (con auto-repeat).
  if (menuMode) {
    uint32_t now = millis();
    float axm = fabsf(nx), aym = fabsf(ny);
    int vsel = 0, ved = 0;
    if      (aym >= OUT_R && aym >= axm) vsel = (ny > 0) ? -1 : +1;   // arriba (ny>0) = sube
    else if (axm >= OUT_R && axm >  aym) ved  = (nx > 0) ? +1 : -1;   // derecha (nx>0) = +
    int nv = axisRepeat(vsel, vAxis, now);
    int ev = axisRepeat(ved,  hAxis, now);
    if (nv) menuNavStep  = nv;
    if (ev) menuEditStep = ev;
    return;
  }

  // Con el SW apretado NO se recolorea: así el click queda "limpio" (en el HW-504
  // apretar el stick lo deflecta un poco; ignoramos esa deflexión mientras el SW
  // está apretado, para que el click no se confunda con un movimiento de color).
  if (swHeld) return;

  // Tras un gesto: esperar a recentrar el stick antes de volver a colorear.
  if (joyGuard) { if (mag < IN_R) joyGuard = false; return; }

  ColorZone z;
  float limit = (curColor == CZ_CENTER) ? OUT_R : IN_R;
  if (mag < limit) {
    z = CZ_CENTER;
  } else {
    float ang = atan2f(ny, nx) * 180.0f / PI;
    if (ang < 0) ang += 360.0f;
    z = SECTOR[((int)roundf(ang / 45.0f)) & 7];
  }

  if (z == candZone) { if (candCount < JOY_STABLE_READS) candCount++; }
  else { candZone = z; candCount = 1; }

  if (candCount >= JOY_STABLE_READS && z != curColor) {
    curColor = z;
    colorChangedFlag = true;
    // Debug: ayuda a diagnosticar direcciones/calibración del joystick.
    Serial.printf("[joy] x=%d y=%d  nx=%.2f ny=%.2f  zona=%d\n", rx, ry, nx, ny, (int)z);
  }
}

void inputUpdate() {
  pressedEdge = -1; allReleasedEdge = false; joyEdge = false;
  uint32_t now = millis();

  int pressedCount = 0;
  for (int i = 0; i < PIN_CHORD_COUNT; i++) {
    bool raw = !digitalRead(PIN_CHORD[i]);
    if (raw != lastRaw[i]) { lastRaw[i] = raw; lastChange[i] = now; }
    if (raw != pressed[i] && (now - lastChange[i]) >= DEBOUNCE_MS) {
      pressed[i] = raw;
      if (raw) pressedEdge = i;
    }
    if (pressed[i]) pressedCount++;
  }
  static int prevCount = 0;
  if (pressedCount == 0 && prevCount > 0) allReleasedEdge = true;
  prevCount = pressedCount;

  holdEv = lpEvent(PIN_HOLD,    lpHold, now);
  voicEv = lpEvent(PIN_VOICING, lpVoic, now);
  // NAV con FUNCIONES FIJAS (flanco simple, sin auto-repeat: son toggles):
  //   NAV+ (derecha)   -> entra/sale del menú
  //   NAV- (izquierda) -> drums on/off
  // La navegación/edición DENTRO del menú la hace el joystick (D-pad).
  navMenuEdge  = edge(PIN_NAV_PLUS,  bNavP, now);
  navDrumsEdge = edge(PIN_NAV_MINUS, bNavM, now);
  navDelta = 0;                                        // los botones ya no navegan el menú

  // SW del joystick: un click limpio (press+release). El .ino decide qué hace
  // según si hay un grado apretado (mayorizar) o no (alternar mayor/menor).
  {
    bool raw = !digitalRead(PIN_JOY_SW);
    if (raw != swLastRaw) { swLastRaw = raw; swChange = now; }
    if (raw != swHeld && (now - swChange) >= DEBOUNCE_MS) {
      swHeld = raw;
      if (!raw) { joyEdge = true; joyGuard = true; }   // release = un click
    }
  }

  pollJoystick();
}

int  inputPressedDegree()  { return pressedEdge; }
bool inputAllReleased()    { return allReleasedEdge; }
uint8_t inputHeldMask() {
  uint8_t m = 0;
  for (int i = 0; i < PIN_CHORD_COUNT; i++) if (pressed[i]) m |= (uint8_t)(1 << i);
  return m;
}
ColorZone inputColor()     { return curColor; }
bool inputColorChanged()   { bool c = colorChangedFlag; colorChangedFlag = false; return c; }
int  inputHoldEvent()      { return holdEv; }
int  inputVoicingEvent()   { return voicEv; }
int  inputNavDelta()       { return navDelta; }
bool inputSwHeld()         { return swHeld; }
bool inputJoySwPressed()   { return joyEdge; }

// --- Navegación v2 (D-pad del joystick en el menú) ---
void inputSetMenuMode(bool on) {
  menuMode = on;
  vAxis.dir = 0; vAxis.rep = 0; hAxis.dir = 0; hAxis.rep = 0;
  menuNavStep = 0; menuEditStep = 0;
  candZone = CZ_CENTER; candCount = 0;   // no arrastrar color pendiente al entrar/salir
}
bool inputNavMenuToggle()  { return navMenuEdge; }    // NAV+ (derecha): entra/sale del menú
bool inputNavDrumsToggle() { return navDrumsEdge; }   // NAV- (izquierda): drums on/off
int  inputMenuNav()  { int v = menuNavStep;  menuNavStep = 0;  return v; }
int  inputMenuEdit() { int v = menuEditStep; menuEditStep = 0; return v; }
