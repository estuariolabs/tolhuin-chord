# TOLHUIN Chord

Sintetizador de **acordes diatónicos** sobre **ESP32-S3** con motor de audio
propio (sin librerías externas de síntesis). Este repositorio es el **firmware
completo + hardware + herramientas**; el desarrollo está publicado con fines de
transparencia.

> Diatonic chord synthesizer on ESP32-S3 with a from-scratch audio engine
> (band-limited synthesis, diatonic harmony, sequencer, effects, MIDI USB/BLE).

**El motor puro** (la parte que compila en PC, sin hardware) también está como
repo aparte, más fácil de reusar como librería:
👉 https://github.com/estuariolabs/tolhuin-chord-engine

---

## Hardware

- **MCU:** ESP32-S3 (dual core + USB-OTG). Target de placa chica: S3 SuperMini.
- **DAC:** PCM5102 por I2S estéreo 16-bit (BCK=38, WS=39, DIN=40, SCK→GND).
- **Audio:** 44100 Hz, bloques de 256 muestras. Core 0 = audio; Core 1 = UI/lógica.
- **Controles:** botones + joystick (ADS1115 por I2C), OLED SSD1306.
- **MIDI:** USB (TinyUSB) + BLE (NimBLE), in/out + MIDI clock in/out.
- Ver `GUIA_CONEXIONES_S3_SUPERMINI.md` y `pcb/` para el cableado y la placa.

## Arquitectura

**Motor (compila en host con `g++`, sin Arduino):**
| Archivo | Qué hace |
|---------|----------|
| `dsp.cpp/.h` | Síntesis (osciladores band-limited, ADSR, SVF, LFO), efectos (delay/reverb/chorus/tremolo), percusión, looper, arpegiador y **reloj maestro** con sync a MIDI clock. |
| `harmony.cpp/.h` | Armonía **diatónica**: acordes por grado, voicings, conducción de voces, reglas duras. |
| `evloop.cpp/.h` | Motor de eventos / secuenciador puro. |

**Firmware (específico de la placa):**
| Archivo | Qué hace |
|---------|----------|
| `tolhuin_chord_v2.ino` | Sketch principal (setup/loop, menú, sesión). |
| `synth.cpp/.h` | Wrapper I2S + tarea FreeRTOS + cola de eventos thread-safe. |
| `input.cpp/.h` | Botones + joystick (ADS1115). |
| `midi.cpp/.h` | MIDI USB (TinyUSB) + BLE (NimBLE), notas + clock. |
| `oled.cpp/.h` | Pantalla SSD1306. |
| `webcfg.cpp/.h` | Protocolo de config por USB (editor web). |

**Otros:** `webtool/` (flasheo web con ESP Web Tools + editor de timbres/patrones),
`tools/` (generadores de samples/íconos + render en host), `diag_*` (sketches de
diagnóstico de I2S), `pcb/`. La bitácora del desarrollo está en `TASKS.md`.

## Compilar y testear

**Firmware** (requiere `arduino-cli` + core `esp32:esp32` 3.2.0 y libs Adafruit):

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi" .
```

**Tests del motor en host** (g++/clang++, sin hardware):

```powershell
powershell -ExecutionPolicy Bypass -File test\run_tests.ps1
```

Verifican armonía, DSP (NaN/clip/RMS/Goertzel, timing de grilla, fase del reloj)
y el secuenciador — todos deterministas.

## Flasheo web

`webtool/` usa [ESP Web Tools](https://esphome.github.io/esp-web-tools/) (cargado
por CDN) con `manifest.json`. El `.bin` mergeado **no se versiona** (ver
`.gitignore`); generalo con `arduino-cli` y publicalo como *release asset* o
serví el webtool con el binario al lado.

## Licencia

**GPL-3.0-or-later** (ver [`LICENSE`](LICENSE)). Los samples de batería en
`tools/samples/` son originales del autor (grabados en Logic Pro).
