# TOLHUIN Chord Synthesizer — Guía para el agente

## Visión
Sintetizador de acordes diatónicos sobre ESP32-S3 + DAC PCM5102 (I2S).
Motor de audio propio (sin AMY, sin librerías externas de síntesis).
Objetivo: acercarse a la calidad y funciones del HiChord.

## Hardware
| Señal | GPIO |
|-------|------|
| I2S BCK | 38 |
| I2S WS / LRCK | 39 |
| I2S DIN (→ PCM5102) | 40 |
| SCK del PCM5102 | GND |

- MCU: ESP32-S3 con PSRAM OPI
- DAC: PCM5102 (I2S estéreo, 16-bit)
- Sample rate: 44100 Hz, bloque: 256 muestras
- Core 0: tarea de audio (I2S); Core 1: UI/lógica

## Arquitectura de archivos
```
tolhuin_chord_v2/
  tolhuin_chord_v2.ino <- sketch principal (setup/loop + I2S)
  evloop.h/.cpp       <- looper de EVENTOS (1 pista de acordes, puro/testeable)
  icons.h             <- pixel-art del OLED (GENERADO por tools/gen_icons.py)
  config.h            <- constantes globales (SAMPLE_RATE, MAX_CHORD_NOTES, etc.)
  state.h             <- tipos compartidos (AppState, Mode, ColorZone, Voicing)
  harmony.h/.cpp      <- motor de armonía diatónica (compila en host y ESP32)
  dsp.h/.cpp          <- síntesis pura (OSC, ADSR, filtro, mix) — SIN hardware
  synth.h/.cpp        <- wrapper I2S + FreeRTOS que llama a dsp
  webcfg.h/.cpp       <- protocolo de config por USB (Serial '#...' -> JSON) + NVS
  webtool/            <- editor web estático (Web Serial): timbres, drums, flasheo
  test/
    run_tests.ps1     <- runner de host (clang++/g++)
    test_harmony.cpp  <- tests de armonía (deterministas)
    test_dsp.cpp      <- tests de audio (NaN/clip/RMS/Goertzel) [creado en T0.3]
  diag_i2s_sine/      <- diagnóstico de hardware I2S (no tocar)
```

## Comandos de build
```powershell
# arduino-cli vive en "..\arduino-cli.exe" (no está en PATH)
# Compilar firmware (SOLO compile, nunca --upload en modo agente)
& "..\arduino-cli.exe" compile `
    --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi" .

# Correr tests de host (el script agrega g++ de WinLibs al PATH solo)
powershell -ExecutionPolicy Bypass -File test\run_tests.ps1
```

Toolchain de host: g++ 16.1.0 (WinLibs UCRT, vía winget) en
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_*\mingw64\bin`.
Core ESP32 3.2.0 y libs Adafruit (GFX, SSD1306, BusIO, ADS1X15) ya instalados.

## REGLAS DURAS (jamás romper)

### Armonía
1. Todas las notas deben ser **diatónicas** a la tonalidad/modo activos.
2. **Ningún acorde dominante**: no puede coexistir una 3ra mayor con una 7ma menor (b7).
3. Sin **b9** (intervalo 13 sobre la raíz), sin **b2** (intervalo 1).
4. Cantidad de notas en [1, MAX_CHORD_NOTES].
5. Voice leading: registro MIDI 36–91, conducción por mínimo movimiento con
   retención de tonos comunes (salto de bajo chico entre acordes).

### Arquitectura
- **No AMY**: el motor de síntesis es propio (`dsp.h/.cpp`). El sketch `diag_amy_min` existe sólo como referencia histórica.
- **dsp.h/.cpp** no debe incluir `ESP_I2S.h`, `freertos/`, ni ningún header de Arduino. Debe compilar con `g++` en la PC.
- Comentarios en **español**.
- Separación modular: armonía / DSP / hardware en capas independientes.

## Modos de escala implementados
- `MODE_IONIAN` (mayor): 0 2 4 5 7 9 11
- `MODE_AEOLIAN` (menor natural): 0 2 3 5 7 8 10

## Motor de audio (estado actual)
Núcleo DSP puro en `dsp.h/.cpp` (compila en host y ESP32). Cadena por muestra:
`voces (osc + ADSR + filtro SVF + LFO) + percusión -> mezcla -> [+ 4 capas del
looper] -> chorus -> reverb -> delay -> tremolo de salida -> L/R`.

Módulos (todos testeables en host, ver `test/test_dsp.cpp`):
- **Osciladores band-limited (PolyBLEP)**: saw y cuadrada sin alias en agudos.
  Paleta: BRASS (pulsos detuneados), EPIANO (FM Rhodes), STRINGS (3 saws),
  SINE, TRIANGLE, ORGAN (aditivo 4 drawbars), FLUTE (aditivo + aliento).
  ORGAN y FLUTE como **wavetable** (1 lookup/muestra) para bajar CPU.
- **`Adsr`**: envolvente A/D/S/R por timbre, sin clicks (retrigger legato).
- **`Svf`**: filtro pasa-bajos resonante de 2 polos (TPT) + envolvente de filtro.
- **`Lfo`**: vibrato (pitch) y trémolo (amplitud) con rate/depth por timbre.
- **`DelayStereo`** (L≠R, feedback, mezcla), **`Reverb`** (Freeverb-lite),
  **`Chorus`** (LFO de retardo corto, 2 tomas L/R), **tremolo de salida** sync BPM.
- **BODY**: capa de cuerpo/unísono por voz (detune fijo + drift + transitorio de
  ataque) que acerca los timbres a un sample, sin tocar los osciladores.
- **SUB-BAJO dedicado** (`synthSetBass`, `app.subBass`, on por defecto): la raíz
  del grado −1 octava como VOZ PROPIA con timbre sine, rastreada aparte del
  acorde (no se re-dispara si la raíz se mantiene). Es el "bass role" del
  HiChord: CALIBRADO por FFT contra un audio real (tools/render_ref.cpp +
  cmp.py) — con él, sub/mid pasa de 0.02 a 0.16 (ref 0.18) y el centroide clava
  4115 Hz (ref 4180). STRINGS se recalibró brillante (cutoff 4500, res 1.3)
  como saw-ensemble del acorde. Toggle web `#subbass`, persiste en la sesión.
- **STRUM** y **ARPEGIADOR con editor** (Ableton-like): 8 estilos (up/down/up-down/
  down-up/converge/diverge/random/acorde), **1-4 octavas**, **rate** sync BPM
  (1/4..1/32 con tresillos), **gate** (staccato↔legato, note-off por bloque) y
  **scale-run** (recorre la escala diatónica en vez del acorde). La secuencia la
  arma `harmonyArpSequence` (pura/testeable: octavas + scale-run); el estilo/gate
  viven en `dsp.cpp`. **6 slots de arp** editables (`ArpCfg`, `#getarp/#setarp`),
  con nombre; el `.ino` los aplica con `applyArp()`. El arp puede **fase-bloquearse
  a un MIDI clock entrante** (`dspArpAdvance`/`dspArpSetExtSync`).
- **LOOPER de 4 capas** [BETA] (audio, buffers int16 en **PSRAM**): graba SÓLO las
  voces (la batería va aparte, no se graba); todas las capas comparten una
  **posición global** (`gLpPos`) -> quedan en fase; largo maestro por la 1ra capa,
  capas nuevas graban una vuelta completa (`recLeft`) y auto-paran (no queda buffer
  sin grabar); soft-clip; estados EMPTY/REC/PLAY/OVERDUB/MUTED + STOP.
- **Percusión**: kick/snare/hi-hat con DOS motores — **SAMPLES** (por defecto;
  `drum_samples.h` generado por `tools/gen_drum_samples.py` desde
  `tools/samples/*.wav`, PCM 22050 en flash, interp. lineal, choke al re-disparo)
  o sintetizada (`dspTriggerKick/Snare/Hat`); toggle `dspDrumsSetEngine`
  (`#drumeng`). Patrón sync BPM (`dspDrumsSet(pattern,bpm)`): 12 slots —
  off/básico/4floor/síncopa + user1..8 — sobre **grilla de 16 pasos**
  (semicorcheas, máscaras uint16, `dspDrumPatternSet/Get`). **Volumen general**
  `dspDrumsSetGain` (`#drumgain`, NVS). El bus de batería lleva soft-clip y se
  mezcla al FINAL (seco, no entra al looper). **Nombres editables** por slot
  (`dspPresetNameGet/Set`, `#drumname`).
- **Generate Random Sound** (`dspRandomizeSound`) y octava global / ancho estéreo.
- **Timbres de USUARIO** `T_USER1..6` ("USR1".."USR6"): slots con **familia de
  oscilador elegible** (`EnvCfg.family`, clampeada a las 7 familias base) y el
  resto de los parámetros libres; la voz guarda `v.osc` y el render despacha por
  familia. Nacen como copias de la paleta base.
- **Editor web por USB** (`webcfg.cpp` + `webtool/`): protocolo `#...`→JSON por el
  CDC (Web Serial) — también viaja por SysEx MIDI. Webtool en pestañas: TIMBRES
  (sliders de todos los params + familia de los USR + audición por grados),
  BATERÍA (grilla 16×3 por slot, volumen, motor, **nombres**, play/stop), ARP
  (estilo/octavas/rate/gate/scale-run por slot + nombre + audición), PAQUETES
  (**export/import `.tolhuin.json` v2**: timbres + patrones + arps + nombres, para
  publicar paquetes de sonido) y FLASH (ESP Web Tools, `webtool/manifest.json`).
  **Dirty-flag** + eco de `#save` (confirmación "guardado ✓"). Transporte
  recomendado: **Web MIDI (SysEx)** — el CDC de la S3 puede fallar en RX; el MIDI
  además convive con el monitor serie. Persistencia en NVS (el `cfgv` =
  sizeof(EnvCfg) descarta blobs de timbre viejos al cambiar el struct; arps y
  nombres tienen sus propias claves).

`synth.cpp` es el wrapper fino: I2S + tarea de núcleo 0 + **cola de eventos**
thread-safe (note on/off, strum, arp, glide, sustain, tremolo, chorus, body,
random, looper, drums). Asigna la PSRAM del looper. `dspRenderBlock` (mono seco)
lo usan los tests; `dspRenderStereo` (con efectos+looper+drums) lo usa el firmware.

UI física: `input.cpp` (botones + joystick por ADS1115, con flags de orientación).
El joystick es **color** + una **capa armónica** con su click (SW; el stick NO
interviene, sólo el pulsador — en el HW-504 no se puede apretar el click e inclinar
el stick a la vez de forma fiable): **click sin grado apretado alterna MAYOR/menor**;
**click con un grado apretado MAYORIZA ese grado** (`chordQual[grado]` togglea
`CQ_DOM7`↔`CQ_DIATONIC`, rompe la regla diatónica a propósito, opt-in). Se limpia al
cambiar tonalidad/modo. **Menú de config 100% por BOTONES, sin el SW, AGRUPADO en
8 páginas temáticas** de 2-3 filas ORDENADAS POR FRECUENCIA DE USO (el OLED
muestra la página con la fila elegida invertida; `FX_PAGES` + `oledFxMenuList`):
SONIDO (TIMBRE/BODY/RANDOM), ARMONIA (KEY/MODE/RIQUEZA), RITMO (BPM/**DRUMS
on-off**/PATRON), ARP (**on-off**/PRESET), TOQUE (OCTAVE/MONO/GLIDE), AMBIENTE
(SUSTAIN/DELAY/CHORUS), SYNC (TREMOLO/CLK OUT), **MEMORIA (GUARDAR/CARGAR** con
feedback "ok!": persiste TODO en NVS desde el equipo, sin web). **NAV−/NAV+
mueven la selección** (el 1er toque sólo ABRE el menú; sostenido = auto-repeat;
timeout ~5 s); **VOICING edita** (tap = +1/toggle, largo = −1). DRUMS y ARP son
toggles instantáneos (`app.drumsOn`). La **SESIÓN** (key/modo/riqueza/octava/
bpm/timbre/patrón/arp/toggles, blob "ses" versionado) se restaura al boot: el
equipo arranca como quedó. OLED con FreeSansBold (forzar size 1: GFX escala las
FreeFonts). **El modo LOOPER se eliminó de la
UI** (dependía de PSRAM, ausente en la S3 SuperMini actual); el DSP del looper
(`dsp.cpp`/`synthLooper*`) queda pero sin cablear.

Verificación: `test/run_tests.ps1` corre armonía (23073 checks) + DSP (157
checks) en host, con WAVs opcionales `test/out_*.wav`. Para CALIBRAR timbres
contra un audio de referencia: `tools/render_ref.cpp` (renderiza el motor en
host a WAV estéreo; overrides de cut/res/fenv por argv y BASST/BASSG por env)
+ análisis FFT en Python. Firmware ~62% flash (MIDI USB+BLE NimBLE, OLED,
fuentes, samples de batería), RAM ~74% interna; el looper usaría ~2.7 MB de
**PSRAM** (ausente en la placa actual).

## Hardware objetivo
**ESP32-S3** (dual core + PSRAM OPI + USB-OTG). El looper depende de PSRAM, el
USB-MIDI de USB-OTG y la separación audio/UI de los 2 cores. El **ESP32-C3** se
evaluó y se descartó para TOLHUIN (single core, sin PSRAM, sin USB-MIDI, ~11 GPIO):
queda para el controlador BT-MIDI aparte. Target de placa chica: ESP32-S3 SuperMini.

## Estado actual (bitácora de TASKS.md)
Ver TASKS.md. **Fases 0–11**: motor DSP + efectos, funciones HiChord, controles
físicos, premium (octava/mono/glide), expresión/conectividad (MIDI USB+BLE in/out
+ **clock OUT/sync IN**), menú de config por botones, timbre FLUTE, capa BODY.
**Fases 12+ (con la placa en la mano, fw 1.x→3.2)**: prototipo S3 SuperMini
andando, SW armónico (mayor/menor + mayorizar), arranque con zoom patagónico,
anti-underrun (fastTanh + -O2 + IRAM + prio 22), EPIANO Rhodes recalibrado,
drums por samples con grilla de 16 pasos + volumen + hi-hat, timbres USR1..6,
arpegiador completo con editor y scale-run, webtool retro con paquetes
`.tolhuin.json`, GUARDAR/CARGAR + sesión al boot, y **matching HiChord medido**
(sub-bajo sine dedicado + STRINGS brillante, FFT vs referencia real).

**v2 (esta copia; la carpeta `tolhuin_chord` queda congelada como v1):**
- **LOOPER DE EVENTOS** (`evloop.h/.cpp`, puro + `test/test_evloop.cpp`): pista
  de acordes que graba EVENTOS (grado/zona/calidad/bajo), cuantizados a la
  grilla; largo redondeado a compás (máx. 8). Reloj = `synthSamplePos()`.
  Página LOOPER (REC/PLAY/CLR; el menú NO se cierra durante REC). **OVERDUB**:
  REC con el loop en PLAY graba capas nuevas encima (melodía en MONO como nota
  MIDI absoluta, o más acordes). **Cada capa captura octava Y timbre al
  grabarse** (loopOct/loopTimbre/melTimbre): TIMBRE y OCTAVE en vivo no tocan
  el loop. Los golpes de batería y los canales DRUMS/PATRN no cortan el loop.
- **CANAL** (fila CHN + GRB): qué tocan los 7 switches — ACORD / MELOD (=MONO)
  / DRUMS (sw1..3 = bombo/caja/hat; con GRB on los golpes se GRABAN cuantizados
  al patrón sonando, overdub TR) / PATRN (switches eligen preset 1..7; la
  tarjeta muestra el NOMBRE del preset).
- **GRILLA DE BATERÍA POR PATRÓN**: bars (1..2) × 4 pulsos × div (4 binaria /
  3 ternaria tresillos) -> hasta 32 pasos, máscaras uint32 (NVS dk4/ds4/dh4 +
  dbd, migra de dk2). Editable en el webtool (selects de compases/subdivisión,
  grilla dinámica); la cuantización en vivo y la tira de pasos del OLED la
  siguen. `#setdrum p k s h [bars div]`.
- **METRÓNOMO** (SYNC → MET): click sintetizado con acento en el 1, reloj
  propio (sirve sin batería), sigue BPM/MIDI clock. Suena por el bus de drums.
- **Timbre WAVE** (`T_WAVE`): wavetable morphing, 4 frames 512 (oscuro ->
  brillante) con crossfade; la posición la barre la env de filtro escalada por
  `EnvCfg.wtMorph` (slider "morph (WAVE)" en el webtool). T_COUNT=14.
- **Pantalla v3** (`oled.cpp`): tarjeta de acorde INVERTIDA (negro sobre caja
  blanca) + columna TIM/KEY/BPM; fila de grados 1..7 (bloque invertido = el que
  suena; K/S/H en canal DRUMS); osciloscopio con TRIGGER + relleno + auto-gain
  (21 px); barra de iconos con RANURAS FIJAS; tira de pasos (12/16 según la
  subdivisión) con fase del loop. Menú con barra de título invertida + icono.
- **Iconos OLED** (`icons.h`, GENERADO por `tools/gen_icons.py` — el arte se
  edita ALLÍ como grillas ASCII): 11 estados 10x10 + 11 páginas 12x12 (el orden
  de IC_PAGE sigue a FX_PAGES).
- **Flasheo**: la placa NO acepta auto-reset — BOOT+RESET manual y aparece en
  **COM11** (`--upload -p COM11`). Banner de versión: `fw=tolhuin-4.0`.

**Núcleo: sólido y testeado** (23073 + 165 DSP + 29 evloop checks host). El transporte web
confiable en esta placa es el CDC (drenado crudo del FIFO TinyUSB); el USB-MIDI
enumera pero su RX host→placa quedó pendiente de verificar.

**Prioridades sugeridas (en orden):**
1. **Hardware S3 estable** (perfboard ya andando; queda la cadena de batería
   LiPo→TP4056→buck-boost).
2. **EDIT armónico por grado** (`chordQual[7]`): PRIMER PASO HECHO — click con
   grado apretado mayoriza a dominante 7 (opt-in). Falta: más calidades (menor/
   mayor/secundarias) y persistencia de la secuencia de grados editados.
3. ~~Matching de timbres~~ HECHO y medido (fw 3.2): sub-bajo sine dedicado
   (el "bass role") + STRINGS 4500/1.3. La receta y el banco quedaron en
   `tools/render_ref.cpp`; para futuros timbres, pedir un WAV de referencia y
   calibrar por FFT (como EPIANO y STRINGS).
4. Looper: **eliminado de la UI** (sin PSRAM en la placa actual). Reintroducir solo
   si aparece una S3 con PSRAM; el DSP quedó intacto.

**Proceso:** preferir cambios chicos + flashear/probar cada uno (los bugs de
looper/OLED se habrían cazado al instante con placa en el medio).
