#!/usr/bin/env python3
"""Copia el motor (armonía, DSP, secuenciador y sus pruebas) al repo público
tolhuin-chord-engine. El firmware es la fuente: el engine nunca se edita a mano.

Uso:
    python3 tools/sincronizar_engine.py ../tolhuin-chord-engine            # copia
    python3 tools/sincronizar_engine.py ../tolhuin-chord-engine --comprobar # sólo compara

Agrega la cabecera GPL del engine a cada archivo. Sale con 1 si --comprobar
encuentra diferencias.
"""
import argparse
import os
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARCHIVOS = [
    'config.h', 'state.h', 'harmony.h', 'harmony.cpp', 'dsp.h', 'dsp.cpp',
    'evloop.h', 'evloop.cpp', 'drum_samples.h',
    'test/test_harmony.cpp', 'test/test_voicelead.cpp', 'test/test_dsp.cpp',
    'test/test_evloop.cpp', 'test/run_tests.sh',
]
CABECERA = ('// SPDX-License-Identifier: GPL-3.0-or-later\n'
            '// TOLHUIN Chord — Engine.  Copyright (C) 2026 Mauro Restivo.\n'
            '// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.\n\n')


def contenido_engine(ruta):
    with open(os.path.join(RAIZ, ruta), encoding='utf-8') as f:
        texto = f.read()
    # drum_samples.h es generado por tools/gen_drum_samples.py: se copia tal cual.
    if ruta.endswith('.sh') or ruta == 'drum_samples.h' or texto.startswith('// SPDX-License-Identifier'):
        return texto
    return CABECERA + texto


def main(argv=None):
    p = argparse.ArgumentParser(description='Sincroniza el motor con tolhuin-chord-engine')
    p.add_argument('engine', help='ruta al checkout de tolhuin-chord-engine')
    p.add_argument('--comprobar', action='store_true', help='no escribe; sale con 1 si hay diferencias')
    args = p.parse_args(argv)
    diferentes = []
    for ruta in ARCHIVOS:
        nuevo = contenido_engine(ruta)
        destino = os.path.join(args.engine, ruta)
        actual = open(destino, encoding='utf-8').read() if os.path.exists(destino) else None
        if actual == nuevo:
            continue
        diferentes.append(ruta)
        if not args.comprobar:
            os.makedirs(os.path.dirname(destino) or '.', exist_ok=True)
            with open(destino, 'w', encoding='utf-8', newline='') as f:
                f.write(nuevo)
    accion = 'difieren' if args.comprobar else 'actualizados'
    print(f'{len(diferentes)} archivo(s) {accion}' + (': ' + ', '.join(diferentes) if diferentes else ''))
    return 1 if args.comprobar and diferentes else 0


if __name__ == '__main__':
    sys.exit(main())
