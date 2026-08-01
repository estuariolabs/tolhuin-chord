# -*- coding: utf-8 -*-
# Analiza un WAV (PCM 16-bit) y reporta caracteristicas de timbre/feel SIN dependencias.
# Uso: python analyze.py archivo.wav
import sys, wave, struct, math, array

def load(path):
    w = wave.open(path, 'rb')
    ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n); w.close()
    a = array.array('h'); a.frombytes(raw)
    if ch > 1:                      # tomar canal 0 (mono mix de referencia)
        a = a[0::ch]
    x = [s / 32768.0 for s in a]
    return x, sr

def rms_env(x, win):
    env = []
    for i in range(0, len(x) - win, win):
        s = 0.0
        for j in range(i, i + win): s += x[j]*x[j]
        env.append((s / win) ** 0.5)
    return env

def goertzel_power(x, i0, i1, freq, sr):
    w = 2.0 * math.pi * freq / sr
    c = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for i in range(i0, i1):
        s0 = x[i] + c*s1 - s2
        s2 = s1; s1 = s0
    N = i1 - i0
    return (s1*s1 + s2*s2 - c*s1*s2) / (N*N)

def main():
    path = sys.argv[1]
    x, sr = load(path)
    dur = len(x)/sr
    peak = max(abs(v) for v in x) or 1e-9
    rms = (sum(v*v for v in x)/len(x)) ** 0.5
    print(f"== {path}")
    print(f"  dur={dur:.2f}s  sr={sr}  peak={peak:.3f}  rms={rms:.4f}")

    # --- envolvente y onsets ---
    win = 512
    env = rms_env(x, win)
    genv = max(env) or 1e-9
    onsets = []
    for i in range(3, len(env)):
        if env[i] > 0.06*genv and env[i] > 1.7*env[i-3] and (not onsets or i-onsets[-1] > 6):
            onsets.append(i)
    print(f"  onsets={len(onsets)}  (~{len(onsets)/dur:.1f}/s)")
    if len(onsets) >= 2:
        gaps = [(onsets[k+1]-onsets[k])*win/sr for k in range(len(onsets)-1)]
        gaps.sort()
        print(f"  separacion notas: mediana={gaps[len(gaps)//2]*1000:.0f}ms  min={gaps[0]*1000:.0f}ms")

    # --- banco log de frecuencias (centroide + bandas) sobre ventanas con energia ---
    freqs = [60*(2**(k/3.0)) for k in range(0,25)]   # 60 Hz .. ~15 kHz (1/3 de octava)
    freqs = [f for f in freqs if f < sr*0.45]
    AW = int(0.20*sr)                                  # ventana de analisis 200 ms
    bandP = [0.0]*len(freqs)
    npts = 0
    for oi in range(0, len(x)-AW, AW):
        # solo ventanas con energia (evita silencios)
        seg = x[oi:oi+AW]
        e = (sum(v*v for v in seg)/AW) ** 0.5
        if e < 0.06*peak: continue
        for fi, f in enumerate(freqs):
            bandP[fi] += goertzel_power(x, oi, oi+AW, f, sr)
        npts += 1
    if npts == 0: npts = 1
    tot = sum(bandP) or 1e-12
    centroid = sum(freqs[i]*bandP[i] for i in range(len(freqs))) / tot
    # bandas resumidas
    def bandsum(lo, hi): return sum(bandP[i] for i,f in enumerate(freqs) if lo<=f<hi)/tot*100
    print(f"  CENTROIDE espectral = {centroid:.0f} Hz   (brillo; mas alto = mas brillante)")
    print(f"  bandas %%: graves<250={bandsum(0,250):.0f}  medios250-1k={bandsum(250,1000):.0f}"
          f"  altos1k-4k={bandsum(1000,4000):.0f}  brillo>4k={bandsum(4000,99999):.0f}")

    # --- cola: decaimiento desde el ultimo onset fuerte ---
    if onsets:
        last = onsets[-1]
        tailpk = max(env[last:]) or 1e-9
        end = len(env)-1
        # tiempo hasta caer 20 dB (factor 0.1) del pico de la cola
        t20 = None
        for i in range(last, len(env)):
            if env[i] < 0.1*tailpk: t20 = (i-last)*win/sr; break
        print(f"  cola: caida -20dB en {('%.2fs'%t20) if t20 else '>fin'} (reverb/decay)")

main()
