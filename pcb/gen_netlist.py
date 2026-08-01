#!/usr/bin/env python3
# ============================================================================
#  gen_netlist.py  -  Genera la netlist del TOLHUIN a partir de UNA fuente de
#  verdad (el diccionario de abajo). Salidas en pcb/out/:
#     tolhuin.net     -> netlist KiCad (importable en pcbnew)
#     netlist.csv     -> conexiones nodo a nodo (neutral, cualquier herramienta)
#     bom.csv         -> lista de materiales
#     connections.txt -> reporte legible para imprimir
#
#  Flujo automatizado:  editar pinout -> `python gen_netlist.py` -> importar el
#  .net en KiCad -> autorutear con Freerouting.  Sin dibujar nada a mano.
#
#  NOTA de footprints: por defecto se usan PIN HEADERS genéricos (existen en la
#  librería estándar de KiCad, así la importación funciona ya). Antes de fabricar,
#  reemplazá el footprint del ESP32 y los módulos por el real (p.ej. DevKitC-1).
# ============================================================================
import csv, os

# ---- Pines del ESP32-S3 (deben coincidir con config.h del firmware) ----
GPIO = {
    "I2S_BCK": 38, "I2S_WS": 39, "I2S_DIN": 40,        # PCM5102A
    "I2C_SDA": 41, "I2C_SCL": 42,                       # OLED + ADS1115
    "BTN_I": 15, "BTN_II": 16, "BTN_III": 17, "BTN_IV": 18,
    "BTN_V": 21, "BTN_VI": 47, "BTN_VII": 48,           # 7 grados
    "NAV_MINUS": 11, "NAV_PLUS": 12, "HOLD": 14, "VOICING": 7,
    "JOY_SW": 13,
}

# ---- Componentes: ref -> (value, lista ORDENADA de pines) ----
# El número de pad = posición en la lista (1..N). El footprint por defecto es un
# header 1xN; cambialo por el módulo real cuando lo tengas.
COMPONENTS = {
    "U1": ("ESP32-S3-DevKitC-1", [
        "GPIO38","GPIO39","GPIO40","GPIO41","GPIO42",
        "GPIO15","GPIO16","GPIO17","GPIO18","GPIO21","GPIO47","GPIO48",
        "GPIO11","GPIO12","GPIO14","GPIO7","GPIO13",
        "3V3","GND",
    ]),
    "U2": ("PCM5102A", ["VIN","GND","BCK","DIN","LCK","SCK","FMT","XSMT","FLT","DEMP","LOUT","ROUT"]),
    "U3": ("ADS1115",  ["VDD","GND","SCL","SDA","ADDR","A0","A1"]),
    "U4": ("SSD1306-OLED", ["GND","VCC","SCL","SDA"]),
    "U5": ("Joystick-HW504", ["GND","VCC","VRX","VRY","SW"]),
    "J1": ("AudioJack-3.5mm", ["TIP","RING","SLEEVE"]),   # salida de audio estéreo
    # Botones: 2 pines (1=señal al GPIO, 2=GND). Reemplazar por tact switch real.
    "SW1": ("SW_PUSH", ["A","B"]), "SW2": ("SW_PUSH", ["A","B"]),
    "SW3": ("SW_PUSH", ["A","B"]), "SW4": ("SW_PUSH", ["A","B"]),
    "SW5": ("SW_PUSH", ["A","B"]), "SW6": ("SW_PUSH", ["A","B"]),
    "SW7": ("SW_PUSH", ["A","B"]), "SW8": ("SW_PUSH", ["A","B"]),
    "SW9": ("SW_PUSH", ["A","B"]), "SW10": ("SW_PUSH", ["A","B"]),
    "SW11": ("SW_PUSH", ["A","B"]),
    # Desacoples y pull-ups I2C
    "C1": ("100nF", ["1","2"]), "C2": ("100nF", ["1","2"]),
    "C3": ("100nF", ["1","2"]), "C4": ("100nF", ["1","2"]),
    "C5": ("100nF", ["1","2"]), "C6": ("10uF",  ["1","2"]),
    "R1": ("4k7", ["1","2"]), "R2": ("4k7", ["1","2"]),
    # ---- Alimentación por batería LiPo 3.7 V ----
    # LiPo -> TP4056 (carga+protección) -> interruptor -> buck-boost 3.3V -> riel
    "BT1":  ("LiPo-3V7",        ["+","-"]),
    "U6":   ("TP4056-charger",  ["BAT+","BAT-","OUT+","OUT-"]),
    "U7":   ("BuckBoost-3V3",   ["VIN+","VIN-","VOUT+","VOUT-"]),
    "SW12": ("PWR-SWITCH",      ["1","2"]),
    "C7":   ("100uF",           ["1","2"]),   # bulk a la salida del conversor
}

# Footprints específicos (los demás usan header genérico / SMD por defecto)
FP_OVERRIDE = {
    "BT1": "Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical",
}

# ---- Footprints por defecto (resolubles en la librería estándar de KiCad) ----
def footprint(ref):
    val, pins = COMPONENTS[ref]
    if ref in FP_OVERRIDE: return FP_OVERRIDE[ref]
    if ref.startswith("C"): return "Capacitor_SMD:C_0805_2012Metric"
    if ref.startswith("R"): return "Resistor_SMD:R_0805_2012Metric"
    n = len(pins)
    return f"Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical"

# ---- Botón i: pin A al grado, pin B a GND ----
BTN = {  # ref -> nombre de la señal de GPIO
    "SW1": "BTN_I", "SW2": "BTN_II", "SW3": "BTN_III", "SW4": "BTN_IV",
    "SW5": "BTN_V", "SW6": "BTN_VI", "SW7": "BTN_VII",
    "SW8": "NAV_MINUS", "SW9": "NAV_PLUS", "SW10": "HOLD", "SW11": "VOICING",
}

# ---- NETS: nombre -> lista de (ref, nombre_de_pin) ----
def build_nets():
    nets = {}
    def add(net, ref, pin): nets.setdefault(net, []).append((ref, pin))

    # 3V3: lo entrega el buck-boost (U7.VOUT+) a todos los módulos + caps/pullups
    for n in ["U7:VOUT+","U1:3V3","U2:VIN","U2:XSMT","U3:VDD","U4:VCC","U5:VCC",
              "C1:1","C2:1","C3:1","C4:1","C5:1","C6:1","C7:1","R1:1","R2:1"]:
        r,p = n.split(":"); add("3V3", r, p)
    # GND común (incluye SCK/FLT/DEMP/FMT del DAC, ADDR del ADS y la rama de poder)
    for n in ["U1:GND","U2:GND","U2:SCK","U2:FMT","U2:FLT","U2:DEMP",
              "U3:GND","U3:ADDR","U4:GND","U5:GND","J1:SLEEVE",
              "U6:OUT-","U7:VIN-","U7:VOUT-",
              "C1:2","C2:2","C3:2","C4:2","C5:2","C6:2","C7:2"]:
        r,p = n.split(":"); add("GND", r, p)
    for sw in BTN: add("GND", sw, "B")          # otro lado de cada botón

    # Cadena de alimentación por batería
    add("BAT_P","BT1","+");    add("BAT_P","U6","BAT+")     # LiPo + -> cargador
    add("BAT_N","BT1","-");    add("BAT_N","U6","BAT-")     # LiPo - (protegido por TP4056)
    add("VSYS","U6","OUT+");   add("VSYS","SW12","1")       # salida protegida -> interruptor
    add("VSYS_SW","SW12","2"); add("VSYS_SW","U7","VIN+")   # interruptor -> conversor

    # I2S
    add("I2S_BCK","U1","GPIO38"); add("I2S_BCK","U2","BCK")
    add("I2S_WS","U1","GPIO39");  add("I2S_WS","U2","LCK")
    add("I2S_DIN","U1","GPIO40"); add("I2S_DIN","U2","DIN")
    # I2C (compartido OLED + ADS) + pull-ups
    add("I2C_SDA","U1","GPIO41"); add("I2C_SDA","U3","SDA"); add("I2C_SDA","U4","SDA"); add("I2C_SDA","R1","2")
    add("I2C_SCL","U1","GPIO42"); add("I2C_SCL","U3","SCL"); add("I2C_SCL","U4","SCL"); add("I2C_SCL","R2","2")
    # Salida de audio: PCM5102 LOUT/ROUT -> jack 3.5mm (DC-coupled, no necesita caps)
    add("AUDIO_L","U2","LOUT"); add("AUDIO_L","J1","TIP")
    add("AUDIO_R","U2","ROUT"); add("AUDIO_R","J1","RING")
    # Joystick
    add("JOY_VRX","U3","A0"); add("JOY_VRX","U5","VRX")
    add("JOY_VRY","U3","A1"); add("JOY_VRY","U5","VRY")
    add("JOY_SW","U1","GPIO13"); add("JOY_SW","U5","SW")
    # Botones -> GPIO
    for sw, sig in BTN.items():
        g = GPIO[sig]
        add(f"{sig}_GPIO{g}", "U1", f"GPIO{g}")
        add(f"{sig}_GPIO{g}", sw, "A")
    return nets

# ---- Resolver nombre de pin -> número de pad (posición en la lista) ----
def pad_of(ref, pin):
    pins = COMPONENTS[ref][1]
    return pins.index(pin) + 1

# ============================================================================
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "out"); os.makedirs(out, exist_ok=True)
    nets = build_nets()

    # ---------- KiCad netlist (.net) ----------
    L = ['(export (version "E")', '  (components']
    for ref,(val,_) in COMPONENTS.items():
        L.append(f'    (comp (ref "{ref}") (value "{val}") (footprint "{footprint(ref)}"))')
    L.append("  )")
    L.append("  (nets")
    for i,(net,nodes) in enumerate(nets.items(), 1):
        L.append(f'    (net (code "{i}") (name "/{net}")')
        for ref,pin in nodes:
            L.append(f'      (node (ref "{ref}") (pin "{pad_of(ref,pin)}"))')
        L.append("    )")
    L.append("  )\n)")
    with open(os.path.join(out,"tolhuin.net"),"w",encoding="utf-8") as f:
        f.write("\n".join(L))

    # ---------- netlist.csv ----------
    with open(os.path.join(out,"netlist.csv"),"w",newline="",encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(["net","ref","pin","pad"])
        for net,nodes in nets.items():
            for ref,pin in nodes: w.writerow([net,ref,pin,pad_of(ref,pin)])

    # ---------- bom.csv (agrupado por value) ----------
    groups = {}
    for ref,(val,_) in COMPONENTS.items():
        groups.setdefault(val, []).append(ref)
    with open(os.path.join(out,"bom.csv"),"w",newline="",encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(["value","qty","refs","footprint"])
        for val,refs in sorted(groups.items()):
            w.writerow([val,len(refs)," ".join(sorted(refs)),footprint(refs[0])])

    # ---------- connections.txt (legible) ----------
    lines = ["TOLHUIN - lista de conexiones (generada automaticamente)",
             "="*60,""]
    for net,nodes in nets.items():
        members = ", ".join(f"{r}.{p}" for r,p in nodes)
        lines.append(f"{net:18s}: {members}")
    with open(os.path.join(out,"connections.txt"),"w",encoding="utf-8") as f:
        f.write("\n".join(lines))

    print(f"OK -> {out}")
    print(f"  componentes: {len(COMPONENTS)}   nets: {len(nets)}")
    for net,nodes in nets.items():
        if len(nodes) < 2:
            print(f"  AVISO: net '{net}' con un solo nodo (revisar)")

if __name__ == "__main__":
    main()
