# TOLHUIN — Estado del proyecto (handoff)

> Resumen para continuar en otra conversación. Autocontenido: con esto + el repo
> (código + bitácora git) se retoma sin perder contexto. Leer también `CLAUDE.md`,
> `config.h` y `pcb/README.md`.

## Qué es
Sintetizador de acordes diatónicos. **ESP32-S3 + PCM5102 (I2S)**, motor DSP propio
(sin AMY). Reglas: notas diatónicas, sin acordes dominantes, sin b9/b2, registro
MIDI 36–91. Comentarios en español. Una feature = un commit; `arduino-cli` solo
**compila** (nunca upload en modo agente) + tests host antes de commitear.

## Firmware (funcionando, ~56% flash / ~73% RAM interna + ~2.7 MB PSRAM del looper)
- **DSP** (`dsp.cpp`): PolyBLEP, ADSR, filtro SVF, LFO, delay estéreo, reverb,
  **chorus**, **tremolo de salida** (sync BPM), **BODY** (cuerpo/unísono por voz),
  paneo por voz. Timbres: BRASS, EPIANO, STRINGS, SINE, TRIANGLE, ORGAN, **FLUTE**
  (ORGAN/FLUTE como wavetable para bajar CPU).
- **Armonía** (`harmony.cpp`): tríada diatónica + 8 zonas de color + presets
  (básico/7as/9as/6-9); voice leading; slash-bass; nombres enarmónicos.
- **Glide de tiempo constante**, mono, octava, hold, strum, arp, **sustain conmutable**.
- **MIDI** (`midi.cpp`): USB (TinyUSB) + BLE (NimBLE), in/out (acorde en un solo
  paquete BLE) + **clock OUT** (TOLHUIN master) y **sync IN** (el arp se fase-
  bloquea a los ticks entrantes).
- **LOOPER de 4 capas [BETA]** (audio en PSRAM): modo en vivo, rec/play/overdub/
  mute/clear/STOP; los grados siguen tocando; capas en fase (posición global);
  graba sólo las voces (la batería no entra). A pulir con placa en mano.
- **Percusión [BETA]**: bombo/caja sintetizados + **patrón sync BPM editable**
  (off/básico/4floor/síncopa), ítem DRUMS del menú; se mezclan secos (no al looper).
- **Control / OLED** (`oled.cpp`): el joystick es **sólo color**. **Menú de config
  por BOTONES**: NAV−/NAV+ navegan, SW (tap) edita (ítems de valor: TIMBRE/KEY/
  OCTAVE/BPM/DELAY/TREMOLO/DRUMS entran a editar, NAV± cambia). Fuente FreeSansBold
  (forzar size 1). Pantallas: normal (acorde + osciloscopio), config y looper.

> **Núcleo sólido; looper+drums BETA** (fuente de los bugs recientes, dependen de
> hardware no definido). Prioridad: **conseguir placa S3 estable + armar el
> perfboard**, después EDIT armónico y matching de timbres. Ver CLAUDE.md.

## Hardware: S3 sí, C3 no
El motor depende del **ESP32-S3** (dual core: audio/UI separados; **PSRAM** para el
looper; **USB-OTG** para USB-MIDI). El **ESP32-C3** se evaluó y NO sirve para TOLHUIN
(single core, sin PSRAM, sin USB-MIDI, ~11 GPIO < los ~17 que usamos): se reserva
para el controlador BT-MIDI aparte. Placa chica recomendada: **ESP32-S3 SuperMini**
(ojo: no expone GPIO42 → mover I2C SCL a 9, y la tecla VII de 48 a 8).

## Hardware / pinout (`config.h`)
- **I2S**: BCK 38, WS 39, DIN 40 (SCK del PCM5102 → GND).
- **I2C**: SDA 41, SCL 42 (OLED 0x3C + ADS1115 0x48).
- **Botones** (INPUT_PULLUP → GND): I–VII = 15,16,17,18,21,47,48; NAV− 11,
  NAV+ 12, HOLD 14, VOICING 7.
- **Joystick HW-504**: SW 13; ejes VRx/VRy → **ADS1115 A0/A1** (no al ESP32).
- Todo 3.3 V. Evitar GPIO33–37 (PSRAM), 0/3/45/46/19/20/43/44.
- **Audio**: PCM5102 LOUT/ROUT → jack 3.5 mm (ya en la netlist, DC-coupled).

## Batería
LiPo 3.7 V (≥500 mAh) → **TP4056** (carga+protección) → interruptor →
**buck-boost 3.3 V** → riel. Nunca directo. USB-MIDI y batería **no a la vez**.

## PCB — automatización (carpeta `pcb/`)
- `gen_netlist.py` = **fuente de verdad** (pinout) → `out/tolhuin.net` (KiCad) +
  `netlist.csv` + `bom.csv` + `connections.txt`. **30 componentes / 27 nets**
  (incluye desacoples, pull-ups I2C, batería y jack de audio).
- `build.py` = orquestador: netlist → place/outline → DSN → **Freerouting CLI** →
  SES → **kicad-cli** (DRC + Gerbers + Drill).
- `pcb_tasks.py` = place/dsn/ses (pcbnew, KiCad 8).
- **Estado: scaffold SIN probar en KiCad** (no instalado). Footprints por defecto =
  headers genéricos → poner los **reales** antes de fabricar (tabla en README).
  Falta instalar KiCad 8 + Java + Freerouting.

## Decisiones pendientes
1. ~~Agregar jack de audio 3.5 mm~~ ✔ hecho (net AUDIO_L/AUDIO_R, comp J1).
2. **Botones**: capacitivos (remapear a GPIO1–14 + reescribir `input.cpp`) vs
   táctiles. **Recomendado: táctiles para v1.1**, capacitivo recién en v2.
3. **Pantalla**: 0.96" vs **1.3"** (mismo driver SSD1306, se ve más grande sin
   tocar firmware) vs mayor (otro driver/firmware).
4. Footprints reales de los módulos.

## Render de referencia (Gemini)
Layout aprobado (front/back). Hay un **prompt corregido** (en el historial) para
regenerarla sin glitches. Look: máscara negra + **ENIG** (pads dorados) + silk
"DESIGNED IN ARGENTINA · v1.1 · COMPLETE SYNTH".

## Próximos pasos sugeridos
1. Decidir botones (táctil vs capacitivo) y pantalla.
2. Instalar KiCad 8 + Java + Freerouting; correr `python pcb\build.py` y ajustar
   las etapas sensibles a versión (place/dsn/ses) — eso quedó sin probar.
3. Poner footprints reales y fabricar (JLCPCB/PCBWay, negro + ENIG).
4. Ensamblar + test físico (continuidad contra `netlist.csv`, encendido seguro a
   3.3 V con límite de corriente, luego módulos y batería).
