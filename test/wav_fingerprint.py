# -*- coding: utf-8 -*-
# Huella de timbre de un tramo [t0,t1] de un WAV: fundamental, perfil armonico
# (H1..H14 en dB rel H1), ratio impares/pares, tilt espectral y envolvente
# (attack/decay/sostenido). Traducible a settings de osc/filtro de dsp.cpp.
# Uso: python fingerprint.py archivo.wav t0 t1 [etiqueta]
import sys, wave, numpy as np

NOTES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
def note(f):
    if f<=0: return '--'
    m=round(69+12*np.log2(f/440.0)); return f"{NOTES[int(m)%12]}{int(m)//12-1}"

def load(path):
    w=wave.open(path,'rb'); ch,sw,sr,n=w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()
    raw=w.readframes(n); w.close()
    if sw==3:
        b=np.frombuffer(raw,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        v=b[:,0]|(b[:,1]<<8)|(b[:,2]<<16); v=np.where(v&0x800000,v-0x1000000,v); a=v.astype(np.float32)/8388608.0
    elif sw==2:
        a=np.frombuffer(raw,dtype=np.int16).astype(np.float32)/32768.0
    else:
        a=np.frombuffer(raw,dtype=np.int32).astype(np.float32)/2147483648.0
    if ch>1: a=a.reshape(-1,ch).mean(axis=1)
    return a,sr

def main():
    path=sys.argv[1]; t0=float(sys.argv[2]); t1=float(sys.argv[3])
    label=sys.argv[4] if len(sys.argv)>4 else ''
    x,sr=load(path)
    seg=x[int(t0*sr):int(t1*sr)]
    if len(seg)<2048: print("tramo muy corto"); return
    # --- fundamental por autocorrelacion (50..1000 Hz) ---
    s=seg-seg.mean(); w=np.hanning(len(s)); sw_=s*w
    ac=np.correlate(sw_,sw_,'full')[len(sw_)-1:]
    lo,hi=int(sr/1000),int(sr/50)
    f0=sr/(lo+np.argmax(ac[lo:hi]))
    # --- espectro promedio (Welch casero) ---
    N=8192; hop=N//2; acc=np.zeros(N//2+1); k=0
    wN=np.hanning(N)
    for i in range(0,len(seg)-N,hop):
        acc+=np.abs(np.fft.rfft(seg[i:i+N]*wN)); k+=1
    if k==0: acc=np.abs(np.fft.rfft(np.r_[seg,np.zeros(N-len(seg))][:N]*wN)); k=1
    S=acc/k; ff=np.fft.rfftfreq(N,1/sr)
    # --- amplitud de cada armonico (pico en +-3% alrededor de n*f0) ---
    H=[]
    for n in range(1,15):
        fc=n*f0
        if fc>sr*0.45: H.append(0.0); continue
        m=(ff>fc*0.97)&(ff<fc*1.03)
        H.append(S[m].max() if m.any() else 0.0)
    H=np.array(H); H1=H[0] if H[0]>0 else (H.max() or 1e-9)
    Hdb=20*np.log10(np.maximum(H,1e-9)/H1)
    odd=H[0::2].sum(); even=H[1::2].sum()
    # tilt sobre el espectro completo 120..8000
    mm=(ff>=120)&(ff<=8000)
    tilt=np.polyfit(np.log2(ff[mm]),20*np.log10(np.maximum(S[mm],1e-9)/S[mm].max()),1)[0]
    # centroide
    mb=(ff>=50)&(ff<sr*0.45); cen=(ff[mb]*S[mb]).sum()/S[mb].sum()
    # --- envolvente RMS (attack a pico, y nivel sostenido vs pico) ---
    win=int(0.01*sr); env=np.sqrt(np.convolve(seg*seg,np.ones(win)/win,'same'))
    pk=env.max(); ipk=int(np.argmax(env))
    a10=np.argmax(env>0.1*pk)
    atk=max(0,(ipk-a10))/sr*1000
    sus=env[int(len(env)*0.6):int(len(env)*0.9)].mean()/pk    # nivel sostenido rel pico
    print(f"== {label or path}  [{t0:.2f}-{t1:.2f}s]")
    print(f"  f0={f0:.1f}Hz ({note(f0)})  centroide={cen:.0f}Hz  tilt={tilt:.1f}dB/oct")
    print(f"  attack~{atk:.0f}ms  sostenido/pico={sus:.2f}  (1=plano/pad, <0.5=percusivo)")
    print(f"  impares/pares={odd/ (even or 1e-9):.2f}  (alto=cuadrada/clarinete; ~1=saw/full)")
    print("  armonicos (dB rel H1):")
    line='   '
    for n in range(14):
        line+=f"H{n+1}={Hdb[n]:5.1f} "
        if n%5==4: line+='\n   '
    print(line)

main()
