# TOLHUIN — Guía de conexiones (ESP32-S3 SuperMini)

> **Pinout ajustado a la placa REAL** (verificado con hardware en mano, prototipo 1
> andando). El audio (I2S 38/39/40) y el I2C (41/42) ya funcionan, y el teclado usa
> **8, 9, 17, 45, 5, 14, 10** + **18, 7, 11, 12, 13**. La placa **no** saca 21/47.
>
> Este mapa es el que está en `config.h`: **las tablas de abajo mandan**. (Una versión
> vieja de esta nota decía que 8/9/17/18/45 no estaban disponibles: es FALSO en esta
> placa, el prototipo 1 los usa y funcionan.)
>
> Todo funciona a **3.3 V**. Masa (GND) **común** para todo.

## Mapa del teclado (según el cableado físico confirmado)
| Señal | GPIO | | Señal | GPIO |
|-------|------|-|-------|------|
| Tecla I   | **8**  | | Tecla VII | **10** |
| Tecla II  | **9**  | | NAV−      | **11** |
| Tecla III | **17** | | NAV+      | **12** |
| Tecla IV  | **45** | | HOLD      | **18** |
| Tecla V   | **5**  | | VOICING   | **7**  |
| Tecla VI  | **14** | | Joystick SW | **13** |

> Audio e I2C **no cambian**: I2S BCK/WS/DIN = **38/39/40**, I2C SDA/SCL = **41/42**.
>
> ⚠️ **GPIO45 (Tecla IV) es pin de strapping.** Seguro **sólo como botón a GND, sin
> resistencia externa** (al arrancar queda en bajo, lo correcto).

---

## 1. Resumen por pin de la ESP32-S3 SuperMini

Cada fila es un cable. Buscá el número de GPIO impreso en la serigrafía de tu placa.

### Audio — PCM5102 (I2S)
| ESP32-S3 | PCM5102 | Nota |
|----------|---------|------|
| **GPIO38** | **BCK** (BCLK) | reloj de bit |
| **GPIO39** | **LCK** (LRCK/WS) | reloj de palabra L/R |
| **GPIO40** | **DIN** | datos de audio |
| GND | **SCK** | ⚠️ obligatorio a GND (activa el PLL interno) |
| 3V3 | **VIN** (VCC) | el módulo tiene su regulador; 3.3 V va bien |
| GND | **GND** | |
| 3V3 | **XSMT** | mute: en ALTO = suena (si queda en bajo, no hay audio) |
| GND | **FMT** | formato I2S |
| GND | **FLT / DEMP** | dejar en GND (filtro/de-énfasis por defecto) |

Salida: **LOUT / ROUT → jack 3.5 mm** (LOUT=tip, ROUT=ring, GND=sleeve).

### Pantalla OLED + ADS1115 (bus I2C compartido)
| ESP32-S3 | Va a | Nota |
|----------|------|------|
| **GPIO41** | **SDA** de OLED **y** ADS1115 | mismo cable a los dos |
| **GPIO42** | **SCL** de OLED **y** ADS1115 | mismo cable a los dos |
| 3V3 | VCC de OLED y VDD de ADS1115 | |
| GND | GND de OLED y ADS1115 | |
| VDD | **ADDR** del ADS1115 | ADDR→VDD = dirección **0x49** (la que usa el firmware) |

> **Pull-ups I2C:** SDA(41) y SCL(42) necesitan **4.7 kΩ a 3V3**. Muchos módulos
> OLED/ADS ya los traen; si ninguno los tiene, agregá uno en cada línea. Si los traen
> los dos, igual funciona (quedan en paralelo).
>
> Direcciones: OLED = **0x3C**, ADS1115 = **0x49** (ADDR→VDD en esta placa).

### Joystick HW-504 → **al ADS1115** (no al ESP32)
| Joystick | Va a |
|----------|------|
| VRx | **A0** del ADS1115 |
| VRy | **A1** del ADS1115 |
| +5V (VCC) | **3V3** |
| GND | GND |
| SW (botón del joystick) | **GPIO13** del ESP32 (con la otra pata a GND) |

> El joystick es **sólo color**. El centro/escala se **auto-calibran** al arrancar,
> así que no importa el rango exacto. Si una dirección queda invertida, se corrige por
> software en `config.h` (`JOY_INVERT_X/Y`, `JOY_SWAP_XY`) — no hace falta recablear.

### Los 11 botones (7 armónicos + 4 de función)
Todos son **pulsadores a masa**: una pata al GPIO, la otra pata a **GND**.
Usan pull-up interno (no necesitan resistencia externa).

| Botón | GPIO | Función |
|-------|------|---------|
| **I**   | **GPIO8**  | grado I |
| **II**  | **GPIO9**  | grado II |
| **III** | **GPIO17** | grado III |
| **IV**  | **GPIO45** | grado IV (botón a GND, sin pull-up externo) |
| **V**   | **GPIO5**  | grado V |
| **VI**  | **GPIO14** | grado VI |
| **VII** | **GPIO10** | grado VII |
| **NAV−**    | GPIO11 | tónica −1 / navegar menú |
| **NAV+**    | GPIO12 | tónica +1 / navegar menú |
| **HOLD**    | **GPIO18** | acorde sostenido |
| **VOICING** | GPIO7  | cicla inversiones / en menú: editar |

---

## 2. Todos los pines usados (checklist)

**GPIO de señal (17):**
`5, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18, 45, 38, 39, 40, 41, 42`

**Rieles:** `3V3` (alimenta OLED, ADS1115, joystick, PCM5102) y `GND` (común).

**Reserva libre:** `1, 2, 3, 4, 6, 15, 16`.
**No usar en esta placa:** `48` (LED RGB onboard).
**No usar nunca:** `33–37` (PSRAM del looper), `19/20` (USB), `43/44` (UART), `0/46` (strapping).

---

## 3. Sugerencia de ubicación en la protoboard 10×10 cm

```
 ┌──────────────────────────────────────────────────┐
 │  [ riel + 3V3 ]  ────────────────────────────────│  ← riel de alimentación arriba
 │  [ riel − GND ]  ────────────────────────────────│
 │                                                   │
 │   ┌─────────┐        ┌──────────────────────┐     │
 │   │  OLED   │        │  ESP32-S3 SuperMini   │     │
 │   │ 128x64  │        │   (USB hacia afuera)  │     │
 │   └─────────┘        └──────────────────────┘     │
 │                                                   │
 │   ┌─────────┐  ┌──────────┐   [I][II][III][IV]    │  ← 7 botones armónicos
 │   │ ADS1115 │  │ joystick │   [V][VI][VII]        │     en fila/arco
 │   └─────────┘  │  HW-504  │                       │
 │                └──────────┘   [NAV-][NAV+]         │  ← 4 de función
 │   ┌─────────┐                 [HOLD][VOICING]     │
 │   │ PCM5102 │──► jack 3.5mm                        │
 │   └─────────┘                                      │
 │  [ riel − GND ]  ────────────────────────────────│  ← 2º riel de GND abajo
 └──────────────────────────────────────────────────┘
```

Criterios:
- La **fila inferior de la ESP32 (38-42, 17, 18)** lleva el audio (I2S) y el I2C:
  orientá la placa para tener esos pads a mano.
- **ESP32 con el USB hacia el borde** (para flashear/cargar sin desarmar).
- **PCM5102 lejos de la parte digital** y con su cable de audio corto hacia el jack;
  poné el **10 µF + 100 nF** de desacople pegados a su VIN.
- **OLED + ADS1115 + joystick juntos** (comparten el bus I2C de GPIO41/GPIO42).
- **Botones en bloque**, con un riel de GND corriendo debajo de la fila para que cada
  pulsador tenga su masa cerca.
- Un **100 nF** de desacople en el VCC de cada módulo (5 en total) + el **10 µF** bulk
  en el PCM5102.

---

## 4. Alimentación (la batería va al final)

Durante el armado y las pruebas: **alimentá por el USB de la ESP32** y tomá `3V3` de
la placa para todos los módulos.

Cuando sumes la batería (LiPo 3.7 V):
```
LiPo → TP4056 (carga+protección) → interruptor → buck-boost 3.3 V → riel 3V3
```
- **Nunca** conectes la LiPo directo al riel (4.2 V llena dañaría los 3.3 V).
- Buck-boost de **≥ 1 A** (los picos de BLE llegan a ~300 mA).
- **No uses USB y batería a la vez** (dos fuentes de 3.3 V enfrentadas): con USB
  conectado, dejá el interruptor de la batería en OFF. Cargá la LiPo por el USB del TP4056.

---

## 5. Primer encendido (orden seguro)

1. Con **sólo la ESP32** por USB, flasheá el firmware (ver [`flash/README.md`](flash/README.md)).
2. Sumá **OLED + ADS1115** y verificá que el OLED muestre el splash y la pantalla normal.
   Si el OLED no prende: revisá SDA=41 / SCL=42 y los pull-ups.
3. Sumá el **joystick** y probá que el color cambie al moverlo.
4. Sumá el **PCM5102 + jack** y tocá un botón armónico: debe sonar el acorde.
   Si no hay audio: confirmá **SCK del DAC a GND** y **XSMT a 3V3**.
5. Sumá los botones y verificá uno por uno con el menú/OLED.
6. Recién al final, la cadena de batería.

---

## 6. Variante SIN OLED (prototipo 2, case impreso en 3D)

El firmware **no necesita la pantalla**: `oledInit()` sondea el bus por ACK en 0x3C y
0x3D, y si no hay panel deja `ok=false` — todas las funciones de dibujo salen por su
guarda y no se dibuja nada. Audio, botones, joystick, MIDI y menú siguen igual.

**Qué cambia respecto de las tablas de arriba: nada.** Mismo pinout exacto.
Simplemente **no se conecta** el OLED al bus I2C; el ADS1115 queda solo en 41/42.

| Detalle | Sin OLED |
|---------|----------|
| SDA/SCL | **41 / 42** (igual), sólo el ADS colgando |
| Pull-ups I2C | los suele traer el módulo ADS. **Si el ADS no los trae, agregá 4.7 kΩ a 3V3 en cada línea** — antes los podía estar poniendo el OLED |
| ADDR del ADS | a **VDD** (0x49) como el prototipo 1; ver nota abajo |
| GPIO liberados | ninguno (el OLED compartía 41/42) |

> **El firmware ahora autodetecta la dirección del ADS** (prueba 0x49, 0x48, 0x4A,
> 0x4B). Si en este módulo dejás ADDR a GND o al aire, el joystick **igual funciona**.
> Al arrancar, el log dice cuál encontró: `[input] ADS present=1 addr=0x49`.

### Lo único que se pierde
El **menú de configuración** (8 páginas) y el feedback visual son "a ciegas": los
botones NAV−/NAV+/VOICING siguen funcionando, pero no ves en qué fila estás. Para el
prototipo sin pantalla conviene manejar la config desde el **webtool por USB** y dejar
la sesión guardada en NVS (GUARDAR), que se restaura sola al encender.

### Verificación al primer encendido (monitor serie 115200)
```
[i2c] scan: 0x49  (1 dispositivos)      <- sin OLED: NO debe aparecer 0x3C
[input] ADS present=1 addr=0x49 centro x=... y=...
[oled] panel NO detectado                <- esperado y correcto
[boot] listo -> tocá un grado
```
Si aparece `ADS present=0`: revisá VDD/GND/SDA/SCL del ADS y los pull-ups.

### ⚠️ Pads a verificar en CADA placa
En la placa del prototipo 1, los pads **6, 14 y 17** dieron continuidad pero **no
llegaban al chip** como entrada. El mapa actual usa **17 (grado III)** y **14 (grado
VI)**. Al encender el prototipo 2, probá los 7 grados uno por uno: si III y/o VI no
responden y el resto sí, es este defecto — no el cableado. Remapeo posible sin tocar
nada más que `config.h` (línea del array `PIN_CHORD`), usando los libres: **1, 2, 3,
4, 15, 16**.
