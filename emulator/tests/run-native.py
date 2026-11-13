#!/usr/bin/env python3
import subprocess,concurrent.futures
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'emulator/tests/out';out.mkdir(exist_ok=True)
def run(name,src):
 exe=out/name
 subprocess.run(['g++','-O2','-std=c++17','-I.',f'test/{name}.cpp',src,'-o',str(exe)],cwd=root,check=True)
 result=subprocess.run([str(exe),str(out)],cwd=root,text=True,capture_output=True)
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 print(name, 'PASS' if result.returncode==0 else 'FAIL',flush=True)
 if result.returncode:print(result.stdout[-2000:]);raise SystemExit(1)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 list(pool.map(lambda x:run(*x),[('test_harmony','harmony.cpp'),('test_voicelead','harmony.cpp'),('test_evloop','evloop.cpp'),('test_dsp','dsp.cpp')]))
cmd=['g++','-O2','-std=c++17','-DARDUINO=10800','-Iemulator/hal','-Iemulator/vendor','-I.','emulator/tests/native_trace.cpp','emulator/bridge.cpp','emulator/display240.cpp','emulator/generated/synth_web.cpp','dsp.cpp','harmony.cpp','evloop.cpp','input.cpp','midi.cpp','oled.cpp','webcfg.cpp','emulator/vendor/Adafruit_GFX.cpp','-o',str(out/'native_trace')]
subprocess.run(cmd,cwd=root,check=True)
with (out/'native.f32').open('wb') as f:subprocess.run([str(out/'native_trace')],stdout=f,cwd=root,check=True)
