# -*- coding: utf-8 -*-
# Firma espectral comparativa de varios WAV (o tramos). Sirve para igualar el
# brillo/filtro/oscilador de nuestros timbres a los del HiChord.
# Uso: python compare.py archivo.wav[:t0:t1] [otro.wav[:t0:t1] ...]
import sys, wave, numpy as np

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

BANDS=[(0,125),(125,250),(250,500),(500,1000),(1000,2000),(2000,4000),(4000,8000),(8000,22050)]
def sig(spec, label):
    a,sr=spec
    N=8192; wN=np.hanning(N); acc=np.zeros(N//2+1); k=0
    for i in range(0,max(1,len(a)-N),N//2):
        acc+=np.abs(np.fft.rfft(a[i:i+N]*wN)); k+=1
    S=acc/max(k,1); ff=np.fft.rfftfreq(N,1/sr)
    mb=(ff>=50)&(ff<sr*0.45)
    cen=(ff[mb]*S[mb]).sum()/S[mb].sum()
    mm=(ff>=120)&(ff<=8000)
    tilt=np.polyfit(np.log2(ff[mm]),20*np.log10(np.maximum(S[mm],1e-9)/S[mm].max()),1)[0]
    tot=S[mb].sum() or 1e-9
    pct=[100*S[(ff>=lo)&(ff<hi)].sum()/tot for lo,hi in BANDS]
    bar=' '.join(f"{p:4.0f}" for p in pct)
    print(f"{label:28} cen={cen:5.0f}Hz tilt={tilt:5.1f} | {bar}")

def parse(arg):
    parts=arg.split('|')
    path=parts[0]; a,sr=load(path)
    if len(parts)>=3:
        t0,t1=float(parts[1]),float(parts[2]); a=a[int(t0*sr):int(t1*sr)]
    else:                                  # saltar ataque/cola: usar 30-80% central
        a=a[int(len(a)*0.3):int(len(a)*0.8)]
    return (a,sr)

print(f"{'archivo':28} {'':14} | bandas %% por octava: <125 .250 .500 1k 2k 4k 8k >8k")
for arg in sys.argv[1:]:
    label=arg.split('/')[-1].split('\\')[-1]
    sig(parse(arg), label)
