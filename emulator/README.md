# Tolhuin Firmware Lab

Port del firmware original ESP32-S3 a WebAssembly con HAL virtual, audio estéreo
en AudioWorklet, OLED original y tres renderers C++ RGB565 240 × 240. Ver
[la evaluación](../docs/evaluacion-pantalla.md) para alcance, límites y hardware.

## Construir

Requiere Python 3, Node.js, g++ y Emscripten 4.0.23. No hay dependencias npm.

```sh
# Si Emscripten no está en PATH:
export EMXX=/ruta/a/emsdk/upstream/emscripten/em++
npm run build
npm test
```

Servir `dist/` por HTTPS o localhost. Para uso local fuera del entorno gestionado:

```sh
python3 -m http.server 8000 --directory dist
```

Abrir http://localhost:8000 y pulsar **Encender audio**. Las teclas 1–7 son
acordes, flechas joystick, H HOLD, V VOICING, D batería, M menú y espacio SW.
Mantener H/V más de 450 ms usa los gestos originales. También funciona con mouse
y táctil. Navegar el menú con ↑↓; editar con ←→. La lectura del joystick conserva
los 12 ms por poll y tres lecturas estables: los gestos demasiado cortos no cuentan.

El guardado del menú o #save persiste blobs NVS del firmware en el navegador.
Exportar/importar mueve esa sesión entre equipos. Los comandos del firmware no
se duplican al equipo físico salvo activación explícita en Conexiones.

## Archivos

- `hal/`: periféricos virtuales; nunca incluir en la compilación de Arduino.
- `bridge.cpp`: ABI y carga directa del sketch `.ino`.
- `build.py`: único ajuste de la tarea I2S, build y hashes de procedencia.
- `display240.*`: vistas C++ portables, sin DOM ni lógica musical duplicada.
- `vendor/`: Adafruit GFX 1.12.4 y fuentes (licencia BSD incluida).
- `web/`: controles, AudioWorklet, adaptadores Web MIDI/Serial/Bluetooth.
- `tests/`: suites de integración, comparación nativa, worklet y BLE.
- `generated/`, `tests/out/`, `dist/`: generados; no editar a mano.

`npm test` ejecuta las cuatro suites originales en paralelo, genera audio nativo,
lo compara con WASM y ejecuta integración/worklet/BLE. Cada prueba falla con salida
no cero ante divergencias. No hay test con hardware real en esta máquina.

## Continuar el trabajo

Para cambios musicales editar los fuentes originales de la raíz y reconstruir:
no duplicar funciones armónicas en JS. Para probar otra pantalla editar el renderer
C++ y conservar la selección de vistas en la UI. Para incorporar un driver TFT,
usar `Display240View` como contrato y respetar los presupuestos de memoria/DMA
descritos en la evaluación. El firmware OLED original sigue intacto.

La app no emula instrucciones Xtensa: recompila el firmware y virtualiza hardware.
El esquema de tareas y tiempos físicos del ESP32 necesita validación en placa.
