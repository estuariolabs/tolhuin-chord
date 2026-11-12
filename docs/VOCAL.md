# Timbre VOCAL — voz por modelado físico

Voz sintética que toca notas y acordes como un timbre más (`T_VOCAL`, índice 17,
el último). Sin samples ni modelos neuronales.

- **Etapa 1:** vocales sostenidas A E I O U (este documento, primera parte).
- **Etapa 2:** letra cantada — texto → sílabas, consonantes y controles. Ver
  [Etapa 2](#etapa-2--letra-cantada) más abajo.

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
- Cantidad de aire y cociente abierto no son editables.

## Etapa 2 — letra cantada

### Propuesta

Un sintetizador de acordes no "lee" un texto: lo **canta**. Por eso el texto a
voz se resuelve como en una partitura con letra: **una sílaba por ataque**. Cada
vez que tocás una nota o un acorde, el coro dice la sílaba siguiente; la vocal se
sostiene mientras dure la nota y la consonante final se pronuncia al soltar. Al
terminar la letra, vuelve a empezar.

Todo sigue siendo modelado físico sobre el mismo tubo: las consonantes no son
samples ni filtros aparte, son **gestos** que cierran o estrechan el tracto.

### Diseño

| Parte | Qué hace | Dónde |
|-------|----------|-------|
| Texto → fonemas | Reglas de la ortografía española (c/qu, g/gu/gü, ll, rr, x, y, h muda). Seseo y yeísmo. Sin diccionario. | `vocal_text.cpp` |
| Silabeo | Diptongos, triptongos e hiatos (la tilde rompe el diptongo: pa-ís), grupos pl/pr/tr…, coda. | `vocal_text.cpp` |
| Gestos | Cada sílaba se vuelve una cola de tramos con objetivos de forma, constricción, sonoridad, ruido y velo. | `VocalArtic` en `vocal.h/.cpp` |
| Tracto | Constricción en un lugar del tubo, ruido inyectado aguas abajo de ella y **rama nasal** (juntura de 3 puertos en el velo, 13 secciones). | `VocalTract::setShape` / `tickFull` |
| Motor | Decide cuándo avanza la sílaba, suaviza sonoridad y ruido por muestra, corre el control cada 64 muestras. | `dsp.cpp` |

Fonemas (22): vocales `a e i o u`; oclusivas `p b t d k g`; fricativas `f s j`;
africada `ch`; nasales `m n ñ`; líquidas `l r rr`; `y` (y / ll).

| Tipo | Cómo se produce en el tubo |
|------|-----------------------------|
| Oclusivas sordas (p t k) | Cierre total y sin voz ~70 ms, después explosión de ruido en el lugar del cierre. |
| Oclusivas sonoras (b d g) | Cierre con poca voz y explosión breve; ligadas (entre vocales) no llegan a cerrar. |
| Fricativas (f s j) | Estrechamiento + ruido aguas abajo, sin voz. |
| Africada (ch) | Cierre y suelta en fricción. |
| Nasales (m n ñ) | Boca cerrada, velo abierto: el sonido sale por la rama nasal. |
| l, y | Estrechamiento parcial con voz. |
| r / rr | Uno o tres toques breves contra el alvéolo. |

**Coarticulación:** durante la consonante la lengua ya va hacia la vocal que
sigue, porque la constricción se aplica *sobre* la forma de esa vocal.

**Qué cuenta como un ataque.** Una nota nueva, un acorde (todas sus notas juntas),
un rasgueo, una nota en modo mono o un acorde con glide. Notas a menos de 120 ms
entre sí son el mismo ataque.

**Qué no sale del modelo** (son parámetros elegidos a mano, por consonante):
duraciones de los gestos, nivel del ruido y su color (un filtro de 1 polo sobre el
ruido blanco). Los lugares de articulación son aproximados.

### Controles

**Editor web** (pestaña Timbres, con el timbre VOCAL elegido): botones de vocal,
campo de letra, "Cargar letra", "Sin letra", "Volver al inicio" y la casilla
"cantar la letra".

**Equipo** (página **VOZ**, al final del menú): `VOC` elige la vocal A/E/I/O/U,
`LET` prende o apaga la letra cargada, `INI` vuelve a la primera sílaba. La letra
no se puede escribir desde el equipo: se carga por web y queda guardada.

**Serial:**

| Comando | Efecto |
|---------|--------|
| `#vtext hola mundo` | carga la letra (`#vtext` solo la borra) |
| `#vlyr 0` / `#vlyr 1` | usar o no la letra |
| `#vreset` | volver a la primera sílaba |
| `#vowel 0..4` | vocal A E I O U del timbre |
| `#vget` | estado: texto, sílaba actual, cantidad |

El texto viaja en ASCII (el SysEx MIDI es de 7 bits): `~n` = ñ, `'a` = á, `:u` = ü.
El editor web lo convierte solo. Máximo 95 caracteres y 48 sílabas.

**NVS:** claves nuevas `vtx` (texto) y `vto` (on/off), con GUARDAR. No tocan el
layout de timbres. Sin esas claves el equipo arranca sin letra, como en la etapa 1.

### Costo (host, µs por bloque, mismo banco que la etapa 1)

| Caso | 1 voz | 4 voces | 12 voces |
|------|-------|---------|----------|
| VOCAL sin letra | 22 | 28 | 47 |
| VOCAL con letra (3 sílabas por segundo) | 35 | 41 | 60 |
| BRASS (referencia, misma corrida) | — | 23 | 54 |

La letra suma un costo fijo (control cada 64 muestras, ruido y rama nasal); no
crece con la polifonía. RAM: unos 3,5 KB estáticos en total (tractos, gestos, letra). **En la placa:
pendiente de medir**; hay que repetir la medición de la etapa 1 con una letra
cargada.

### Prueba en la placa

1. Los pasos de [Medición en la placa](#medición-en-la-placa), y además:
2. `#timbre 17`, `#vtext hola mundo`, tocar cuatro notas: debe decir *o · la · mun · do*.
3. Tocar un acorde: una sola sílaba para todo el acorde.
4. `#perf reset`, tocar acordes con letra ~20 s, `#perf`.
5. Menú → página VOZ: cambiar `VOC`, apagar y prender `LET`, usar `INI`.
6. GUARDAR, reiniciar: la letra tiene que seguir ahí.

### Límites de la etapa 2

- Es canto silábico, no habla: no hay prosodia, acento ni duración propia de las
  sílabas; el ritmo lo ponés vos al tocar.
- Una sola letra para todas las voces (el tracto es compartido).
- La pronunciación es por reglas: fallan las excepciones (la x de "México", la w,
  palabras de otros idiomas). No distingue s/z ni ll/y.
- Las consonantes atrasan la vocal entre 20 y 130 ms respecto del ataque.
- Con tempos muy rápidos se descartan los gestos que no llegaron a decirse.
- b/d/g sonoras suenan casi mudas durante el cierre (no se modela la vibración de
  las paredes).
- Las consonantes no fueron ajustadas a oído ni contra grabaciones: los tests
  miden que cada tipo se comporte como debe (silencio, ruido agudo, murmullo
  nasal), no que se entiendan. Hace falta escucharlas.
- El firmware no se compiló con `arduino-cli`; `synth.cpp`, `webcfg.cpp` y el
  `.ino` sólo pasaron un chequeo de sintaxis en PC contra cabeceras simuladas.

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

- Para las consonantes: la descripción articulatoria estándar del español (lugar y
  modo de articulación) y la técnica general de síntesis articulatoria por
  constricciones y ramas laterales sobre guías de onda (Smith, obra citada). Los
  valores numéricos son propios.

Dependencias nuevas: ninguna.
