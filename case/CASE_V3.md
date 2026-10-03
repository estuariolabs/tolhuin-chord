# Tolhuin Chord — Carcasa v3 (ESP32-S3 DevKit completa)

Medidas: **146 × 88 mm**. Con la inclinación de 7° la caja mide **35 mm de alto adelante y 46 mm atrás**, sin contar las teclas. El archivo fuente es `tolhuin_case_v3.scad` (OpenSCAD) y todas las medidas están arriba de todo, con nombre.

## Qué cambió y por qué
| Problema del prototipo anterior | Solución en v3 |
|---|---|
| Los botones quedaban sueltos y había que pegarlos con hot-glue | Se usan **switches mecánicos MX** que se **encastran a presión** en el panel (calan en una placa de 1,5 mm). No llevan PCB: se sueldan los cables directo |
| La caja quedaba chica | La caja se dimensionó para la DevKit completa (63 × 27 mm con antena) y tiene **chequeos automáticos**: si algo choca en altura, OpenSCAD no compila y avisa qué es |
| El OLED y el joystick se pegaban | Van en **rieles con ranura** que cuelgan del panel. La placa entra deslizando y la frena un resalte |
| La electrónica quedaba suelta | La **perfboard de 5 × 7 cm** se atornilla con M2 y la DevKit va enchufada en headers hembra, así se puede sacar. El TP4056 y el buck-boost entran en guías y la batería va en una bahía |
| Los tornillos se veían arriba | Se usan 4 tornillos **M3 × 12 desde abajo**, que no se ven |

## Antes de imprimir: medir con calibre (marcadas `MEDIR` en el .scad)
Ya confirmadas: DevKit 63 × 27 mm, joystick 33 × 26 mm y 13 mm de alto sin capuchón.
Como el eje del joystick es corto, el panel se adelgaza a 1,2 mm alrededor del agujero.

1. **DevKit**: dónde están los USB-C (la ranura trasera está centrada en la placa).
2. **Joystick**: `joy_stick_dy`, cuánto está corrido el eje respecto del centro de la placa, y `joy_shaft_d`, el diámetro de la punta del eje.
3. **OLED**: `oled_glass_th` (espesor del vidrio) y `oled_win_off`.
4. **Llave de encendido y jack**: el diámetro de la rosca (está puesto en 6,2 mm).
5. **Batería**: `bat` (el valor actual corresponde a una 803040, de 30 × 40 × 8 mm).

## Orden de impresión
Los STL no se suben al repo (se generan desde el `.scad`). Para exportarlos, abrí `tolhuin_case_v3.scad` en OpenSCAD, cambiá `part` (`top`, `bottom`, `caps_chord` o `caps_T`/`caps_S`/`caps_D`, `caps_fn`, `cap_joy`, `test_keys`) y exportá con F6 + "Export STL". También se puede hacer desde la consola:
`openscad -D 'part="top"' -o tolhuin_v3_top.stl tolhuin_case_v3.scad`

1. Primero imprimí **`tolhuin_v3_test_keys.stl`**, un recorte con 2 agujeros de switch (unos 20 minutos). Encastrá un switch y probá: tiene que entrar con un clic y no bailar. Si queda flojo o muy duro, ajustá `sw_cut` de a 0,1 mm.
2. **`tolhuin_v3_top.stl`** (panel): ya viene con la cara de arriba contra la cama. No necesita soportes.
3. **`tolhuin_v3_bottom.stl`** (base).
4. **Teclas**: `caps_chord` en gris y `caps_fn` en naranja. También podés comprar keycaps MX en blanco, que andan seguro. `cap_joy` es el capuchón del joystick.

Recomendación: PETG o PLA, con 3 perímetros y 20 % de relleno.

## Inclinación (7°)
La caja está inclinada 7° hacia quien toca, así la pantalla y las teclas quedan mejor orientadas. La inclinación sale de una **cuña maciza debajo del piso**, que se imprime junto con la base. Por dentro no cambia nada: las piezas, las alturas y los chequeos de choque son los mismos.
- `tilt = 7` es el valor por defecto. Con `tilt = 0` vuelve a ser una caja plana; también se pueden probar 5° o 10°.
- `tolhuin_v3_bottom.stl` ya viene apoyado sobre la cara inclinada, listo para imprimir sin soportes. La cuña es maciza: con un relleno del 15 al 20 % no gasta mucho filamento.
- Las patitas de goma y el grabado de la URL quedan sobre la cara inclinada. Los alojamientos de los tornillos atraviesan la cuña, así que se siguen usando M3 × 12. Para los de atrás hace falta un destornillador largo, porque la cabeza queda a unos 13 mm de profundidad.

## Colores y acabado
Cada parte se imprime por separado, así que se pueden combinar colores sin pintar nada. Para elegir la combinación, cambiá `theme` en el `.scad` y mirá el render (F5).

| Paleta | Base | Panel | Acordes | Función |
|---|---|---|---|---|
| `bosque` | verde bosque | hueso | verdes (3 tonos) | naranja óxido |
| `glaciar` | blanco | blanco | celestes (3 tonos) | azul petróleo |
| `noche` | grafito | negro | grises (3 tonos) | ámbar |
| `lenga` | PLA madera | crema | arenas (3 tonos) | rojo lenga |
| `mar` | petróleo | crema | arenas (3 tonos) | coral |
| `clasico` | blanco | blanco | gris | naranja (la idea original) |

- **Colores por función armónica** (`chord_color = "funcion"`): las teclas de acordes se agrupan en tres tonos, según la función de cada grado.
  - **T** (tónica): I, iii y vi.
  - **S** (subdominante): ii y IV.
  - **D** (dominante): V y vii°.

  La agrupación vale tanto en mayor como en menor, y ayuda a encontrar los acordes de memoria. Para imprimirlas así, exportá `caps_T`, `caps_S` y `caps_D`, cada una en su color. Si preferís un solo color, usá `chord_color = "uniforme"` y exportá `caps_chord`.
- **Leyendas de otro color**: las leyendas van en relieve. Si en el slicer agregás un cambio de filamento (M600, o "pausa" en Cura y PrusaSlicer) a **7,5 mm** de altura, las letras salen en otro color.
- **Grabados del panel** (`deco = true`): un marco alrededor del OLED, un aro alrededor del joystick y una línea entre la fila de función y los acordes. Si los repasás con un marcador de pintura, quedan bien marcados.

## Compras nuevas
- 11 switches MX (Gateron, Outemu o similares, todos sirven).
- 1 perfboard de 5 × 7 cm.
- 2 tiras de headers hembra de 2,54 mm.
- 4 tornillos M3 × 12 y 4 tornillos M2 × 5.
- 4 patitas de goma de 10 mm.
- Opcional: 11 keycaps MX.

## Armado
1. Encastrá los 11 switches desde arriba del panel. Soldá un cable de GND que pase por todos (en cadena) y un cable por switch al GPIO que le corresponde.
2. Deslizá el OLED en sus rieles, entrando por la izquierda. Conviene soldarle los cables directo y no usar el header.
3. Deslizá el joystick en sus rieles, entrando por el frente, con el header hacia atrás.
4. En la base: atornillá la perfboard (con la DevKit, el PCM5102 y el ADS1115), colocá el TP4056, el buck-boost y la batería, y montá el jack y la llave en la pared derecha.
5. Cerrá con los 4 tornillos M3 desde abajo.
6. Conviene poner un conector entre el panel y la base (JST o dupont), así se puede abrir la caja sin desoldar nada.

## Firmware
Con la DevKitC-1 completa están disponibles todos los pines originales. Usá **`TOLHUIN_HW_REV 1`** (HOLD en GPIO18), que es la configuración por defecto, y no hace falta cambiar nada.
- Tiene que ser una **ESP32-S3**. Una "ESP32 DevKit" clásica no sirve, porque no tiene USB-MIDI ni PSRAM.
- Verificá en qué GPIO tiene el LED RGB tu placa. En algunas DevKitC-1 v1.1 está en el **GPIO38**, que es el BCK del I2S. Si es tu caso, el LED va a parpadear mientras suena, pero no afecta el audio.
