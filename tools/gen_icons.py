# Genera icons.h: pixel-art 1-bit para el OLED (SSD1306, drawBitmap de Adafruit
# GFX: filas MSB-first, cada fila alineada a byte). El ARTE VIVE ACA como grillas
# ASCII ('#' = pixel encendido, '.' = apagado): editar y re-correr.
#   python tools/gen_icons.py     (desde la raiz del repo)
#
# Guia de estilo (v2): iconos SOLO para estados y paginas del menu; los timbres
# van en TEXTO (mas rapido de leer y no exige un dibujo por sonido nuevo).
# Trazo simple, masas grandes, nada menor a 2 px, mismo peso visual.
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, '..', 'icons.h')

# ---------------------------------------------------------------------------
# ESTADOS 10x10 (reemplazan las letras HGMASTCBD + R/L de la fila 0).
# Orden = enum IconState de icons.h (¡no cambiar sin cambiar oled.cpp!).
# ---------------------------------------------------------------------------
STATE = {
  'HOLD': [        # candado: el acorde queda sostenido
    "...####...",
    "..#....#..",
    "..#....#..",
    ".########.",
    ".########.",
    ".###..###.",
    ".###..###.",
    ".########.",
    ".########.",
    ".........."],
  'GLIDE': [       # rampa diagonal: portamento entre acordes
    "##........",
    "###.......",
    ".###......",
    "..###.....",
    "...###....",
    "....###...",
    ".....###..",
    "......###.",
    ".......###",
    "........##"],
  'MONO': [        # un "1": una sola voz
    "..........",
    "....##....",
    "...###....",
    "..####....",
    "....##....",
    "....##....",
    "....##....",
    "....##....",
    "..######..",
    ".........."],
  'ARP': [         # escalera ascendente: notas una por vez
    "........##",
    "........##",
    ".....#####",
    ".....##...",
    ".....##...",
    "..#####...",
    "..##......",
    "#####.....",
    "##........",
    "##........"],
  'SUSTAIN': [     # fermata: la nota resuena al soltar
    "..........",
    "...####...",
    "..#....#..",
    ".#......#.",
    "#........#",
    "..........",
    "....##....",
    "....##....",
    "..........",
    ".........."],
  'TREMOLO': [     # onda doble: amplitud ondulante
    "..........",
    ".##...##..",
    "#..#.#..#.",
    "....#....#",
    "..........",
    ".##...##..",
    "#..#.#..#.",
    "....#....#",
    "..........",
    ".........."],
  'CHORUS': [      # dos circulos superpuestos: voces dobladas
    "..........",
    ".###......",
    "#...#.....",
    "#..###....",
    "#..#..#...",
    ".###..#...",
    "...#..#...",
    "...####...",
    "..........",
    ".........."],
  'BODY': [        # cuerpo macizo con costillas: resonancia/ensamble
    "..........",
    "...####...",
    "..######..",
    ".########.",
    ".##.##.##.",
    ".########.",
    ".##.##.##.",
    "..######..",
    "...####...",
    ".........."],
  'DRUMS': [       # tambor frontal
    "..........",
    "..######..",
    ".#......#.",
    ".########.",
    ".#......#.",
    ".#......#.",
    ".#......#.",
    "..######..",
    "...#..#...",
    ".........."],
  'REC': [         # punto de grabacion (looper grabando)
    "..........",
    "...####...",
    "..######..",
    ".########.",
    ".########.",
    ".########.",
    ".########.",
    "..######..",
    "...####...",
    ".........."],
  'LOOP': [        # play: el loop esta sonando
    "..........",
    "..##......",
    "..####....",
    "..######..",
    "..#######.",
    "..######..",
    "..####....",
    "..##......",
    "..........",
    ".........."],
}

# ---------------------------------------------------------------------------
# PAGINAS DEL MENU 12x12 (junto al titulo). Orden = FX_PAGES del .ino:
# SONIDO ARMONIA JOYSTICK RITMO ARP LOOPER TOQUE AMBIENTE SYNC MEMORIA
# ---------------------------------------------------------------------------
PAGE = {
  'SONIDO': [      # corchea
    "............",
    ".....######.",
    ".....######.",
    ".....##...#.",
    ".....##.....",
    ".....##.....",
    ".....##.....",
    ".....##.....",
    "..######....",
    ".########...",
    "..######....",
    "............"],
  'ARMONIA': [     # dos notas con barra (intervalo)
    "............",
    "..##########",
    "..##########",
    "..##......##",
    "..##......##",
    "..##......##",
    "..##......##",
    "..##......##",
    "####....####",
    "####....####",
    "............",
    "............"],
  'JOYSTICK': [    # palanca con bola y base
    "............",
    ".....##.....",
    "....####....",
    "....####....",
    ".....##.....",
    ".....##.....",
    ".....##.....",
    "....####....",
    ".##########.",
    "############",
    "............",
    "............"],
  'RITMO': [       # tambor con patas
    "............",
    ".##########.",
    "#..........#",
    "############",
    "#..........#",
    "#..........#",
    "#..........#",
    "#..........#",
    ".##########.",
    "...#....#...",
    "..#......#..",
    "............"],
  'ARP': [         # escalera grande
    "..........##",
    "..........##",
    ".......#####",
    ".......##...",
    ".......##...",
    "....#####...",
    "....##......",
    "....##......",
    ".#####......",
    ".##.........",
    ".##.........",
    "............"],
  'LOOPER': [      # circulo con flecha (loop)
    "............",
    "...#####....",
    "..#.....#...",
    ".#.......#..",
    ".#.......#..",
    ".#.....#####",
    ".#......###.",
    ".#.......#..",
    "..#.....#...",
    "...#####....",
    "............",
    "............"],
  'CANAL': [       # 3 pistas con flecha selectora en la del medio
    "............",
    "....#######.",
    "....#######.",
    "............",
    "#...........",
    "##..#######.",
    "###.#######.",
    "##..#######.",
    "#...........",
    "............",
    "....#######.",
    "....#######."],
  'TOQUE': [       # mano (3 dedos + palma)
    "............",
    "...#.#.#....",
    "...#.#.#....",
    "...#.#.#....",
    "...#####....",
    "..######....",
    ".#######....",
    ".#######....",
    ".#######....",
    "..######....",
    "...####.....",
    "............"],
  'AMBIENTE': [    # taps de eco decrecientes (delay/reverb)
    "............",
    ".##.........",
    ".##.........",
    ".##..##.....",
    ".##..##.....",
    ".##..##..##.",
    ".##..##..##.",
    ".##..##..##.",
    ".##..##..##.",
    "............",
    "............",
    "............"],
  'SYNC': [        # reloj
    "............",
    "...######...",
    "..#......#..",
    ".#....#...#.",
    ".#....#...#.",
    "#.....#....#",
    "#.....####.#",
    "#..........#",
    ".#........#.",
    ".#........#.",
    "..#......#..",
    "...######..."],
  'MEMORIA': [     # diskette
    "............",
    ".#########..",
    ".#.......##.",
    ".#..####..#.",
    ".#..####..#.",
    ".#........#.",
    ".#..####..#.",
    ".#.#....#.#.",
    ".#.#....#.#.",
    ".#.#....#.#.",
    ".##########.",
    "............"],
}

def to_bytes(rows, w):
    """Grilla ASCII -> bytes drawBitmap (MSB first, filas alineadas a byte)."""
    bpr = (w + 7) // 8
    out = []
    for r in rows:
        assert len(r) == w, f"fila de {len(r)} px (esperaba {w}): {r!r}"
        for b in range(bpr):
            v = 0
            for bit in range(8):
                x = b * 8 + bit
                if x < w and r[x] == '#':
                    v |= 0x80 >> bit
            out.append(v)
    return out

def emit(f, name, icons, w, h):
    keys = list(icons.keys())
    for k in keys:
        assert len(icons[k]) == h, f"{k}: {len(icons[k])} filas (esperaba {h})"
        data = to_bytes(icons[k], w)
        f.write(f"static const uint8_t IC_{name}_{k}[] = {{ "
                + ", ".join(f"0x{v:02X}" for v in data) + " };\n")
    f.write(f"\n// Tabla indexada (orden = enum en este header).\n")
    f.write(f"static const uint8_t* const IC_{name}[] = {{\n  "
            + ", ".join(f"IC_{name}_{k}" for k in keys) + "\n};\n\n")
    return keys

with open(OUT, 'w', encoding='utf-8') as f:
    f.write("/* ============================================================================\n")
    f.write("   icons.h  -  Pixel-art del OLED (GENERADO por tools/gen_icons.py: el arte\n")
    f.write("   se edita ALLA como grillas ASCII, NO aca). Formato drawBitmap (MSB first).\n")
    f.write("   Estados 10x10 (fila 0) + paginas del menu 12x12 (junto al titulo).\n")
    f.write("   ============================================================================ */\n")
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write("#define IC_STATE_W 10\n#define IC_STATE_H 10\n")
    f.write("#define IC_PAGE_W  12\n#define IC_PAGE_H  12\n\n")
    sk = emit(f, "STATE", STATE, 10, 10)
    f.write("enum IconState { " + ", ".join(f"ICS_{k}" for k in sk) + " };\n\n")
    pk = emit(f, "PAGE", PAGE, 12, 12)
    f.write("// Orden = FX_PAGES del .ino (indice de pagina -> icono).\n")
    f.write("enum IconPage { " + ", ".join(f"ICP_{k}" for k in pk) + " };\n")

print(f"OK -> icons.h  ({len(STATE)} estados 10x10 + {len(PAGE)} paginas 12x12)")
