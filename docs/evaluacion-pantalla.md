# Tolhuin: pantalla 240 × 240 y port web

## Resultado

La ampliación a una TFT color de 240 × 240 es técnicamente viable en ESP32-S3.
Recomiendo un **módulo ST7789 por SPI de 3,3 V**, manteniendo el DAC PCM5102 y
el ADS1115 actuales. La elección del módulo exacto queda condicionada a su
consumo, conector y dimensiones. **No se ha validado físicamente ni se ha
flasheado el equipo.** El navegador permite comparar sonido, lógica y diseños;
no mide los plazos de DMA, la alimentación ni la temperatura del ESP32.

La resolución toma como referencia al M-VAVE FM-1. No implica que su panel o
su firmware sean compatibles eléctricamente con Tolhuin: FM-1 usa JieLi AC791N;
Tolhuin usa ESP32-S3. Se propone trasladar ideas de navegación y legibilidad,
no ejecutar el firmware FM-1 sobre ESP32.

## Fuente y alcance del port

Fuente: https://github.com/estuariolabs/tolhuin-chord

Base revisada: `c61a07c16a8dfaeaead07646cc7d6c57997ade9f`, configuración predeterminada
`TOLHUIN_HW_REV=1`. `config.h` es la referencia ejecutable. La guía de conexiones
y los netlists contienen revisiones antiguas y contradicciones: por ejemplo el
netlist ocupa GPIO15/16 mientras el firmware vigente no, y la guía aclara que
GPIO21 no sale en esta SuperMini. El pinout nuevo debe cotejarse contra el armado
real; no se deben aplicar los netlists viejos a esta propuesta.

Se compilan directamente, sin cambios en sus archivos fuente:

- `tolhuin_chord_v2.ino`: setup, loop, selección de acordes, menú, sesión y looper.
- `dsp.cpp`, `harmony.cpp`, `evloop.cpp`: motor completo.
- `input.cpp`: antirrebote, pulsaciones largas, histéresis, navegación y colores.
- `midi.cpp`: parser original, SysEx, reloj y generación MIDI.
- `webcfg.cpp`: comandos y formato de sesión.
- `oled.cpp`: dibujo original, iconos y fuentes reales de Adafruit GFX.

`synth.cpp` se genera en una carpeta ignorada con **una única transformación
comprobada**: la tarea de audio hace una iteración por callback en vez de un
bucle infinito. Se conserva su cola de eventos, DSP, scope, contador de muestras
y conversión de PCM. Se añaden dos getters para leer los buffers L/R.
`provenance.json` publica hashes SHA-256 de los fuentes originales.

Los adaptadores HAL sustituyen GPIO, ADC/I2C, I2S, FreeRTOS, USB, BLE y NVS.
La entrada del joystick virtual ocupa el rango ADC inicial ±2000 alrededor del
centro, deshaciendo `JOY_SWAP_XY`. Conserva las zonas y tiempos del código real;
no simula el ruido eléctrico del ADS1115. NVS usa los blobs originales en memoria,
respaldados en localStorage y exportables a JSON. No escribe la NVS del equipo
salvo que se active explícitamente la duplicación de comandos.

El port **no es un emulador binario Xtensa de la ROM/ELF del ESP32**: es el
firmware C++ recompilado para WebAssembly con periféricos virtuales. Se mantiene
44.100 Hz, bloques de 256 muestras, control a pasos de 1 ms y PCM estéreo de 16 bits. Web Audio puede
remuestrear hacia la frecuencia de la salida física del sistema. Latencia,
precisión float y planificación del navegador difieren de la placa.

El firmware completo vive en una única instancia en AudioWorklet. El hilo de
interfaz recibe imágenes, estado y MIDI; no implementa armonía ni síntesis en JS.
Se evita un motor visual separado que pudiera divergir del instrumento.

## Presupuesto de pantalla

| Recurso | Cálculo | Resultado |
|---|---|---|
| Buffer RGB565 | 240 × 240 × 2 | 115.200 B = 112,5 KiB |
| Doble buffer | 2 × 115.200 | 225 KiB |
| Cuadro completo SPI 40 MHz | 115.200 × 8 / 40.000.000 | 23,04 ms mínimo teórico |
| 20 fps completos | 115.200 × 20 | 2,304 MB/s |
| 30 fps completos | 115.200 × 30 | 3,456 MB/s |
| Presupuesto de audio | 256 / 44.100 | 5,805 ms por bloque |

Los 23 ms no incluyen comandos, copias, rasterización ni pausas del bus. Una
transferencia bloqueante de pantalla no debe ejecutarse en la tarea de audio.
Recomiendo actualización por rectángulos sucios a 20–30 fps: acorde, flags y
menú sólo cuando cambian; scope a 15–20 fps. No son tasas medidas en placa.

Conservar la separación actual: audio en core 0, lógica/display en core 1.
Priorizar audio sobre SPI y limitar el tiempo de render. Para DMA, usar buffers
internos aptos para DMA o pequeños buffers de líneas; no asumir que todo buffer
PSRAM sirve directamente. En una unidad sin PSRAM, comenzar con una estrategia
por franjas, no con doble buffer. El motor ya reserva memoria para FX y looper.

## Conexión propuesta

Según los GPIO libres de `config.h` y la reserva declarada en la guía:

| TFT ST7789 | ESP32-S3 | Observación |
|---|---|---|
| VCC / GND | Alimentación del módulo / GND común | Confirmar módulo de lógica 3,3 V |
| SCLK | GPIO2 | SPI dedicado |
| MOSI / SDA del TFT | GPIO4 | Es MOSI SPI; no conectar al bus I2C |
| DC | GPIO15 | Libre en el firmware vigente; revisar PCB física |
| CS | GPIO16 | Preferir módulo con CS; algunos lo atan internamente |
| RST | GPIO1 | Evita GPIO21, no expuesto según la guía |
| BL | Alimentación según módulo | Limitación/driver adecuado; no directo al GPIO |

Se mantienen I2S 38/39/40, I2C 41/42 y USB 19/20. No usar GPIO33–37 (PSRAM),
GPIO0/3/45/46 (strapping), GPIO48 (LED), ni reutilizar GPIO18 en el prototipo
con pad roto. HOLD es GPIO18 en REV1 y GPIO6 en REV2; el port usa REV1.

La retroiluminación aumenta el consumo y puede introducir ruido en el DAC.
Medir corriente total, margen del regulador, ruido de audio con BLE activo y
la caída de alimentación con brillo máximo. Confirmar tamaño físico del panel,
zona activa, tornillos, flex y visibilidad en el gabinete antes de diseñar PCB.
No hay una medición de autonomía ni una lista de materiales cerrada todavía.

## Propuestas de interfaz

Las tres variantes están en `emulator/display240.cpp`, usan Adafruit GFX y
devuelven el mismo framebuffer RGB565 de 240 × 240 que puede transferirse al TFT.
El navegador sólo convierte esos píxeles a RGBA. No son maquetas HTML del display.

1. **Performance**: acorde grande, tono/modo, timbre, BPM, grados activos y onda.
   Inspiración: jerarquía y lectura directa de Felucca y sus derivados.
2. **Rhythm**: acorde, pasos de transporte, batería y estado del looper.
   Inspiración: pantallas organizadas por pasos de la familia de grooveboxes FM-1.
   La grilla indica posición, no pretende mostrar un patrón de golpes inventado.
3. **Scope**: Lissajous XY del audio real, acorde y estados, ampliando la idea
   visual que ya usa el OLED de Tolhuin.

El scope usa ganancia visual acotada sobre muestras reales; no modifica audio.
Todas presentan el menú original con tres filas, selección y valores originales.
No agregan parámetros musicales. El botón de menú y el joystick conservan las
funciones y timeout de 8 s; HOLD y VOICING conservan el umbral largo de 450 ms.
Se puede volver a ver el framebuffer OLED 128 × 64 original como referencia.

Para integrar al ESP32, reutilizar `Display240View`/`display240Render`, pasar los
valores ya calculados por el sketch y transferir RGB565 con LovyanGFX, TFT_eSPI
o esp_lcd. Adaptar a franjas/rectángulos según memoria. Este trabajo entrega
el renderer portable y la evaluación; no declara un driver ST7789 ya probado.

## MIDI, USB y Bluetooth

| Vía | Implementado en el laboratorio | Condición física |
|---|---|---|
| Web MIDI | Entradas/salidas, notas y clock; routing de salida opt-in | Puerto USB MIDI del Tolhuin o BLE MIDI emparejado en el SO |
| Web Serial | Comandos originales #hello/#state/#degree, monitor JSON | Tolhuin CDC, 115200 baud, puerto no ocupado |
| Web Bluetooth | GATT BLE MIDI estándar, notificaciones y notas/clock out | Chrome/Edge y SO compatibles; servicio no reservado por otro programa |
| MIDI SysEx USB | F0 7D 01 comando / F0 7D 02 respuesta | Permiso SysEx y salida USB MIDI seleccionada |

El firmware original **sólo despacha configuración SysEx por USB**. Su camino BLE
recibe notas y clock, pero no llama al parser de configuración; no se ofrece
consulta #state por BLE como si estuviera implementada. Para estado usar CDC o
USB MIDI. Si Web Bluetooth no expone BLE MIDI, emparejarlo en el SO y elegirlo
por Web MIDI. No usar WebUSB para reclamar una interfaz ya manejada por MIDI/CDC.

Conectar un controlador o Tolhuin por MIDI hace sonar el DSP del port. Las notas
MIDI directas no cambian el grado de AppState: ése es el comportamiento original.
Se ven en el monitor y en el scope. El firmware no transmite la telemetría completa
de sus botones/joystick: no es posible reconstruir de forma exacta su estado de
UI sólo desde note-on/off. El laboratorio permite consultar #state físico y
comparar los campos disponibles, o duplicar comandos de edición al equipo.
La salida física de notas requiere activar “Enviar lo que toco”.

Las APIs requieren HTTPS o localhost y permiso iniciado por un gesto. Safari y
Firefox no ofrecen todas estas APIs. Si Sites se muestra dentro de un iframe sin
permisos de dispositivo, abrir la URL del sitio directamente en una pestaña.
La nube donde se construyó no tiene acceso a los dispositivos USB/BT del usuario.

## Evidencia y límites

- Cuatro suites originales aprobadas: armonía, conducción, DSP y evloop.
- 15 pruebas de integración del port, incluida grabación/reproducción desde el
  menú y restauración de sesión en una instancia nueva.
- Comparación de 307.200 muestras entre compilación nativa y WASM con la misma
  secuencia de controles: NRMSE ≈ **0,0002392 %**, error máximo 1 LSB PCM16.
  Es evidencia para esa secuencia, no prueba de identidad de todas las ejecuciones.
- Prueba del AudioWorklet en un contexto sin DOM/Node: inicialización, audio,
  MIDI, controles, imágenes y transferencia de estado.
- Pruebas del transporte BLE con running status, note-off por velocity cero,
  clock/realtime y SysEx fragmentado. No certifican un dispositivo físico.
- Browser QA visual/interactivo pendiente: el servicio de preview devolvió
  `relay could not open browser port 4173`, y el navegador compartido falló con
  `the gaas-browser node_repl sandbox cannot be disabled`.
- USB, BLE, consumo, FPS y carga del ESP32 pendientes de banco físico.

El registro reproducible está en `test-results.json` y los fuentes de prueba en
`emulator/tests`. No se modificaron ni se flashearon los dispositivos.

## Referencias

- Firmware Tolhuin: https://github.com/estuariolabs/tolhuin-chord
- Colección y familias FM-1: https://github.com/aroum/fm1-custom-fw
- SDK abierto FM-1 y simulador: https://github.com/m-vave/fm1_open
- Adafruit GFX 1.12.4: https://github.com/adafruit/Adafruit-GFX-Library/tree/1.12.4

Las interfaces son propuestas originales inspiradas en esas familias, no copias
pixel a pixel ni incorporación de recursos visuales propietarios.
