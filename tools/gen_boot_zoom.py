# Genera los 9 frames del zoom Argentina -> Tierra del Fuego (OLED 128x64).
# Poligonos en (lon, lat) aproximados; proyeccion X=lon*0.75, Y=-lat.
# Viewport 2:1 (proyectado) que interpola: bbox Argentina -> bbox isla TDF.
# Por frame: fill escanline -> contorno (solo vs vecinos EN pantalla, para no
# dibujar el borde del display) -> rombo de Tolhuin en los frames finales.
import math

W, H = 128, 64
NF = 9                      # 9 frames a 333 ms = 3 s (3 zooms por segundo)

# --- Silueta Argentina continental (sentido horario desde el NO) ---
mainland = [
    (-66.6, -21.8), (-64.3, -22.2), (-62.8, -22.0), (-60.5, -23.9),
    (-58.5, -24.8), (-57.5, -25.5), (-56.0, -25.5), (-54.6, -25.6),
    (-53.7, -26.5),                                   # Misiones (punta NE)
    (-55.6, -28.2), (-57.6, -30.2), (-58.2, -31.8), (-58.4, -33.5),
    (-58.4, -34.6),                                   # Rio de la Plata
    (-57.0, -35.4), (-56.7, -36.3),                   # Punta Rasa
    (-57.5, -38.1),                                   # Mar del Plata
    (-59.8, -38.9), (-61.8, -38.9), (-62.3, -39.8),   # Bahia Blanca
    (-64.9, -40.7), (-65.1, -41.0),                   # golfo San Matias
    (-64.0, -42.2), (-63.6, -42.6),                   # peninsula Valdes
    (-65.0, -43.4), (-65.3, -45.0), (-67.4, -45.9),   # Comodoro
    (-67.3, -46.6), (-65.9, -47.6),                   # Pto Deseado
    (-67.6, -49.0), (-68.3, -50.1), (-69.0, -51.6),   # Rio Gallegos
    (-68.4, -52.3),                                   # Punta Dungeness (estrecho)
    (-72.0, -52.2),                                   # limite S (estrecho)
    (-72.3, -51.7), (-73.1, -50.0), (-72.6, -48.4),   # frontera andina
    (-71.6, -46.7), (-71.8, -45.5), (-71.3, -43.8),
    (-71.9, -42.5), (-71.7, -40.8), (-71.0, -39.0),
    (-70.4, -36.5), (-69.8, -34.5), (-70.1, -33.0),
    (-70.3, -31.5), (-69.5, -29.5), (-68.6, -27.5),
    (-67.0, -24.5),
]
# --- Isla de Tierra del Fuego (silueta completa) ---
fuego = [
    (-74.0, -53.3), (-73.2, -53.0), (-71.5, -52.8), (-70.0, -52.65),
    (-68.6, -52.65),                                  # cabo Espiritu Santo
    (-68.2, -53.2), (-67.4, -53.9), (-66.5, -54.1),
    (-65.2, -54.6),                                   # peninsula Mitre
    (-66.3, -55.05), (-67.5, -54.9), (-68.6, -54.85), # Ushuaia
    (-70.5, -54.95), (-72.0, -54.6), (-73.5, -54.0),
]
# --- Malvinas (dos islas chicas) ---
malv_w = [(-61.3, -51.4), (-60.0, -51.25), (-59.9, -51.9), (-61.0, -52.0)]
malv_e = [(-58.8, -51.3), (-57.8, -51.4), (-58.0, -52.0), (-59.0, -51.9)]
POLYS = [mainland, fuego, malv_w, malv_e]

TOLHUIN = (-67.2, -54.5)

def proj(p):
    return (p[0] * 0.75, -p[1])          # X comprime lon, Y = -lat (norte arriba)

PP = [[proj(p) for p in poly] for poly in POLYS]
PT = proj(TOLHUIN)

# Viewport inicial: bbox de todo (con margen), aspecto 2:1 proyectado.
allp = [p for poly in PP for p in poly]
x0, x1 = min(p[0] for p in allp), max(p[0] for p in allp)
y0, y1 = min(p[1] for p in allp), max(p[1] for p in allp)
h0 = (y1 - y0) * 1.06
c0 = ((x0 + x1) / 2, (y0 + y1) / 2)
# Viewport final: bbox de la isla (con margen).
fx = [p[0] for p in PP[1]]; fy = [p[1] for p in PP[1]]
h8 = (max(fy) - min(fy)) * 1.35
c8 = ((min(fx) + max(fx)) / 2, (min(fy) + max(fy)) / 2)

def viewport(i):
    t = i / (NF - 1)
    h = h0 * (h8 / h0) ** t              # zoom exponencial (factor cte por paso)
    s = (h0 - h) / (h0 - h8)             # paneo atado al progreso del zoom
    cx = c0[0] + (c8[0] - c0[0]) * s
    cy = c0[1] + (c8[1] - c0[1]) * s
    return cx, cy, h                     # ancho proyectado = 2*h (pantalla 2:1)

def raster(i):
    cx, cy, h = viewport(i)
    w = 2 * h
    def to_px(p):
        return ((p[0] - (cx - w / 2)) / w * W, (p[1] - (cy - h / 2)) / h * H)
    filled = [[0] * W for _ in range(H)]
    for poly in PP:
        pts = [to_px(p) for p in poly]
        ys = [p[1] for p in pts]
        yA = max(0, int(math.floor(min(ys)))); yB = min(H - 1, int(math.ceil(max(ys))))
        for y in range(yA, yB + 1):
            yc = y + 0.5
            xs = []
            n = len(pts)
            for k in range(n):
                xa, ya = pts[k]; xb, yb = pts[(k + 1) % n]
                if (ya <= yc < yb) or (yb <= yc < ya):
                    xs.append(xa + (xb - xa) * (yc - ya) / (yb - ya))
            xs.sort()
            for k in range(0, len(xs) - 1, 2):
                a = max(0, int(round(xs[k]))); b = min(W - 1, int(round(xs[k + 1])))
                for x in range(a, b + 1):
                    filled[y][x] = 1
    # contorno: lleno con un vecino EN PANTALLA vacio (no marca el borde del display)
    out = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if not filled[y][x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H and not filled[ny][nx]:
                    out[y][x] = 1
                    break
    # rombo de Tolhuin en los frames finales (cuando la isla ya se distingue)
    tx, ty = to_px(PT)
    tx, ty = int(round(tx)), int(round(ty))
    if i >= 6 and 3 <= tx < W - 3 and 3 <= ty < H - 3:
        for dy in range(-3, 4):
            r = 3 - abs(dy)
            for dx in range(-r, r + 1):
                out[ty + dy][tx + dx] = 1
    return out, (tx, ty)

frames = []
for i in range(NF):
    g, tpx = raster(i)
    frames.append(g)
    if i == NF - 1:
        print(f"[frame {i}] Tolhuin px = {tpx}")

def preview(i):
    print(f"--- frame {i} ---")
    for y in range(H):
        line = ''.join('#' if frames[i][y][x] else ' ' for x in range(W)).rstrip()
        print(line)

for i in (0, 4, 6, 8):
    preview(i)

# --- emitir C array ---
with open("zoom_array.txt", "w") as f:
    f.write(f"static const uint8_t ZOOM[{NF}][{W * H // 8}] PROGMEM = {{\n")
    for i in range(NF):
        out = []
        for y in range(H):
            for bx in range(W // 8):
                b = 0
                for bit in range(8):
                    if frames[i][y][bx * 8 + bit]:
                        b |= 1 << (7 - bit)
                out.append(b)
        f.write("  { // frame %d\n" % i)
        for k in range(0, len(out), 20):
            f.write("    " + ",".join(str(v) for v in out[k:k+20]) + ",\n")
        f.write("  },\n")
    f.write("};\n")
print("\nOK -> zoom_array.txt")
