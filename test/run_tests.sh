#!/bin/sh
# Tests de host en macOS y Linux. Devuelve distinto de 0 si algo falla.
set -eu
raiz=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
salida=$(mktemp -d)
trap 'rm -rf "$salida"' EXIT
cxx=${CXX:-c++}
for prueba in harmony:harmony voicelead:harmony dsp:dsp evloop:evloop; do
  nombre=${prueba%%:*}
  fuente=${prueba##*:}
  echo "[test_$nombre]"
  "$cxx" -std=c++17 -O2 ${CXXFLAGS:-} -I "$raiz" "$raiz/test/test_$nombre.cpp" "$raiz/$fuente.cpp" -o "$salida/test_$nombre"
  (cd "$salida" && "./test_$nombre" | tail -1)
done
