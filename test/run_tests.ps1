# ============================================================================
#  run_tests.ps1  -  Compila y corre los tests de host (sin placa).
#  Requiere un compilador C++ de host: g++ (MinGW) o clang++ (LLVM).
#  Instalar clang si falta:  winget install -e --id LLVM.LLVM
#  Uso:  powershell -ExecutionPolicy Bypass -File test\run_tests.ps1
#  Devuelve 0 si todo pasa; distinto de 0 si falla (para el bucle del agente).
# ============================================================================
$ErrorActionPreference = "Stop"
$testDir = $PSScriptRoot
$root    = Split-Path -Parent $testDir      # carpeta del proyecto (padre de test\)

# Agregar compiladores conocidos al PATH de esta sesión si no están ya
foreach ($binDir in @(
    "C:\Program Files\LLVM\bin",
    "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
)) {
    if ((Test-Path $binDir) -and ($env:PATH -notlike "*$([System.IO.Path]::GetFileName($binDir))*")) {
        $env:PATH += ";$binDir"
    }
}

# Elegir compilador disponible (g++ primero, luego clang++)
$cxx = $null
foreach ($c in @("g++", "clang++")) {
  if (Get-Command $c -ErrorAction SilentlyContinue) { $cxx = $c; break }
}
if (-not $cxx) {
  Write-Host "ERROR: no hay compilador C++ de host (g++ o clang++)."
  Write-Host "Instalalo: winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT"
  exit 2
}
Write-Host "Compilador: $cxx"

$fail = 0

# --- test de armonía ---
$srcH = Join-Path $testDir "test_harmony.cpp"
$outH = Join-Path $testDir "test_harmony.exe"
if (Test-Path $srcH) {
  Write-Host "`n[test_harmony]"
  & $cxx -std=c++17 -I "$root" "$srcH" (Join-Path $root "harmony.cpp") -o "$outH"
  if ($LASTEXITCODE -ne 0) { Write-Host "ERROR compilando test_harmony"; exit 1 }
  & "$outH"
  if ($LASTEXITCODE -ne 0) { $fail = 1 }
}

# --- test de DSP (creado en T0.3; se corre si existe) ---
$srcD = Join-Path $testDir "test_dsp.cpp"
$outD = Join-Path $testDir "test_dsp.exe"
if (Test-Path $srcD) {
  Write-Host "`n[test_dsp]"
  & $cxx -std=c++17 -I "$root" "$srcD" (Join-Path $root "dsp.cpp") -o "$outD"
  if ($LASTEXITCODE -ne 0) { Write-Host "ERROR compilando test_dsp"; exit 1 }
  & "$outD" "$testDir"          # escribe out_*.wav en test\
  if ($LASTEXITCODE -ne 0) { $fail = 1 }
}

# --- test del looper de eventos ---
$srcE = Join-Path $testDir "test_evloop.cpp"
$outE = Join-Path $testDir "test_evloop.exe"
if (Test-Path $srcE) {
  Write-Host "`n[test_evloop]"
  & $cxx -std=c++17 -I "$root" "$srcE" (Join-Path $root "evloop.cpp") -o "$outE"
  if ($LASTEXITCODE -ne 0) { Write-Host "ERROR compilando test_evloop"; exit 1 }
  & "$outE"
  if ($LASTEXITCODE -ne 0) { $fail = 1 }
}

if ($fail -eq 0) { Write-Host "`nTODOS LOS TESTS EN VERDE" } else { Write-Host "`nHAY TESTS EN ROJO" }
exit $fail
