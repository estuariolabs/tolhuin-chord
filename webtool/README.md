# TOLHUIN — Editor Web (USB)

Editor de timbres, acordes/voicings, efectos y drum patterns + instalador de
firmware, todo desde el navegador (Web Serial: **Chrome o Edge de escritorio**).
Sin backend: es una página estática. Referencia de diseño: la companion app del
HiChord (app.hichord.shop), que usa Web MIDI + CCs/SysEx; acá el transporte es
Web Serial con un protocolo de texto (ver `webcfg.h`).

Pestañas:
- **Timbres** — 20 parámetros por timbre, en vivo, con Fábrica y Guardar (NVS).
- **Acordes** — toca los 7 grados con el motor de armonía REAL del equipo
  (tónica/modo/octava/voicing/riqueza) y visualiza el voicing con roles de voz
  (BAJO/RAÍZ/3RA/5TA/EXT), como el piano-roll del HiChord.
- **Efectos** — chorus, reverb, delay, tremolo, body, glide y sustain en vivo.
- **Drum patterns** — grilla 2×8; slots 4–7 de usuario.
- **Presets** — captura TODO el equipo a localStorage, aplica, exporta/importa
  `.json` (compartible), con opción de persistir en NVS al aplicar.
- **Firmware** — flasheo con ESP Web Tools.

## Usar el editor

1. Serví esta carpeta por HTTP (Web MIDI/Serial exigen `https://` o `localhost`):

   ```powershell
   cd webtool
   python -m http.server 8000
   # -> http://localhost:8000
   ```

2. Conectá el sinte por USB y apretá **Conectar por MIDI** (recomendado — igual
   que el HiChord: el protocolo viaja por SysEx sobre el puerto "TOLHUIN MIDI",
   convive con el monitor serie y con un DAW). Bonus: el teclado de la pestaña
   Acordes se pinta EN VIVO con lo que toques en el equipo físico.
   Alternativa: **Conectar por Serial** (mismo protocolo por el puerto CDC).
3. Pestaña **Timbres**: movés un slider y suena al instante. **Guardar en el
   equipo** persiste todo en la memoria del sinte (NVS, sobrevive al apagado).
4. Pestaña **Drum patterns**: los slots 4–7 son de usuario (los 1–3 de fábrica
   también se pueden pisar). La grilla aplica en vivo; **Probar** lo hace sonar
   al BPM elegido.

En el menú físico del sinte, los patrones de usuario aparecen como
`user1..user4` en el ítem DRUMS.

## Generar el binario de firmware (pestaña Firmware)

El botón "Instalar firmware" usa [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
y lee `manifest.json`, que apunta a `firmware/tolhuin_chord.ino.merged.bin`.
Para generarlo:

```powershell
# desde la raíz del repo
& "..\arduino-cli.exe" compile `
    --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi" `
    --output-dir webtool\firmware .
```

El core ESP32 3.x deja ahí `tolhuin_chord.ino.merged.bin` (bootloader +
particiones + app, flasheable desde offset 0). Antes de flashear, **desconectá
el editor**: el puerto no se comparte.

Para publicarlo: subí `webtool/` a GitHub Pages (o cualquier hosting estático
con HTTPS) y actualizá `firmware/` en cada release.

## Protocolo serie (por si querés scriptearlo)

Comandos de texto por línea sobre el mismo CDC del monitor (115200). Entran con
`#`, las respuestas son una línea JSON. Ver `webcfg.h` para la lista completa:
`#hello`, `#getenv/​#setenv`, `#factory`, `#getdrum/#setdrum`, `#drums`,
`#noteon/#noteoff`, `#chord/#chordoff`, `#save/#load`.
