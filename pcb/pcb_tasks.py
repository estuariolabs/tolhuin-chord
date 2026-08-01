#!/usr/bin/env python3
# ============================================================================
#  pcb_tasks.py  -  Tareas de PCB que necesitan pcbnew. Se corre con el PYTHON
#  DE KICAD (no el del sistema), porque ese trae el módulo `pcbnew`:
#     & "C:\Program Files\KiCad\8.0\bin\python.exe" pcb_tasks.py place
#
#  Subcomandos:
#     place : coloca cada footprint por bloques + dibuja el contorno (Edge.Cuts)
#     dsn   : exporta Specctra .dsn para Freerouting
#     ses   : importa el .ses ruteado por Freerouting y guarda la placa
#
#  Requiere que la placa ya exista (out/tolhuin.kicad_pcb): se crea una vez
#  importando out/tolhuin.net en pcbnew. El orquestador (build.py) llama a esto.
#
#  NOTA: probado en KiCad 8 (API VECTOR2I). Las funciones Export/ImportSpecctra
#  pueden variar de firma según versión; van con try/except y aviso de GUI.
# ============================================================================
import sys, os
try:
    import pcbnew
except ImportError:
    print("ERROR: corré este script con el python de KiCad (el que trae pcbnew).")
    sys.exit(2)

HERE  = os.path.dirname(os.path.abspath(__file__))
OUT   = os.path.join(HERE, "out")
BOARD = os.path.join(OUT, "tolhuin.kicad_pcb")

def V(x, y):                      # mm -> VECTOR2I (nanómetros)
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))

# ---- Reglas de colocación (mm) por referencia: x, y, rotación ----
def position(ref):
    if ref.startswith("SW") and ref[2:].isdigit():
        n = int(ref[2:])
        if n <= 7:  return (12 + (n - 1) * 18, 14, 0)    # grados I..VII (fila 1)
        if n <= 11: return (12 + (n - 8) * 18, 34, 0)    # NAV-/NAV+/HOLD/VOICING
        return (60, 110, 0)                              # SW12 (interruptor)
    fixed = {
        "U1": (78, 72, 0),                               # ESP32-S3
        "U2": (22, 72, 0),                               # PCM5102A
        "J1": (22, 50, 0),                               # jack de audio 3.5mm
        "U3": (122, 58, 0), "U4": (122, 78, 0),          # ADS1115, OLED
        "R1": (104, 52, 0), "R2": (110, 52, 0),          # pull-ups I2C
        "U5": (122, 98, 0),                              # joystick
        "BT1": (22, 104, 0), "U6": (42, 104, 0),         # LiPo, cargador
        "U7": (80, 104, 0), "C7": (98, 104, 0),          # buck-boost, bulk
    }
    if ref in fixed:
        return fixed[ref]
    if ref.startswith("C"):                              # desacoples: columna der.
        i = int(ref[1:]);  return (150, 10 + i * 8, 0)
    return (160, 10, 0)                                  # cualquier otro: a un lado

def do_place():
    b = pcbnew.LoadBoard(BOARD)
    for fp in b.GetFootprints():
        x, y, r = position(fp.GetReference())
        fp.SetPosition(V(x, y))
        fp.SetOrientationDegrees(r)
    draw_outline(b)
    pcbnew.SaveBoard(BOARD, b)
    print("OK place ->", BOARD)

def draw_outline(b, margin=8.0):
    xs, ys = [], []
    for fp in b.GetFootprints():
        p = fp.GetPosition()
        xs.append(pcbnew.ToMM(p.x)); ys.append(pcbnew.ToMM(p.y))
    x0, y0 = min(xs) - margin, min(ys) - margin
    x1, y1 = max(xs) + margin, max(ys) + margin
    for d in list(b.GetDrawings()):                      # borra contorno previo
        if d.GetLayer() == pcbnew.Edge_Cuts:
            b.Remove(d)
    edges = [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)),
             ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]
    for a, c in edges:
        seg = pcbnew.PCB_SHAPE(b)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(V(*a)); seg.SetEnd(V(*c))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(pcbnew.FromMM(0.15))
        b.Add(seg)

def do_dsn():
    dsn = os.path.join(OUT, "tolhuin.dsn")
    b = pcbnew.LoadBoard(BOARD)
    try:
        try:    pcbnew.ExportSpecctraDSN(b, dsn)
        except TypeError: pcbnew.ExportSpecctraDSN(dsn)
        print("OK dsn ->", dsn)
    except Exception as e:
        print("No pude exportar DSN por script:", e)
        print("  GUI: File -> Export -> Specctra DSN -> out/tolhuin.dsn")

def do_ses():
    ses = os.path.join(OUT, "tolhuin.ses")
    b = pcbnew.LoadBoard(BOARD)
    try:
        try:    pcbnew.ImportSpecctraSES(b, ses)
        except TypeError: pcbnew.ImportSpecctraSES(ses)
        pcbnew.SaveBoard(BOARD, b)
        print("OK ses aplicada ->", BOARD)
    except Exception as e:
        print("No pude importar SES por script:", e)
        print("  GUI: File -> Import -> Specctra Session -> out/tolhuin.ses")

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "place"
    {"place": do_place, "dsn": do_dsn, "ses": do_ses}.get(cmd, do_place)()
