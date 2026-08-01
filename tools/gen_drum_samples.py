# Genera drum_samples.h a partir de los WAV de tools/samples/ (kick/snare/hihat).
# Requisitos de los WAV: PCM 16-bit MONO (el sample rate se guarda en el header;
# el motor reproduce con paso = rate/SAMPLE_RATE e interpolacion lineal).
# Procesado: normaliza el pico a 0.95, recorta la cola en silencio (<-44 dB) y
# aplica un fade-out corto para que el final no haga click.
# Uso:  python tools/gen_drum_samples.py   (desde la raiz del repo)
import wave, struct, os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = os.path.join(HERE, '..', 'drum_samples.h')
SRC  = [('KICK', 'kick.wav'), ('SNARE', 'snare.wav'), ('HAT', 'hihat.wav')]

def load(path):
    w = wave.open(path)
    assert w.getnchannels() == 1,  f"{path}: tiene que ser MONO"
    assert w.getsampwidth() == 2,  f"{path}: tiene que ser 16-bit"
    n, sr = w.getnframes(), w.getframerate()
    x = list(struct.unpack(f'<{n}h', w.readframes(n)))
    # normalizar el pico a 0.95
    peak = max(1, max(abs(v) for v in x))
    g = 0.95 * 32767.0 / peak
    x = [int(round(v * g)) for v in x]
    # recortar cola en silencio (umbral ~-44 dB), dejando 5 ms de margen
    TH = 200
    end = len(x)
    while end > 1 and abs(x[end - 1]) < TH: end -= 1
    end = min(len(x), end + sr // 200)
    x = x[:end]
    # fade-out de 3 ms (sin click al terminar)
    f = max(1, sr * 3 // 1000)
    for i in range(f):
        x[end - f + i] = int(x[end - f + i] * (1.0 - (i + 1) / f))
    return x, sr

rates, blobs = set(), []
for name, fn in SRC:
    x, sr = load(os.path.join(HERE, 'samples', fn))
    rates.add(sr)
    blobs.append((name, x, sr))
    print(f"{name:6s} {fn:11s} {sr} Hz  {len(x)} muestras = {len(x)/sr*1000:.0f} ms  ({len(x)*2} bytes)")
assert len(rates) == 1, f"todos los WAV deben tener el mismo sample rate (hay {rates})"
rate = rates.pop()

total = sum(len(x) for _, x, _ in blobs)
with open(OUT, 'w', encoding='utf-8') as f:
    f.write("/* ============================================================================\n")
    f.write("   drum_samples.h  -  Samples de bateria (GENERADO por tools/gen_drum_samples.py\n")
    f.write("   desde tools/samples/*.wav, NO editar a mano). PCM int16 mono; en el ESP32\n")
    f.write("   los const van a flash (~%d KB). Lo incluye SOLO dsp.cpp.\n" % (total * 2 // 1024))
    f.write("   ============================================================================ */\n")
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define DS_RATE {rate}u   // sample rate de los WAV de origen\n\n")
    for name, x, sr in blobs:
        f.write(f"static const uint32_t DS_{name}_LEN = {len(x)}u;\n")
        f.write(f"static const int16_t  DS_{name}[{len(x)}] = {{\n")
        for k in range(0, len(x), 20):
            f.write("  " + ",".join(str(v) for v in x[k:k+20]) + ",\n")
        f.write("};\n\n")
print(f"\nOK -> drum_samples.h  ({total} muestras, {total*2//1024} KB en flash)")
