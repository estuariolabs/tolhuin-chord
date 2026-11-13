#!/usr/bin/env python3
"""Compila los fuentes originales. Sólo adapta el bucle de tarea I2S al callback web."""
import hashlib,json,os,shutil,subprocess
from pathlib import Path
root=Path(__file__).resolve().parent.parent
os.chdir(root)
out=root/'dist';out.mkdir(exist_ok=True)
gen=root/'emulator/generated';gen.mkdir(exist_ok=True)
synth=(root/'synth.cpp').read_text()
needle='static void audioTask(void*) {\n  for (;;) {'
assert synth.count(needle)==1, 'Cambió audioTask: revisar el adaptador antes de portar'
(gen/'synth_web.cpp').write_text(synth.replace(needle,'static void audioTask(void*) {\n  {',1)+'\nfloat* emuAudioLeft(){return bufL;}\nfloat* emuAudioRight(){return bufR;}\n')
# El .ino se incluye directamente: C++ estándar, sin reescribir el comportamiento.
files=['dsp.cpp','harmony.cpp','evloop.cpp','input.cpp','midi.cpp','oled.cpp','webcfg.cpp','tolhuin_chord_v2.ino','synth.cpp','state.h','config.h']
manifest={'upstream':'https://github.com/estuariolabs/tolhuin-chord','commit':subprocess.check_output(['git','log','-1','--format=%H','--','tolhuin_chord_v2.ino'],text=True).strip(),'sources':{f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},'adaptation':'audioTask: una iteración por callback; HAL virtual de GPIO/I2C/I2S/USB/BLE/NVS','sampleRate':44100,'blockSize':256}
(out/'provenance.json').write_text(json.dumps(manifest,indent=2)+'\n')
emcc=os.environ.get('EMXX') or shutil.which('em++') or '/workspace/emsdk/upstream/emscripten/em++'
cmd=[emcc,'-std=c++17','-O2','-DARDUINO=10800','-Iemulator/hal','-Iemulator/vendor','-I.','emulator/bridge.cpp','emulator/display240.cpp','emulator/generated/synth_web.cpp','dsp.cpp','harmony.cpp','evloop.cpp','input.cpp','midi.cpp','oled.cpp','webcfg.cpp','emulator/vendor/Adafruit_GFX.cpp','-sMODULARIZE=1','-sEXPORT_ES6=1','-sEXPORT_NAME=createTolhuin','-sENVIRONMENT=web,worker,shell,node','-sALLOW_MEMORY_GROWTH=0','-sINITIAL_MEMORY=67108864','-sFILESYSTEM=0','-sEXPORTED_RUNTIME_METHODS=["cwrap","UTF8ToString","HEAPF32","HEAPU8","HEAPU16"]','-o',str(out/'tolhuin.js')]
subprocess.run(cmd,check=True)
for f in (root/'emulator/web').iterdir():
 if f.is_file():shutil.copy2(f,out/f.name)
shutil.copy2(root/'LICENSE',out/'LICENSE.txt')
shutil.copy2(root/'docs/evaluacion-pantalla.md',out/'evaluacion-pantalla.md')
# Fuentes completos redistribuibles junto al WASM (GPL).
subprocess.run(['tar','czf',str(out/'source.tar.gz'),'--exclude=.git','--exclude=dist','--exclude=emulator/generated','--exclude=emulator/tests/out','--exclude=node_modules','--exclude=.openai','--exclude=*.wav','--exclude=.gitignore','--exclude=emulator/__pycache__','.'],check=True)
print('Port construido en',out)
