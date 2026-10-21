# PCB del TOLHUIN — flujo automatizado (código → KiCad → autorute)

La placa se diseña **desde código**: el pinout vive en `gen_netlist.py` (una sola
fuente de verdad, alineada con `config.h` del firmware). De ahí salen la netlist
de KiCad, el CSV de conexiones y el BOM. No se dibuja el esquemático a mano.

```
pcb/
  gen_netlist.py     <- fuente de verdad (pinout) + generador
  out/
    tolhuin.net      <- netlist KiCad (se importa en pcbnew)
    netlist.csv      <- conexiones nodo a nodo (cualquier herramienta)
    bom.csv          <- lista de materiales
    connections.txt  <- reporte legible para imprimir
```

## Regenerar la netlist (tras cambiar un pin)
```powershell
python pcb\gen_netlist.py
```

## Pipeline automático completo (código → placa ruteada → Gerbers + DRC)
Un comando encadena todas las etapas:
```powershell
python pcb\build.py
```
Etapas (cada una se saltea con aviso si falta su herramienta):
1. **netlist** desde el pinout (python del sistema).
2. **colocación + contorno** — `pcb_tasks.py place` (python de KiCad): ubica cada
   footprint por bloques y dibuja el borde en `Edge.Cuts`.
3. **export DSN** — `pcb_tasks.py dsn` (Specctra, para el router).
4. **ruteo** — **Freerouting en CLI**: `java -jar freerouting.jar -de … -do …`.
5. **aplicar SES** — `pcb_tasks.py ses` (mete las pistas ruteadas en la placa).
6. **tests + fab** — `kicad-cli`: corre **DRC** (`out/drc.rpt`) y exporta
   **Gerbers + Drill** (`out/gerbers/`).

### Config (una vez) — editá las rutas arriba de `build.py`
```python
KICAD_PY        = r"C:\Program Files\KiCad\8.0\bin\python.exe"
KICAD_CLI       = r"C:\Program Files\KiCad\8.0\bin\kicad-cli.exe"
FREEROUTING_JAR = r"C:\tools\freerouting.jar"
```

### Los dos pasos que todavía NO son headless
- **Crear la placa la 1.ª vez**: `out/tolhuin.kicad_pcb` se genera importando
  `out/tolhuin.net` en pcbnew (`File > Import > Netlist`). Después, `build.py`
  re-coloca/rutea/fabrica solo.
- **DSN/SES**: el ida-y-vuelta con Freerouting se intenta por script, pero según
  tu versión de KiCad puede caer a 2 clics de GUI (`File > Export Specctra DSN` /
  `File > Import Specctra Session`). El script te avisa si pasa.

> Estado: scaffold **sin probar en KiCad** todavía. Cuando instales KiCad 8 +
> Java + Freerouting lo corremos y ajustamos lo que haga falta (sobre todo 2/3/5).

## Qué hace falta instalar (una vez)
- **KiCad 8** (gratis): https://www.kicad.org/download/  → trae `pcbnew` y todas
  las librerías de footprints estándar que usa la netlist.
- **Java 17+** y **Freerouting** (autorouter, gratis):
  https://github.com/freerouting/freerouting/releases  (es un `.jar`).

## Paso a paso
1. **Generar** la netlist: `python pcb\gen_netlist.py`.
2. **KiCad → nuevo proyecto** vacío. Abrí **pcbnew** (editor de PCB).
3. **File → Import → Netlist…** → elegí `pcb/out/tolhuin.net` → *Update PCB*.
   Aparecen los 24 componentes con sus pads y el ratsnest (líneas de conexión).
   Los footprints por defecto son **pin headers** estándar (resolubles): la
   importación funciona sin instalar nada extra.
4. **Colocar** los componentes (drag) en una distribución cómoda. Tip: agrupá por
   bloque (DAC, I2C, joystick, fila de botones). Definí el borde de placa en la
   capa *Edge.Cuts*.
5. **Autorutear**:
   - `File → Export → Specctra DSN` → `tolhuin.dsn`.
   - `java -jar freerouting.jar` → abrí el `.dsn` → *Autoroute* → al terminar
     *File → Export Specctra Session* → `tolhuin.ses`.
   - En pcbnew: `File → Import → Specctra Session` → `tolhuin.ses`. Pistas listas.
6. **Fabricar**: `File → Plot` (Gerbers) + `File → Fabrication Outputs → Drill`.

## IMPORTANTE antes de fabricar — footprints reales
Los footprints por defecto son headers genéricos 1xN (para que el import y el
autorute funcionen ya). Para una placa física correcta, reemplazá:

| Ref | Módulo | Footprint real sugerido |
|-----|--------|-------------------------|
| U1  | ESP32-S3-DevKitC-1 | footprint del DevKitC-1 (lib de comunidad / espressif) |
| U2  | PCM5102A (board púrpura) | header doble que matchee el módulo |
| U3  | ADS1115 (Adafruit/genérico) | `Adafruit ADS1115` o header del módulo |
| U4  | OLED 0.96" I2C | header 1x4 del módulo (suele venir bien) |
| U5  | Joystick HW-504 | header 1x5 del módulo |
| J1  | Jack audio 3.5mm | `Connector_Audio:Jack_3.5mm_*` (mapear TIP/RING/SLEEVE) |
| SW1–11 | tact switch | `Button_Switch_THT:SW_PUSH_6mm` (reasigná pads A/B) |

> Al cambiar un footprint, mantené el mapeo pad↔señal que figura en
> `connections.txt`. Para los headers, **el número de pad = la posición del pin
> en la lista** dentro de `gen_netlist.py`.

## Alimentación por batería LiPo 3.7 V
Cadena: `LiPo → TP4056 (carga+protección) → interruptor → buck-boost 3.3V → riel 3V3`.

- **BT1** LiPo plana (recomendado ≥ 500 mAh; 1000 mAh ≈ 6 h). Conector JST-PH.
- **U6** TP4056 con protección: carga la batería por su **propio micro-USB**.
- **SW12** interruptor general (corta el consumo).
- **U7** buck-boost a 3.3 V: usa toda la curva 4.2→3.0 V de la LiPo. Elegí uno de
  **≥ 1 A** (picos de BLE ~300 mA). Su salida alimenta TODO el riel 3V3.
- **C7** 100 µF bulk a la salida del conversor.

> La LiPo **NO** se conecta directo al riel: 4.2 V (llena) dañaría los 3.3 V.
>
> Como el firmware usa **USB-MIDI**, el modelo de uso es: **con USB → SW12 en OFF
> (corre del USB); sin USB → SW12 en ON (batería, BLE-MIDI).** No uses ambas a la
> vez para no enfrentar dos fuentes de 3.3 V. Cargá por el USB del TP4056.

## Reglas eléctricas ya contempladas
- Riel único **3.3 V** y **GND común** (incluye SCK/FLT/DEMP/FMT del DAC a GND).
- **5 × 100 nF** de desacople (uno por módulo) + **10 µF** bulk cerca del PCM5102A.
- **2 × 4.7 kΩ** de pull-up en SDA/SCL.
- Pines reservados evitados: GPIO33–37 (PSRAM OPI), 0/3/45/46, 19/20, 43/44.
