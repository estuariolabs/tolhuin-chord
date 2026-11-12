# Timbre VOCAL — voz por modelado físico (etapa 1)

Voz sintética que toca notas y acordes como un timbre más (`T_VOCAL`, índice 17,
el último). Sin samples ni modelos neuronales. Etapa 1: **vocales sostenidas
A E I O U**. No hay texto a voz, fonemas, consonantes ni letras.

## Modelo

| Parte | Qué es | Dónde |
|-------|--------|-------|
| Fuente glótica | Derivada del flujo glótico, pulso polinómico con cierre abrupto (familia Rosenberg / KLGLOTT88). El salto del cierre se corrige con PolyBLEP. Ruido de aspiración modulado por el flujo, más una fuga mínima constante. | `vocal.h` |
| Tracto vocal | Guía de ondas de Kelly-Lochbaum: 22 secciones cilíndricas = 17,5 cm a 44,1 kHz. Reflexión parcial en glotis y labios. | `vocal.h` |
| Vocales | Una función de área por vocal (8 puntos de control, glotis → labios). Cambiar de vocal = deslizar áreas (~45 ms). | `vocal.cpp` |
| Coro | Cada nota aporta sólo su fuente glótica; todas comparten **dos tractos** (L de 17,5 cm y R de 16,7 cm). | `dsp.cpp` |

**Por qué un tracto compartido.** El tracto es un sistema lineal: sumar las
fuentes y pasarlas por un tubo da lo mismo que un tubo por nota con la misma
vocal. Así la polifonía cuesta un oscilador por nota y el tubo es un costo fijo.
Consecuencia: todas las voces VOCAL que suenan a la vez comparten la vocal (manda
el timbre de la última nota iniciada).

**Por qué no un banco de formantes.** Un banco fija frecuencia, ancho y nivel de
cada formante a mano y por separado. Acá salen de la geometría del tubo: al pasar
de una vocal a otra todos los formantes se mueven juntos y de forma coherente, y
el modelo se puede extender (largo de tracto, constricciones) sin rediseñar
filtros. El banco es algo más barato; el costo medido en host no justificó
resignar el modelado.

**Áreas de las vocales.** No son datos de MRI ni de otro proyecto. Las genera
`tools/vocal_fit.cpp`: parte de formas articulatorias cualitativas y las ajusta
hasta que los formantes **medidos sobre el propio modelo** caen en estos objetivos
(valores típicos redondeados de vocales del español, voz masculina adulta; son
una guía de diseño, no una cita: verificar contra la bibliografía antes de
publicarlos como dato fonético):

| Vocal | Objetivo F1/F2 (Hz) | Medido F1/F2/F3 (tubo L) |
|-------|---------------------|--------------------------|
| A | 700 / 1200 | 710 / 1195 / 2515 |
| E | 450 / 1950 | 450 / 1935 / 2590 |
| I | 280 / 2300 | 280 / 2270 / 2910 |
| O | 480 / 900  | 495 / 870 / 2545 |
| U | 310 / 720  | 310 / 710 / 3165 |

Tubo uniforme de control: 490 / 1470 / 2455 Hz (teoría: 500 / 1500 / 2500).

## Cómo se controla (sin UI nueva)

VOCAL reutiliza campos existentes de `EnvCfg` (no cambia su tamaño ni el NVS):

| Campo | En VOCAL |
|-------|----------|
| `wtMorph` (slider "morph") | vocal: 0 = A, 0.25 = E, 0.5 = I, 0.75 = O, 1 = U (interpola) |
| `cutHz` | brillo de la voz |
| `vib`, `lfoRate`, `trem` | vibrato y trémolo |
| `atkMs`, `decMs`, `sus`, `relMs` | envolvente |

Por serial: `#timbre 17`, `#setenv 17 wtmorph=0.5`. `res`, `fEnvOct` y la
envolvente de filtro no tienen efecto en este timbre.

## Compatibilidad

- `T_VOCAL` se agregó **después** de `USR6`: ningún índice existente cambió.
- NVS: el layout pasa de 17 a 18 timbres. `nvsLoad` lee los 17 guardados tal cual
  y deja VOCAL de fábrica (sin esa rama, actualizar borraba las ediciones).
- `ENG_VOCAL` (= 9) es un valor de `EnvCfg.family` (motor), no un índice de timbre.
- Con el timbre sin usar, el código nuevo no se ejecuta. Los WAV de los 9 timbres
  que exporta `test_dsp` son idénticos byte a byte antes y después del cambio.

## Costo

**En host** (`tools/bench_dsp.cpp`, mejor de 5, con chorus + body + efectos). Sirve
para comparar timbres entre sí; **no predice microsegundos de la placa**.

| Caso | µs por bloque (PC) |
|------|--------------------|
| Sólo efectos, sin voces | 8 |
| BRASS 4 / 12 voces | 26 / 67 |
| STRINGS 12 voces | 52 |
| HBRASS 7 voces (su tope actual) | 27 |
| **VOCAL 1 / 2 / 4 / 8 / 12 voces** | **22 / 23 / 28 / 37 / 50** |
| VOCAL con un solo tracto, 1 / 4 / 12 | 18 / 24 / 48 |

RAM: dos `VocalTract` estáticos, unos 1 KB en total. No usa PSRAM ni memoria
dinámica.

**En la placa: pendiente de medir** (ver abajo). Hasta tener esos números no hay
una polifonía vocal segura declarada; el timbre no tiene tope propio de voces.

Si la medición no da margen: `#define VOCAL_STEREO_TRACTS 0` en `config.h` (un
solo tracto) y volver a medir. No se toca la polifonía ni el resto de los timbres.

## Medición en la placa

1. Compilar y flashear como siempre (BOOT+RESET manual).
2. `#perf` → el campo `psram` dice cuánta PSRAM detectó el core (0 = no hay).
   Anotar también `heapFree` / `heapMin`.
3. `#hello` debe listar 18 timbres con `VOCAL` al final.
4. Para cada caso: `#timbre 17`, `#perf reset`, tocar, esperar ~20 s, `#perf`,
   soltar. Anotar `maxUs` (presupuesto: 5805) y `lateBlocks` (debe ser 0).
   - 1 voz: `#noteon 60`
   - 2 voces: `#noteon 60` + `#noteon 67`
   - 4 voces: + `#noteon 64` + `#noteon 72`
   - acorde real desde los botones, con sub-bajo, chorus, batería y BLE conectado
     (el peor caso).
5. Repetir el caso de 4 voces con un timbre conocido (`#timbre 0`, BRASS) para
   tener la referencia en la misma sesión.
6. NVS: antes de flashear, editar un timbre y `#save`; después de flashear,
   comprobar que la edición sigue ahí.
7. Escucha: `#setenv 17 wtmorph=0`, `0.25`, `0.5`, `0.75`, `1`.

## Límites conocidos de esta etapa

- Una sola vocal a la vez para todas las voces VOCAL.
- En agudos (por encima de ~C6) las vocales cerradas (O, U) pierden nivel e
  identidad: la fundamental queda por encima de sus formantes. Es propio del
  modelo y de la voz real.
- El nivel varía con la nota (hasta ~±7 dB) según qué armónico cae sobre un
  formante. No hay compensación.
- El F3 de la U quedó alto (3165 Hz frente a ~2400): F1 y F2 sí coinciden.
- Largo de tracto fijo (voz adulta); sin tipos de voz ni formante del cantante.
- La respuesta del tubo se repite en espejo por encima de 11 kHz (una muestra por
  sección); la fuente tiene muy poca energía ahí.
- Fuera de alcance: texto a voz, fonemas, consonantes, nasales, diccionario,
  cambios de UI, cantidad de aire y cociente abierto editables.

## Referencias (conceptuales; no se copió código)

- J. L. Kelly y C. C. Lochbaum, "Speech synthesis", 4th Int. Congress on
  Acoustics, 1962 — tracto como tubos concatenados.
- A. E. Rosenberg, "Effect of glottal pulse shape on the quality of natural
  vowels", JASA 49, 1971 — pulsos glóticos polinómicos.
- D. H. Klatt y L. C. Klatt, "Analysis, synthesis, and perception of voice
  quality variations among female and male talkers", JASA 87, 1990 — KLGLOTT88.
- V. Välimäki y A. Huovilainen, "Antialiasing oscillators in subtractive
  synthesis", IEEE Signal Processing Magazine, 2007 — corrección tipo BLEP.
- J. O. Smith III, *Physical Audio Signal Processing* (libro en línea, CCRMA) —
  guías de onda y junturas de dispersión.
- B. H. Story, I. R. Titze y E. A. Hoffman, "Vocal tract area functions from
  magnetic resonance imaging", JASA 100, 1996 — referencia de plausibilidad; no
  se usaron sus datos.
- CHIMERA: referencia conceptual indicada para el proyecto (fuente glótica +
  tracto físico). No se consultó ni se copió su código en esta etapa.

Dependencias nuevas: ninguna.
