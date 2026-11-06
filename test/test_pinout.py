#!/usr/bin/env python3
"""Verifica que la netlist publicada coincida con config.h y que no haya choques."""
import csv
import importlib.util
import os
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("gen_netlist", os.path.join(RAIZ, "pcb", "gen_netlist.py"))
netlist = importlib.util.module_from_spec(spec)
spec.loader.exec_module(netlist)

fallas = []
pines = list(netlist.GPIO.values())
if len(pines) != len(set(pines)):
    fallas.append(f"GPIO repetido en config.h: {sorted(p for p in set(pines) if pines.count(p) > 1)}")

prohibidos = {0, 3, 46, 19, 20, 43, 44}  # strapping, USB y UART de arranque
for nombre, pin in netlist.GPIO.items():
    if pin in prohibidos:
        fallas.append(f"{nombre} usa GPIO{pin}, reservado")

with open(os.path.join(RAIZ, "pcb", "out", "netlist.csv"), encoding="utf-8") as f:
    filas = list(csv.DictReader(f))

for nombre, pin in netlist.GPIO.items():
    conectados = [fila for fila in filas if fila["ref"] == "U1" and fila["pin"] == f"GPIO{pin}"]
    if not conectados:
        fallas.append(f"{nombre}: GPIO{pin} no está en out/netlist.csv (regenerar con gen_netlist.py)")

addr = [fila["net"] for fila in filas if fila["ref"] == "U3" and fila["pin"] == "ADDR"]
if addr != [netlist.ADS_ADDR_NET]:
    fallas.append(f"ADS1115 ADDR en {addr}, config.h pide {netlist.ADS_ADDR_NET}")

if fallas:
    print("\n".join(fallas))
    sys.exit(1)
print(f"pinout OK: {len(pines)} señales coinciden con config.h")
