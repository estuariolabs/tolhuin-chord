# TOLHUIN Chord v0.2.0

Esta versión amplía TOLHUIN como instrumento de interpretación y exploración
armónica.

## Novedades

### HBRASS

Nuevo timbre de metales expresivo, desarrollado mediante comparación auditiva y
medición espectral y optimizado para el ESP32-S3. HBRASS se incorpora como una
voz independiente: el timbre BRASS original permanece disponible.

### Looper funcional

El looper permite capturar progresiones y reproducirlas con el estado musical
del instrumento, integrándose con timbres, tempo y controles de interpretación.

### Conducción de voces seleccionable

TOLHUIN ahora separa el voicing instantáneo del algoritmo que conecta un acorde
con el siguiente:

- **PARALELO:** posiciones directas y estables, con movimiento de bloque.
- **SUAVE:** conserva tonos comunes y minimiza desplazamientos.
- **CORAL:** distribuye las voces en registros compactos y evita cruces y saltos
  innecesarios.
- **CONTRARIO:** favorece el movimiento contrario entre bajo y voz superior sin
  sacrificar registro ni estabilidad.

El editor web acompaña estos modos con un visualizador de distribución y
movimiento de voces.

## Hardware compatible

El binario adjunto está compilado exclusivamente para **TOLHUIN prototipo 1**:

```text
TOLHUIN_HW_REV=1
```

No se publica un binario para prototipo 2. Antes de instalar en un armado
distinto, revisar el pinout en `config.h` y `GUIA_CONEXIONES_S3_SUPERMINI.md`.

## Verificación

La versión se publica después de ejecutar las suites host de armonía, conducción
de voces, DSP y event loop, y de compilar el firmware para ESP32-S3 con revisión
de hardware 1. Los hashes y métricas finales se incluyen en la publicación de
GitHub.
