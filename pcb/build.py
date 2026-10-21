#!/usr/bin/env python3
# ============================================================================
#  build.py  -  Orquestador del pipeline PCB del TOLHUIN. Un comando encadena:
#     1) netlist desde el pinout      (python del sistema)
#     2) colocación + contorno        (python de KiCad -> pcb_tasks.py place)
#     3) export Specctra DSN          (python de KiCad -> pcb_tasks.py dsn)
#     4) ruteo                        (Freerouting CLI)
#     5) aplicar ruteo (SES)          (python de KiCad -> pcb_tasks.py ses)
#     6) tests + fab                  (kicad-cli: DRC + Gerbers + Drill)
#
#  Uso:   python build.py
#  Ajustá las rutas de CONFIG a tu instalación. Cada etapa se saltea con un
#  aviso claro si falta su herramienta -> nunca rompe, te dice qué falta.
# ============================================================================
import os, sys, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, "out")

# ---------------- CONFIG (ajustar a tu PC) ----------------
KICAD_PY        = r"C:\Program Files\KiCad\8.0\bin\python.exe"     # trae pcbnew
KICAD_CLI       = r"C:\Program Files\KiCad\8.0\bin\kicad-cli.exe"
FREEROUTING_JAR = r"C:\tools\freerouting.jar"
# ----------------------------------------------------------
BOARD = os.path.join(OUT, "tolhuin.kicad_pcb")
DSN   = os.path.join(OUT, "tolhuin.dsn")
SES   = os.path.join(OUT, "tolhuin.ses")

def have(p): return bool(p) and os.path.exists(p)
def stage(n, t): print(f"\n=== [{n}] {t} ===")
def run(cmd):
    print("  $", " ".join(cmd))
    return subprocess.run(cmd).returncode

def tasks(sub):
    return run([KICAD_PY, os.path.join(HERE, "pcb_tasks.py"), sub])

def main():
    os.makedirs(OUT, exist_ok=True)

    stage(1, "Netlist desde el pinout")
    run([sys.executable, os.path.join(HERE, "gen_netlist.py")])

    stage(2, "Colocación + contorno (pcbnew)")
    if not have(BOARD):
        print("  Falta la placa:", BOARD)
        print("  -> 1 sola vez: en pcbnew, File > Import > Netlist (out/tolhuin.net)")
        print("     y guardá como out/tolhuin.kicad_pcb. Después este paso es automático.")
    elif have(KICAD_PY):
        tasks("place")
    else:
        print("  KICAD_PY no existe:", KICAD_PY)

    stage(3, "Export Specctra DSN")
    if have(KICAD_PY) and have(BOARD):
        tasks("dsn")

    stage(4, "Ruteo (Freerouting CLI)")
    if have(FREEROUTING_JAR) and have(DSN):
        run(["java", "-jar", FREEROUTING_JAR, "-de", DSN, "-do", SES])
    else:
        print("  Salteado. Revisá FREEROUTING_JAR y que exista out/tolhuin.dsn.")
        print("  Comando: java -jar <freerouting.jar> -de out/tolhuin.dsn -do out/tolhuin.ses")

    stage(5, "Aplicar ruteo (SES) a la placa")
    if have(KICAD_PY) and have(SES):
        tasks("ses")

    stage(6, "Tests + Fab (kicad-cli: DRC + Gerbers + Drill)")
    if have(KICAD_CLI) and have(BOARD):
        gdir = os.path.join(OUT, "gerbers"); os.makedirs(gdir, exist_ok=True)
        run([KICAD_CLI, "pcb", "drc", "--exit-code-violations",
             "--output", os.path.join(OUT, "drc.rpt"), BOARD])
        run([KICAD_CLI, "pcb", "export", "gerbers", "--output", gdir, BOARD])
        run([KICAD_CLI, "pcb", "export", "drill",   "--output", gdir, BOARD])
    else:
        print("  kicad-cli no existe:", KICAD_CLI)

    print("\nListo. Revisá  out/drc.rpt  y  out/gerbers/.")

if __name__ == "__main__":
    main()
