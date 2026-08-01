# -*- coding: utf-8 -*-
# Mapa tiempo->contenido de un WAV largo (unboxing con voz + timbres).
# Separa habla de notas de instrumento por planitud espectral (tonalidad)
# y estabilidad de pitch. Reporta una linea de tiempo gruesa y los segmentos
# tonales sostenidos (candidatos a "timbre del HiChord").
import sys, wave, numpy as np

NOTES = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def hz_to_note(f):
    if f <= 0: return '--'
    m = round(69 + 12*np.log2(f/440.0))
    return f"{NOTES[int(m)%12]}{int(m)//12-1}"

def load(path):
    w = wave.open(path,'rb')
    ch,sw,sr,n = w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()
    raw = w.readframes(n); w.close()
    if sw==2:
        a = np.frombuffer(raw, dtype=np.int16).astype(np.float32)/32768.0
    elif sw==3:                                   # 24-bit LE -> int32 con extension de signo
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v = b[:,0] | (b[:,1]<<8) | (b[:,2]<<16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        a = v.astype(np.float32)/8388608.0
    else:
        a = np.frombuffer(raw, dtype=np.int32).astype(np.float32)/2147483648.0
    if ch>1: a = a.reshape(-1,ch).mean(axis=1)
    return a, sr

def main():
    path = sys.argv[1]
    x, sr = load(path)
    dur = len(x)/sr
    print(f"== {path}  dur={dur:.1f}s  sr={sr}  peak={np.abs(x).max():.3f}")

    win = int(0.093*sr); win -= win%2          # ~93 ms
    hop = int(0.046*sr)                          # ~46 ms
    w = np.hanning(win).astype(np.float32)
    nfr = 1 + (len(x)-win)//hop
    rms = np.zeros(nfr); cen = np.zeros(nfr); flat = np.zeros(nfr); pit = np.zeros(nfr)
    freqs = np.fft.rfftfreq(win, 1/sr)
    fb = (freqs>=50)&(freqs<=sr*0.45)
    for i in range(nfr):
        s = x[i*hop:i*hop+win]
        rms[i] = np.sqrt(np.mean(s*s))
        S = np.abs(np.fft.rfft(s*w))+1e-12
        P = S[fb]; ff = freqs[fb]
        psum = P.sum()
        cen[i] = (ff*P).sum()/psum
        gm = np.exp(np.mean(np.log(P))); am = np.mean(P)
        flat[i] = gm/am                          # 0=tonal, 1=ruido/plano
        pit[i] = ff[np.argmax(P)]                # frecuencia dominante (grosera)

    peak = rms.max()
    # --- linea de tiempo gruesa (bins de 2s) ---
    print("\n-- timeline (bins 2s): t  rms  centroid  flat(0=tonal)  pitchDom --")
    binf = int(2.0*sr/hop)
    for b in range(0, nfr, binf):
        sl = slice(b, min(b+binf, nfr))
        r = rms[sl]
        if r.mean() < 0.01*peak: continue
        m = r > 0.3*r.max()                      # ventanas activas del bin
        if m.sum()==0: continue
        t0 = b*hop/sr
        print(f"  {t0:6.1f}s  rms={r.mean():.3f}  cen={cen[sl][m].mean():5.0f}Hz"
              f"  flat={flat[sl][m].mean():.3f}  pit~{np.median(pit[sl][m]):5.0f}Hz {hz_to_note(np.median(pit[sl][m]))}")

    # --- segmentos tonales sostenidos: flat bajo + pitch estable + rms alto ---
    active = rms > 0.10*peak
    tonal  = flat < 0.16
    cand = active & tonal
    segs = []
    i = 0
    while i < nfr:
        if cand[i]:
            j = i
            while j+1<nfr and cand[j+1]: j += 1
            if (j-i+1)*hop/sr >= 0.35:           # dur minima 350 ms
                segs.append((i,j))
            i = j+1
        else:
            i += 1
    print(f"\n-- segmentos tonales sostenidos (candidatos a timbre): {len(segs)} --")
    print("   t_ini   dur   nota(med)  cen   flat   tilt(dB/oct)  attack")
    for (i,j) in segs:
        sl = slice(i,j+1)
        t0 = i*hop/sr; d=(j-i+1)*hop/sr
        p = np.median(pit[sl]); c = cen[sl].mean(); fl = flat[sl].mean()
        # tilt espectral: pendiente log-log promedio sobre el segmento
        s = x[i*hop:(j+1)*hop+win]
        S = np.abs(np.fft.rfft((s[:len(s)//2*2])*np.hanning(len(s[:len(s)//2*2]))))+1e-12
        ff = np.fft.rfftfreq(len(s[:len(s)//2*2]),1/sr)
        m = (ff>=120)&(ff<=8000)
        tilt = np.polyfit(np.log2(ff[m]), 20*np.log10(S[m]/S[m].max()), 1)[0]
        # attack: tiempo del rms del segmento de 10% a 90% del pico local
        rseg = rms[sl]; pk=rseg.max()
        a10 = np.argmax(rseg>0.1*pk); a90 = np.argmax(rseg>0.9*pk)
        atk = max(0,(a90-a10))*hop*1000
        print(f"  {t0:6.1f}s {d:5.2f}s  {hz_to_note(p):>5}{'':2}{c:6.0f} {fl:.3f}"
              f"   {tilt:6.1f}      {atk:5.0f}ms")

main()
