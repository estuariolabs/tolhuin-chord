// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   midi.cpp  -  Entrada MIDI por USB (TinyUSB) y BLE (NimBLE), parseo crudo.
   Sin la MIDI Library: se parsean los bytes a mano (más robusto y liviano).
   Maneja:
     - clock (0xF8): calcula el BPM -> sincroniza delay/arp.
     - note on/off: toca el synth en el timbre actual.
     - start/stop (0xFA/0xFC): reinicia el cálculo de tempo.
   MIDI OUT: notas (acorde) + CLOCK OUT (master): emite 0xFA/0xF8/0xFC a 24 PPQN
   desde app.bpm para que otros equipos sigan el tempo de TOLHUIN.
   USB: paquetes USB-MIDI de 4 bytes. BLE: servicio MIDI estándar (NimBLE).
   Capa de hardware (ESP32/Arduino). NO testeado en placa.
   FQBN con USB-OTG/TinyUSB (USBMode=default). BLE aparece como "TOLHUIN".
   ============================================================================ */
#include <Arduino.h>
#include "config.h"
#include "midi.h"
#include "synth.h"
#include "webcfg.h"   // webcfgFeedMidi: comandos del editor web por SysEx

#include <Adafruit_TinyUSB.h>
#include <NimBLEDevice.h>

static Adafruit_USBD_MIDI usbMidi;
static NimBLECharacteristic* gChr = nullptr;   // para notificar (MIDI OUT por BLE)

static volatile float gBpm = 0.0f;
static volatile bool  gBpmNew = false;
static volatile bool  gBleConn = false;
static uint32_t       lastClkUs = 0;
static float          clkAvgUs = 0.0f;
static volatile uint32_t gTickCount = 0;     // ticks 0xF8 entrantes (para sync del arp)
static volatile uint32_t gLastTickMs = 0;    // millis() del último tick (para "clock vivo")

static void sendUsb(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2);  // (definida abajo)

// ---- tempo desde el MIDI clock (24 PPQN) ----
static void onClock() {
  uint32_t now = micros();
  gTickCount++; gLastTickMs = millis();         // para el sync del arp a los ticks
  if (lastClkUs != 0) {
    uint32_t dt = now - lastClkUs;
    if (dt > 1000 && dt < 100000) {           // ~25..600 BPM
      clkAvgUs = (clkAvgUs > 0) ? (clkAvgUs * 0.8f + dt * 0.2f) : dt;
      gBpm = 60000000.0f / (clkAvgUs * 24.0f);
      gBpmNew = true;
    }
  }
  lastClkUs = now;
}

// ---- máquina de estados MIDI con running status (compartida USB/BLE) ----
static uint8_t rsStatus = 0, rsData[2], rsIdx = 0, rsNeed = 0;
static void parseByte(uint8_t b) {
  if (b & 0x80) {
    if (b >= 0xF8) {                          // realtime: no altera running status
      if (b == 0xF8) onClock();
      else if (b == 0xFA) { lastClkUs = 0; clkAvgUs = 0; gTickCount = 0; }   // Start: reinicia compás
      else if (b == 0xFC) { lastClkUs = 0; clkAvgUs = 0; }                    // Stop
      return;
    }
    rsStatus = b; rsIdx = 0;
    uint8_t hi = b & 0xF0;
    rsNeed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
  } else {
    if (!rsStatus) return;
    if (rsIdx < 2) rsData[rsIdx++] = b;
    if (rsIdx >= rsNeed) {
      rsIdx = 0;
      uint8_t hi = rsStatus & 0xF0;
      if (hi == 0x90) { if (rsData[1]) synthNoteOn(rsData[0], rsData[1]); else synthNoteOff(rsData[0]); }
      else if (hi == 0x80) synthNoteOff(rsData[0]);
    }
  }
}

// ---- BLE MIDI: quita header/timestamps y pasa los bytes MIDI al parser ----
static void parseBleMidi(const uint8_t* d, size_t len) {
  if (len < 2) return;
  for (size_t i = 1; i < len; i++) {          // i=0 es el header
    uint8_t b = d[i];
    // un byte con bit7 seguido de otro con bit7 es un timestamp-low: saltarlo
    if ((b & 0x80) && (i + 1 < len) && (d[i + 1] & 0x80)) continue;
    parseByte(b);
  }
}

class ChrCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    NimBLEAttValue v = c->getValue();
    parseBleMidi(v.data(), v.length());
  }
};
class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { gBleConn = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    gBleConn = false; NimBLEDevice::startAdvertising();
  }
};

void midiInit() {
  // USB MIDI. OJO: Windows cachea los descriptores por VID/PID — si la placa ya
  // se enumeró antes con otro nombre, puede seguir mostrando el viejo hasta
  // desinstalar el dispositivo en el Administrador de dispositivos.
  TinyUSBDevice.setManufacturerDescriptor("TOLHUIN");
  TinyUSBDevice.setProductDescriptor("TOLHUIN Chord");
  usbMidi.setStringDescriptor("TOLHUIN MIDI");
  usbMidi.begin();
  // CLAVE: con CDCOnBoot el USB ya se enumeró (solo CDC) ANTES de setup(); la
  // interfaz MIDI agregada acá no existe para el host hasta RE-enumerar. Sin
  // este detach/attach, Windows ve el dispositivo compuesto con una única
  // función (COM) y el puerto "TOLHUIN MIDI" no aparece jamás.
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(20);
    TinyUSBDevice.attach();
  }

  // BLE MIDI (servicio estándar)
  NimBLEDevice::init("TOLHUIN");
  NimBLEDevice::setMTU(128);        // MTU grande: un acorde entero entra en un paquete
  NimBLEServer* srv = NimBLEDevice::createServer();
  srv->setCallbacks(new SrvCb());
  NimBLEService* svc = srv->createService("03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
  NimBLECharacteristic* chr = svc->createCharacteristic(
      "7772E5DB-3868-4112-A1A9-F2669D106BF3",
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  chr->setCallbacks(new ChrCb());
  gChr = chr;
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(svc->getUUID());
  adv->start();

  Serial.println("[midi] USB + BLE MIDI listos (BLE: 'TOLHUIN')");
}

// ---- SysEx entrante del editor web: F0 7D 01 <comando ASCII> F7 ----
// Reensambla el stream a través de los paquetes USB-MIDI y se lo pasa a webcfg.
#define SYX_MAX 340
static uint8_t syxBuf[SYX_MAX];
static int     syxLen = 0;
static bool    syxOn  = false;

static void syxFeed(const uint8_t* d, int n) {
  for (int i = 0; i < n; i++) {
    uint8_t b = d[i];
    if (b == 0xF0) { syxOn = true; syxLen = 0; continue; }
    if (b == 0xF7) {
      if (syxOn && syxLen >= 2 && syxBuf[0] == 0x7D && syxBuf[1] == 0x01)
        webcfgFeedMidi((const char*)syxBuf + 2, syxLen - 2);
      syxOn = false; syxLen = 0;
      continue;
    }
    if (syxOn && syxLen < SYX_MAX) syxBuf[syxLen++] = b;
  }
}

void midiUpdate() {
  uint8_t pkt[4];
  while (usbMidi.readPacket(pkt)) {
    // DIAGNÓSTICO (sonda cruda, antes de todo el parseo): vuelca al monitor serie
    // los primeros 48 paquetes USB-MIDI que llegan (salvo el clock 0xF8, que
    // spamea). Si al mandar '#hello' desde la web NO aparece ni una línea
    // "[midi] rx ...", los paquetes no están llegando a readPacket -> el problema
    // es la enumeración USB-MIDI o el dispositivo, NO el parseo de SysEx.
    static int rxDbg = 0;
    if (rxDbg < 48 && pkt[1] != 0xF8) {
      rxDbg++;
      Serial.printf("[midi] rx %02X %02X %02X %02X\n", pkt[0], pkt[1], pkt[2], pkt[3]);
    }
    // Despacho por CIN (nibble bajo del byte 0): los paquetes de SysEx llevan
    // datos crudos en pkt[1..3] y NO deben interpretarse como status.
    uint8_t cin = pkt[0] & 0x0F;
    if      (cin == 0x4) { syxFeed(pkt + 1, 3); continue; }   // sysex start/continua
    else if (cin == 0x5) { syxFeed(pkt + 1, 1); continue; }   // sysex termina (1 byte)
    else if (cin == 0x6) { syxFeed(pkt + 1, 2); continue; }   // sysex termina (2 bytes)
    else if (cin == 0x7) { syxFeed(pkt + 1, 3); continue; }   // sysex termina (3 bytes)
    uint8_t status = pkt[1];
    if (status == 0xF8) onClock();
    else if (status == 0xFA) { lastClkUs = 0; clkAvgUs = 0; gTickCount = 0; }
    else if (status == 0xFC) { lastClkUs = 0; clkAvgUs = 0; }
    else if ((status & 0xF0) == 0x90) { if (pkt[3]) synthNoteOn(pkt[2], pkt[3]); else synthNoteOff(pkt[2]); }
    else if ((status & 0xF0) == 0x80) synthNoteOff(pkt[2]);
  }
}

// Escribe un paquete USB-MIDI ESPERANDO lugar en el FIFO de TinyUSB. El FIFO es
// chico y una respuesta JSON son ~70 paquetes seguidos: sin esta espera, los
// paquetes que no entran se pierden y el SysEx llega incompleto (Chrome lo
// descarta en silencio). Timeout corto para no colgar el loop si el host no lee.
static bool sendUsbBlocking(uint8_t cin, uint8_t a, uint8_t b, uint8_t c) {
  uint8_t pkt[4] = { cin, a, b, c };
  uint32_t t0 = millis();
  while (!usbMidi.writePacket(pkt)) {
    if (millis() - t0 > 100) return false;
    delay(1);                                    // deja drenar el FIFO al stack USB
  }
  return true;
}

// SysEx saliente con la respuesta del web-config: F0 7D 02 <ASCII> F7.
// Se parte en paquetes USB-MIDI de a 3 bytes con el CIN que corresponda.
void midiSendSysExText(const char* text, int len) {
  const uint8_t hdr[3] = { 0xF0, 0x7D, 0x02 };
  int total = 3 + len + 1;                       // header + texto + F7
  auto at = [&](int i) -> uint8_t {
    if (i < 3)        return hdr[i];
    if (i < 3 + len)  return (uint8_t)(text[i - 3] & 0x7F);   // ASCII 7-bit
    return 0xF7;
  };
  for (int i = 0; i < total; i += 3) {
    int rem = total - i;
    bool ok;
    if      (rem >  3) ok = sendUsbBlocking(0x04, at(i), at(i + 1), at(i + 2));
    else if (rem == 3) ok = sendUsbBlocking(0x07, at(i), at(i + 1), at(i + 2));
    else if (rem == 2) ok = sendUsbBlocking(0x06, at(i), at(i + 1), 0);
    else               ok = sendUsbBlocking(0x05, at(i), 0, 0);
    if (!ok) { Serial.println("[midi] TX SysEx: FIFO lleno, respuesta cortada"); return; }
  }
}

bool  midiClockChanged() { bool b = gBpmNew; gBpmNew = false; return b; }
float midiClockBpm()     { return gBpm; }
bool  midiBleConnected() { return gBleConn; }
uint32_t midiClockTickCount() { return gTickCount; }
bool     midiClockLive()      { return gLastTickMs != 0 && (millis() - gLastTickMs) < 500; }

// ---- MIDI OUT ----
static void sendUsb(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t pkt[4] = { cin, status, d1, d2 };
  usbMidi.writePacket(pkt);
}
static void sendBle(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!gBleConn || !gChr) return;
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);
  uint8_t pkt[5] = { (uint8_t)(0x80 | ((ts >> 7) & 0x3F)),
                     (uint8_t)(0x80 | (ts & 0x7F)), status, d1, d2 };
  gChr->setValue(pkt, 5);
  gChr->notify();
}
// Byte de sistema en tiempo real (0xF8 clock, 0xFA start, 0xFC stop) por USB+BLE.
static void sendRealtime(uint8_t status) {
  sendUsb(0x0F, status, 0, 0);                 // USB-MIDI: CIN 0xF = single-byte realtime
  if (gBleConn && gChr) {                       // BLE-MIDI: header + timestamp + 1 byte
    uint16_t ts = (uint16_t)(millis() & 0x1FFF);
    uint8_t pkt[3] = { (uint8_t)(0x80 | ((ts >> 7) & 0x3F)),
                       (uint8_t)(0x80 | (ts & 0x7F)), status };
    gChr->setValue(pkt, 3); gChr->notify();
  }
}

// ---- MIDI clock OUT (master) ----
static bool     gClkOutOn  = false;
static uint32_t gClkNextUs = 0;     // micros() del próximo tick

void midiClockOutSet(bool on) {
  if (on == gClkOutOn) return;
  gClkOutOn = on;
  if (on) { sendRealtime(0xFA); gClkNextUs = micros(); }   // Start + arranca ya
  else    { sendRealtime(0xFC); }                          // Stop
}
bool midiClockOut() { return gClkOutOn; }

void midiClockOutUpdate(uint16_t bpm) {
  if (!gClkOutOn) return;
  if (bpm < 20) bpm = 20; if (bpm > 300) bpm = 300;
  uint32_t tickUs = 60000000UL / ((uint32_t)bpm * 24UL);   // 24 PPQN
  uint32_t now = micros();
  // Emite los ticks vencidos (comparación con resta -> a prueba de wrap de micros).
  // Tope de 4 por vuelta: si se atrasó mucho, no spamea ni se cuelga.
  for (int g = 0; g < 4 && (int32_t)(now - gClkNextUs) >= 0; g++) {
    sendRealtime(0xF8);
    gClkNextUs += tickUs;
  }
}

void midiSendNoteOn(uint8_t note, uint8_t vel) {
  uint8_t v = (vel > 127) ? 127 : vel;
  sendUsb(0x09, 0x90, note, v);     // note on, canal 1, cable 0
  sendBle(0x90, note, v);
}
void midiSendNoteOff(uint8_t note) {
  sendUsb(0x08, 0x80, note, 0);     // note off, canal 1
  sendBle(0x80, note, 0);
}

void midiSendChord(const uint8_t* off, uint8_t nOff,
                   const uint8_t* on, uint8_t nOn, uint8_t vel) {
  uint8_t v = (vel > 127) ? 127 : vel;
  // USB: paquetes individuales (no se descartan como las notifs BLE).
  for (uint8_t i = 0; i < nOff; i++) sendUsb(0x08, 0x80, off[i], 0);
  for (uint8_t i = 0; i < nOn;  i++) sendUsb(0x09, 0x90, on[i], v);

  // BLE: TODO en un solo paquete con running status 0x90 (note-off = velocity 0).
  if (!gBleConn || !gChr || (nOff + nOn) == 0) return;
  uint16_t ts = (uint16_t)(millis() & 0x1FFF);
  uint8_t lo = (uint8_t)(0x80 | (ts & 0x7F));
  uint8_t pkt[2 + (MAX_CHORD_NOTES + 1) * 2 * 3];
  uint8_t p = 0;
  pkt[p++] = (uint8_t)(0x80 | ((ts >> 7) & 0x3F));   // header
  bool first = true;
  for (uint8_t i = 0; i < nOff; i++) {
    pkt[p++] = lo; if (first) { pkt[p++] = 0x90; first = false; }
    pkt[p++] = off[i]; pkt[p++] = 0;                 // running status -> note off
  }
  for (uint8_t i = 0; i < nOn; i++) {
    pkt[p++] = lo; if (first) { pkt[p++] = 0x90; first = false; }
    pkt[p++] = on[i]; pkt[p++] = v;
  }
  gChr->setValue(pkt, p);
  gChr->notify();
}
