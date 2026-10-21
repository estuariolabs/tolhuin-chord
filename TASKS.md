# TASKS — Perfeccionar el motor de audio (acercarlo al HiChord)

Objetivo: mejorar la calidad del motor de audio de TOLHUIN y sumar las
funciones **emulables** del HiChord, sin romper las reglas de armonía ni la
arquitectura. Leer `CLAUDE.md` primero.

Referencia HiChord (lo emulable en este hardware): formas de onda múltiples +
FM, ADSR, filtro pasa-bajos + LFO, efectos (trémolo/delay/reverb), modo STRUM
(rueda las notas del acorde con retardo), arpegiador al BPM, drone, octava,
voice leading. (Samples, looper, drums, secuenciador: fuera de alcance acá.)

---

## Cómo trabajar (agente)
- Tareas EN ORDEN, de a una. **Una tarea = un commit de git** (mensaje claro).
- Después de CADA tarea: (1) compilar firmware `arduino-cli compile` (SOLO
  compile, **no** subir a la placa), (2) correr `test/run_tests.ps1`. Las dos
  tienen que pasar antes de commitear.
- Donde diga **OÍDO: sí**, dejá el build listo, marcá la tarea como
  `LISTA PARA ESCUCHAR` acá en TASKS.md y **seguí con la siguiente** tarea
  autónoma. La persona sube y escucha aparte; no bloquees esperándola.
- No rompas las **REGLAS DURAS** de armonía (CLAUDE.md). No reintroduzcas AMY.
  Mantené la separación modular y los comentarios en español.
- Si tras 2–3 intentos algo no compila / no pasa tests, marcá la tarea como
  `BLOQUEADA` con el detalle y pasá a la siguiente.

## Definición de "verificado"
- **Compila**: `arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,PSRAM=opi" .` sin errores.
- **Tests host**: `test/run_tests.ps1` devuelve 0.
- Para audio, los tests son **numéricos y deterministas** (no requieren oído):
  sin NaN/Inf, sin clip (|x|<=1), DC ~0, RMS en rango, frecuencia esperada
  (Goertzel), respuesta al impulso correcta, etc. Generar un `.wav` opcional
  para que la persona escuche.

---

## FASE 0 — Infraestructura de verificación (100% autónoma, sin oído)

### T0.0  Compilador de host
Asegurar `g++` o `clang++` (MinGW o LLVM). Si falta, instalarlo
(`winget install -e --id LLVM.LLVM` para clang) y verificar.
Aceptación: `test/run_tests.ps1` compila y corre. OÍDO: no.

### T0.1  Test de armonía (incluido — revisar y extender)
`test/test_harmony.cpp` ya verifica: notas diatónicas, sin dominante, sin
b9/b2, tope de voces, registro y saltos de voice leading. Corré
`run_tests.ps1`, confirmá verde y agregá casos faltantes.
Aceptación: tests en verde. OÍDO: no.

### T0.2  Separar el núcleo DSP del hardware  ← clave para todo lo demás
Extraer la síntesis pura (osciladores, envolvente, filtro, render por muestra
y mezcla) a `dsp.h/.cpp`, **sin** `ESP_I2S` ni FreeRTOS. `synth.cpp` queda
como wrapper fino (I2S + tarea de núcleo 0 + cola de eventos) que llama a
`dsp`. El núcleo DSP debe compilar **también en host** (g++).
Aceptación: firmware compila (arduino-cli) Y `dsp` compila en host; tests de
armonía siguen verde. OÍDO: sí (confirmar que suena igual que antes).

### T0.3  Test de audio en host
`test/test_dsp.cpp`: renderizar a buffer float N segundos de un acorde por
timbre y verificar: sin NaN/Inf, sin clip, DC ~0, RMS en rango, fundamental
cerca de la frecuencia esperada (Goertzel por nota). Exportar
`test/out_<timbre>.wav` (16-bit) para escucha opcional. Sumalo a `run_tests.ps1`.
Aceptación: tests en verde; .wav generados. OÍDO: opcional.

---

## FASE 1 — Calidad del motor (tests numéricos + escucha final)

### T1.1  Anti-aliasing PolyBLEP en saw y cuadrada
PolyBLEP en STRINGS (saw) y BRASS (cuadrada) para limpiar el alias en agudos.
Aceptación: test de host mide menor energía de alias en una nota aguda vs. la
versión naive. OÍDO: sí.

### T1.2  ADSR real por timbre
Generalizar la envolvente a ADSR (A/D/S/R) por timbre, sin clicks (sin saltos
bruscos entre muestras en bordes de nota).
Aceptación: test verifica llegada a sustain, release a 0, y sin
discontinuidades > umbral. OÍDO: sí.

### T1.3  Filtro pasa-bajos resonante + envolvente de filtro
Reemplazar el LPF de 1 polo por uno de 2 polos (state-variable) con cutoff y
resonancia por timbre, modulado por envolvente de filtro.
Aceptación: test verifica atenuación de agudos y estabilidad (sin desbordes)
en barrido de cutoff/resonancia. OÍDO: sí.

### T1.4  LFO: vibrato + trémolo
Módulo LFO con rate/depth por timbre: vibrato (pitch) y trémolo (amplitud).
Aceptación: test mide la modulación en el buffer. OÍDO: sí.

---

## FASE 2 — Efectos (corazón "dreamy"; el HiChord tiene delay y reverb)

### T2.1  Delay estéreo
Delay con tiempo, realimentación y mezcla. Salida estéreo real (L≠R).
Aceptación: respuesta al impulso con ecos en el retardo correcto y
realimentación que decae (estable). OÍDO: sí.

### T2.2  Reverb (Schroeder / Freeverb-lite)
Reverb simple (combs en paralelo + allpass en serie), mezcla ajustable.
Aceptación: respuesta al impulso decae suave (RT60 en rango), sin clip/NaN,
estable. OÍDO: sí.

---

## FASE 3 — Funciones del HiChord (emulables en este hardware)

### T3.1  Modo STRUM (la firma Omnichord/HiChord)
Disparar el acorde sostenido nota por nota con un retardo de rasgueo
(grave→agudo), velocidad seleccionable. Mapear control (modo + joystick).
Aceptación: test verifica note-on espaciados por el retardo y en orden
ascendente. OÍDO: sí.

### T3.2  Arpegiador con BPM
Reloj de tempo; notas del acorde una por vez sincronizadas; patrones
up/down/updown; tap tempo opcional.
Aceptación: test verifica tiempos de nota en las subdivisiones del BPM y orden
de patrón correcto. OÍDO: sí.

### T3.3  Octava global (−1..+2)
Cablear `octaveOffset` (ya está en AppState) a un control.
Aceptación: test verifica desplazamiento de fundamental ±12 semitonos. OÍDO: sí.

### T3.4  Ancho estéreo en cuerdas
Repartir el detune de STRINGS entre L/R para dar amplitud.
Aceptación: test verifica decorrelación L/R. OÍDO: sí.

---

## FASE 4 — Paleta y cierre

### T4.1  Más formas de onda / motores
Sumar seno, triángulo y un "órgano" (multi-cuadrada/aditivo) para acercarse a
la paleta del HiChord.
Aceptación: test de fundamentales y nivel por timbre. OÍDO: sí.

### T4.2  Integración final
Firmware compila con todo activo; actualizar OLED si hace falta; dejar
`CLAUDE.md` al día con el estado nuevo.
Aceptación: compila + tests verde. OÍDO: sí (pasada general).

---

## Fuera de alcance (no implementar sin pedido)
Samples, looper, secuenciador, drums, MIDI USB, menús extensos. Anotar ideas,
no codear.

---

## Bitácora (el agente la completa)
- [x] T0.0  — hecha (g++ WinLibs + clang LLVM; núcleo de armonía; 2977 checks verde)
- [x] T0.1  — hecha (tests extendidos: 12 tonalidades, VL menor, anti-dominante en V; 11962 checks verde)
- [x] T0.2  — **LISTA PARA ESCUCHAR** (DSP puro en dsp.h/.cpp portado del MVP;
              synth.cpp = wrapper I2S+FreeRTOS; .ino demo I-IV-V-vi. Firmware compila
              30% flash, dsp compila en host, armonía verde. Suena igual que el MVP:
              BRASS cuadrada / EPIANO FM / STRINGS 3 saws.)
- [x] T0.3  — hecha (test/test_dsp.cpp: NaN/Inf, clip, DC~0, RMS, Goertzel por nota
              vs sonda 100 Hz; 15 checks verde. Genera test/out_{brass,epiano,strings}.wav.
              OÍDO: opcional — WAVs listos para escuchar.)
- [x] T1.1  — **LISTA PARA ESCUCHAR** (PolyBLEP en saw STRINGS y cuadrada BRASS;
              alias a 7000 Hz reducido ~400x/440x vs naive. 17 checks verde, firmware 30%.)
- [x] T1.2  — **LISTA PARA ESCUCHAR** (envolvente refactorizada a módulo Adsr
              testeable: A/D/S/R por timbre en ms, retrigger legato sin click,
              robo de la voz más silenciosa. Test verifica sustain, release a 0
              y que ningún salto supera el slew rate. 25 checks verde, firmware 30%.)
- [x] T1.3  — **LISTA PARA ESCUCHAR** (LPF 1 polo -> SVF 2 polos TPT estable, con
              cutoff/resonancia por timbre y envolvente de filtro que abre el cutoff
              (fEnvOct octavas) a tasa de bloque. Test: 5000 Hz atenuado a 8e-5, 200 Hz
              pasa a 0.97, estable en 30 combos cutoff/Q. 29 checks verde, firmware 30%.)
- [x] T1.4  — **LISTA PARA ESCUCHAR** (módulo Lfo por voz con rate/depth por timbre;
              vibrato (pitch) y trémolo (amplitud). STRINGS con shimmer (trem 0.18),
              brass firme. Test: rate 4.90 Hz, vibrato ratio 20x, trémolo modDepth 0.65
              a 5.2 Hz. 34 checks verde, firmware 30%.)
- [x] T2.1  — **LISTA PARA ESCUCHAR** (delay estéreo: L 250 ms / R 187 ms (3/4) -> L≠R,
              feedback 0.35, mezcla 0.30. Salida del DSP ahora estéreo (dspRenderStereo);
              synth.cpp manda L/R reales al I2S. Test de impulso: ecos 1.0/0.5/0.25 al
              retardo correcto, decae estable, L≠R. 41 checks verde, firmware 30% flash,
              RAM 47% por los buffers del delay.)
- [x] T2.2  — **LISTA PARA ESCUCHAR** (reverb Freeverb-lite: 4 combs amortiguados +
              2 allpass, room/damp/mix ajustables, en serie antes del delay. Test de
              impulso: RT60 1.18 s, decae suave, estable, sin NaN/clip. 45 checks verde,
              firmware 30% flash, RAM 54%.)
- [x] T3.1  — **LISTA PARA ESCUCHAR** (modo STRUM: dspMakeStrum ordena ascendente y
              espacia por el retardo; scheduler en blockSetup cuela los note-on a su
              tiempo. synthSetStrum(on,ms) + rama de strum en synthPlaySet (vía cola).
              Demo del .ino con rasgueo 35 ms. Test: plan 60<64<67 paso 1323; en vivo
              disparos a 256/1536/2816. 51 checks verde, firmware 30%.)
- [x] T3.2  — **LISTA PARA ESCUCHAR** (arpegiador: dspArpStepIndex (UP/DOWN/UPDOWN, puro)
              + reloj en blockSetup que apaga la nota previa y dispara la siguiente en la
              subdivisión del BPM. synthSetArp(on,bpm,stepsPerBeat,pattern) vía cola.
              Test: patrones OK; en vivo notas 60,64,67,60,64,67 a 11025 muestras
              (corcheas@120). 57 checks verde, firmware 30%.)
- [x] T3.3  — **LISTA PARA ESCUCHAR** (octava global: harmonyApplyOctave transpone ±12
              por octava (clamp 0..127), cableada a app.octaveOffset en el .ino tras el
              voice leading. Test de armonía: fundamental y todas las notas se desplazan
              12*oct para oct -1..+2. Firmware 30%.)
- [x] T3.4  — **LISTA PARA ESCUCHAR** (ancho estéreo en cuerdas: render por voz estéreo
              (renderVoiceStereo); los 3 saws se reparten L/R (0.7/0.5/0.3 vs 0.3/0.5/0.7)
              con filtro por canal. Otros timbres mono. Test: corr(L,R) STRINGS=0.815
              (ancho), BRASS=1.000 (mono). 59 checks verde, firmware 30%.)
- [x] T4.1  — **LISTA PARA ESCUCHAR** (paleta extendida: T_SINE (seno), T_TRIANGLE
              (triángulo) y T_ORGAN (aditivo, 4 drawbars). ENV/filtro/LFO por timbre.
              Test: fundamentales fuertes y RMS sano (SINE 0.19 / TRI 0.17 / ORGAN 0.14);
              WAVs generados. 74 checks verde, firmware 30%.)
- [x] T4.2  — **LISTA PARA ESCUCHAR** (integración final: firmware compila con todo
              activo (30% flash, RAM 54%); .ino demo recorre los 6 timbres con la
              progresión (pasada general); CLAUDE.md actualizado con el estado del motor.
              OLED se deja para cuando se cablee la UI. 74 checks verde.)

Fases 0–4 COMPLETAS.

## FASE 5 — Controles físicos (UI)
- [x] T5.1  — voicing/inversiones (CLOSE/INV1/INV2/OPEN) + pines de control en
              config.h; harmonyApplyVoicing con test host (conserva el acorde).
- [x] T5.2  — portado el módulo input.h/.cpp (7 grados, HOLD, VOICING, NAV±,
              joystick por ADS1115). Compila 32% flash.
- [x] T5.3  — **LISTA PARA PROBAR EN PLACA** (loop() real: botones/joystick
              reemplazan el demo. Tocar grado, HOLD, ciclar voicing, transponer
              tónica ±semitono, color por joystick, TAP=timbre, gesto=modo.
              Compila 33% flash, tests host verde. Probar con la mano.)

- [x] T5.4  — **LISTA PARA PROBAR EN PLACA** (strum/arp por pulsación larga:
              HOLD tap=sostener / largo=STRUM on-off; VOICING tap=inversión /
              largo=arp (off→up→down→updown). NAV con SW sostenido = BPM 40–300.
              Strum y arp excluyentes. Compila 33%, tests host verde.)

## FASE 6 — Sonido + feel (acercar a la versión premium del HiChord)
- [x] T6.1  — EPIANO Rhodes FM (cuerpo cálido + tine metálico rápido). out_epiano.wav.
- [x] T6.2  — BRASS HiChord (dos pulsos detuneados, ancho 0.32 reedy, filtro estable).
              Nuevo dspOscPulseBlep. out_brass.wav.
- [x] T6.3  — Humanización del toque: micro-retardo de arranque + variación de
              amplitud por voz (vel). dspNoteOn(note,timbre,vel,delay). Test verde.
- [x] T6.4  — Más brillo general (cutoffs + sostenido de filtro arriba; reverb damp 0.3).
- [x] T6.5  — Ancho estéreo por voz (paneo equipotencia por nota). Análisis de la
              referencia (claude.wav): corr L/R ≈ 0 (ultra-ancha), tilt -5.9 dB/oct
              (rica, no opaca), reverb -20dB en 0.38s (≈ la mía). El gap era el ANCHO:
              acordes pasan de corr 1.0 (mono) a 0.57 (BRASS)/0.49 (ORGAN). Herramienta
              test/analyze.py para comparar WAVs. **A AUDITAR en placa.**

## FASE 7 — Premium (octava, mono, glide)
- [x] T7.1  — Motor: glide/portamento (curMidi->targMidi por voz) y modo monofónico
              (dspMonoNote legato + dspMonoOff). dspSetGlide.
- [x] T7.2  — **LISTA PARA PROBAR EN PLACA** — wrapper synth (eventos mono/glide) +
              capa SHIFT (click del joystick): SW+NAV = octava (-1..+2), SW+HOLD =
              mono on/off, SW+VOICING = glide on/off. Test: mono=1 voz; glide desliza
              (p220>p440) vs salta. Firmware 33% flash, host 12004 + 83 verde.
- [x] T7.3  — glide polifónico entre acordes (portamento de acorde); refinado luego a
              glide de TIEMPO constante (~220 ms, todas las voces llegan juntas).

## FASE 8 — Expresión y conectividad (realizada; antes "fuera de alcance")
> Trabajo posterior a la Fase 7, derivado de la práctica. Lo que se marcaba como
> "fuera de alcance" (MIDI) o "no conectado" (OLED) ya está implementado.
- [x] T8.1  — MIDI IN: USB (TinyUSB) + BLE (NimBLE, aparece como "TOLHUIN"). Clock
              externo (0xF8) sincroniza delay y arp; note on/off tocan el synth. (`midi.cpp`)
- [x] T8.2  — MIDI OUT: manda lo que tocás por USB + BLE para grabar en DAW. El acorde
              entero sale en UN solo paquete BLE con running status (fix "solo nota grave").
- [x] T8.3  — Paleta de color del joystick más rica: 8 zonas distintas + centro.
- [x] T8.4  — Slash-bass / inversiones por grado sostenido: mantené un grado y tocá otro
              -> el sostenido es el bajo (slash / pedal). Bajo siempre en la raíz, con
              línea de bajo que se mueve entre acordes.
- [x] T8.5  — Delay editable y sincronizable a tempo (SW+grado 5/6/7): tiempo
              (1/16..1/4), profundidad (off..fuerte) y TAP TEMPO. Presets de armonía
              (SW+grado 1-4: básico+8va / 7as / 9as / 6-9).
- [x] T8.6  — Joystick: auto-calibración de rango (arregla lado muerto / direcciones).
- [x] T8.7  — OLED SSD1306 128x64 CONECTADA (`oled.cpp`, cableada en el .ino): acorde
              completo arriba (nombres enarmónicos según zona+preset) + osciloscopio
              grande al centro (forma de onda real vía synthGetScope) + timbre/flags +
              punto BLE. Redibuja por cambio o ~15 fps.
- [x] T8.8  — **LISTA PARA PROBAR EN PLACA** — SUSTAIN conmutable: al soltar la tecla,
              las voces resuenan con un release largo (~1.5 s) en vez de cortarse, como
              un sinte de acordes. Motor: dspSetSustain(on,relMs) + helper voiceRelease
              que alarga el release en vivo al soltar (amp + filtro). Control: SW del
              joystick sostenido + DERECHA = ON / IZQUIERDA = OFF (en paralelo al gesto
              de modo, que es arriba/abajo). Flag 'S' en el OLED. Test host: a 0.5 s de
              soltar, sin sustain rms=0.0000 (callado) vs con sustain rms=0.0451
              (resuena). 93 checks verde, firmware 55% flash / 68% RAM.
- [x] T8.9  — **LISTA PARA PROBAR EN PLACA** — TREMOLO global sincronizado al BPM:
              modula la amplitud de la salida (al final de la cadena, tras reverb/delay).
              Motor: dspSetTremolo(on,rateHz,depth) + LFO de salida en dspRenderStereo.
              Wrapper synthSetTremolo (cola, evento 15). Control: SHIFT+VOICING LARGO
              cicla off->1/4->1/8->1/16 (subdivisión del pulso); el rate = (BPM/60)*subdiv
              sigue al BPM (tap-tempo SW+grado7 / MIDI clock). Profundidad fija 0.6.
              Flag 'T' en el OLED. Test host: modulación a 6 Hz = 393x sobre off. 100
              checks verde.

## FASE 9 — Matching de timbres contra el HiChord (medido)
> Con clips aislados del HiChord (flutes/saws/sawsbass) + herramientas de análisis
> espectral, se mide la diferencia y se ajusta el motor. Herramientas en test/:
> wav_segmap.py (mapa tiempo->contenido), wav_fingerprint.py (huella armónica +
> envolvente) y wav_compare.py (firma espectral por bandas de octava, A/B).
- [x] T9.1  — **LISTA PARA ESCUCHAR** — timbre FLUTE nuevo (T_FLUTE). Análisis: el
              "flutes" del HiChord es fundamental dominante (~71%) con cola armónica
              + aliento, centroide ~1149 Hz; nuestro SINE era 96% fundamental puro
              (muerto). Implementación: oscilador aditivo (H1 1.0 + H2 .40 + H3 .22 +
              H4 .12 + H5 .06) + ruido de aliento (dspNoise) filtrado/envuelto; ataque
              blando (80 ms), vibrato 5.5 Hz, filtro abierto sin resonancia. A/B medido:
              centroide 1246 Hz vs 1149 Hz del HiChord (within 8%). 98 checks verde,
              genera out_flute.wav. Cicla con TAP del joystick (OLED: "FLU").

## FASE 10 — Esquema de FX estilo HiChord (menú navegable)
> Referencia: el HiChord usa un botón FX + joystick para un menú de efectos
> (CHORUS/DELAY/TREMOLO/GLIDE...) y FX+click = Random Sound. Se adopta el
> paradigma en TOLHUIN usando el SW del joystick como modificador.
- [x] T10.1 — Motor: efecto CHORUS estéreo (LFO de retardo corto, dos tomas L/R en
              cuadratura) + Generate Random Sound (dspRandomizeSound/dspResetSound con
              ENV_DEFAULT). Wrappers synthSetChorus/synthRandomizeSound. Tests: chorus
              ensancha (corr 0.772->0.487), random cambia (diff 0.0355) y reset restaura
              idéntico. (commit del motor)
- [x] T10.2 — **LISTA PARA PROBAR EN PLACA** — MENÚ FX navegable. SW sostenido +
              Izq/Der navega la lista (CHORUS/DELAY/TREMOLO/GLIDE/SUSTAIN/MONO/OCTAVE/
              MODE/RANDOM), + Abajo actúa (toggle/cicla rhythm rate), + Arriba va atrás.
              Se ve en el OLED (oledFxMenu) mientras se mantiene el SW. Reubica modo y
              sustain (antes en SW+direcciones) al menú; los atajos SHIFT+botón
              (octava/mono/glide/preset/delay/tap) quedan como acceso rápido. Flag 'C'
              de chorus en el OLED. Firmware 55% flash / 70% RAM, 103 checks host verde.

- [x] T10.3 — Joystick: flags de orientación JOY_INVERT_X/Y/SWAP_XY en config.h
              (la auto-calibración sólo ajusta centro/escala, no la polaridad). Default
              0 = sin cambio; corrige una dirección invertida o ejes cruzados sin recablear.
- [x] T10.4 — **LISTA PARA ESCUCHAR** — capa BODY (cuerpo/unísono), ítem del menú FX.
              Investigación: el HiChord suena más "musical" sobre todo por usar SAMPLES
              reales + capas híbridas (fuera de alcance acá); la armonía la igualamos.
              Aproximación por síntesis: 3 offsets de pitch por voz (sin tocar los
              osciladores → preserva la identidad del timbre): detune fijo ±5 cents
              (spread de ensamble), drift random-walk lento (inestabilidad analógica) y
              transitorio de ataque que se asienta (~35 ms). dspSetBody(on,amt) +
              synthSetBody. Test: BODY on cambia el sonido (diff 0.0547), off determinista
              (0.000000). 105 checks verde. Flag 'B' en el OLED. Ver memoria
              hichord-engine-reference.

- [x] T10.5 — **FIX** ruido/crack al cambiar de acorde en modo NO-HOLD con sustain.
              (a) Robo de voz suave: robar PRIMERO una voz en RELEASE (protege las que
              suenan) con keepPhase=true (sin reiniciar fase/filtro -> sin click).
              (b) Causa real (sólo con FLUTE): era CPU. La FLUTE (5 senos aditivos +
              ruido por muestra) era el oscilador más caro; con sustain se acumulaban
              hasta 12 voces y el render se pasaba del bloque -> underrun del I2S
              ("se rompe el buffer"). Fix: precomputar FLUTE y ORGAN en WAVETABLES
              (suma de armónicos fija) -> 1 lookup por muestra en vez de 5/4 senos,
              mismo sonido exacto (rms/fundamentales idénticos). RAM 70->73% (dos tablas
              de 1024). 111 checks verde, firmware 55%.

## FASE 11 — MIDI clock (master + sync fino) y más
> Features evaluadas como viables (ver memoria features-planificadas): MIDI clock,
> EDIT armónico por grado y looper de 4 capas.
- [x] T11.1 — **LISTA PARA PROBAR EN PLACA** — MIDI clock OUT (TOLHUIN como master de
              tempo). midiClockOutSet/Update en midi.cpp: al activar manda Start (0xFA) y
              emite 0xF8 a 24 PPQN desde app.bpm (USB-MIDI CIN 0xF + BLE realtime); al
              desactivar manda Stop (0xFC). Sigue el BPM en vivo (tap/NAV/sync). Control:
              ítem "CLK OUT" en el menú FX (off/out). Firmware 55% / 73%. Capa de hardware,
              no testeable en host -> a probar con un equipo MIDI externo.
- [x] T11.2 — **LISTA PARA PROBAR EN PLACA** — Sync IN fino del arp. Con un clock MIDI
              externo vivo, el arp avanza fase-bloqueado a los ticks (12 ticks = corchea)
              en vez de su reloj interno. Núcleo: dspArpAdvance() + dspArpSetExtSync()
              (arpTriggerStep factorizado; blockSetup salta el contador interno si ext).
              midi.cpp cuenta ticks (midiClockTickCount/midiClockLive, reset en Start).
              El .ino engancha cuando arp on + clock vivo + no somos master. Test host:
              sin ticks no avanza; 4 ticks -> 60,64,67,60 (patrón UP). 113 checks verde,
              firmware 55%/73%.
- [ ] T11.3 — EDIT armónico por grado (chordQual[7]); rompe la regla diatónica en modo
              opt-in. Pendiente (ver memoria).
- [~] T11.5 — Percusión: SONIDOS de bombo (kick) y caja (snare) HECHOS. Síntesis pura
              (dspTriggerKick/Snare + dspDrumTick), mezclados en la salida seca (el looper
              los capta). Kick = seno con caída de tono+amplitud; caja = ruido + 2 senos.
              synthKick/synthSnare (eventos 27/28). Test host testDrums (kick grave+decae,
              caja con agudos). 126 checks verde. FALTA decidir el DISPARO on-device:
              botones manuales vs patrón sincronizado al BPM vs notas MIDI.
- [x] T11.4 — **LISTA PARA PROBAR EN PLACA** — Looper de 4 capas COMPLETO (audio, PSRAM,
              post-mezcla/pre-efectos). Núcleo dsp (LpLayer EMPTY/REC/PLAY/OVERDUB/MUTED,
              largo maestro por la 1ra capa, soft-clip) + ps_malloc 4x8s en PSRAM + wrappers
              thread-safe + tests host (120 checks). Transporte STOP (gLpRun). CONTROL en
              vivo (modo LOOPER del menú FX): los grados siguen tocando; SW tap = rec/play/
              overdub de la capa activa; NAV± = elegir capa; VOICING tap = mute / largo =
              clear; HOLD largo = STOP+salir. OLED con estado de las 4 capas. Firmware
              56%/73%, 120 checks verde. A probar en placa con la PSRAM real.

Hallazgos de la FASE 9 (pendientes de implementar):
- **saws / STRINGS**: el HiChord reparte energía pareja hasta >8 kHz (14% vs 3% nuestro);
  nos falta aire arriba. Subir cutoff del filtro de STRINGS + aflojar damping.
- **sawsbass**: saw grave brillante (A1 con armónicos hasta 8 kHz); no hay timbre de bajo.

Pendiente real: terminar el matching de timbres (STRINGS más brillante, bajo saw);
ítems "LISTA PARA PROBAR/ESCUCHAR EN PLACA" esperan oído.
Fuera de alcance (sin pedido): samples, looper, secuenciador, drums.

## FASE 12 — Editor web por USB (webtool/) + protocolo de config

- [x] T12.1 — EDICIÓN DE TIMBRES EN VIVO: EnvCfg público en dsp.h + 3 niveles
              (ENV vivo / ENV_DEFAULT preset del usuario / ENV_FACTORY inmutable).
              dspEnvGet/Set/Factory; el set escribe vivo+usuario -> la edición
              sobrevive al cambio de timbre y al Random. Eventos 30/31 en la cola
              (synthEnvApply / synthDrumPatternSet, thread-safe). Test host
              testEnvEdit (134 checks verde).
- [x] T12.2 — DRUM PATTERNS EDITABLES: DRUM_KICK/SNARE ahora 8 slots (0=off,
              1-3 fábrica pisables, 4-7 usuario). dspDrumPatternSet/Get; el menú
              DRUMS del OLED cicla los 8 (user1..user4). Test testDrumPatternEdit.
- [x] T12.3 — PROTOCOLO SERIE (webcfg.h/.cpp): comandos '#...' por el CDC USB,
              respuestas JSON de 1 línea. #hello/#getenv/#setenv/#factory/
              #getdrum/#setdrum/#drums/#noteon/#chord/#save/#load. Persistencia
              NVS (Preferences "tolhuin": blob EnvCfg x7 + máscaras de drums,
              versionado por sizeof). webcfgInit carga al boot; webcfgPoll en loop().
- [x] T12.4 — EDITOR WEB (webtool/index.html, estático, Web Serial): pestañas
              Timbres (sliders en vivo con debounce), Drums (grilla 2x8),
              Firmware (ESP Web Tools + manifest.json -> merged.bin de
              arduino-cli --output-dir) y Consola. Ver webtool/README.md.
              A PROBAR EN PLACA (Web Serial requiere Chrome/Edge + localhost/HTTPS).

## FASE 13 — Referencia HiChord companion (app.hichord.shop) aplicada

Análisis de la app real del HiChord (JS sin minificar): usa Web MIDI (CCs para
escalares + SysEx 0x7D para osciladores/arp/samples). Su sonido = 6 roles de voz
(BASS/ROOT/3RD/5TH/EXT1/EXT2) x 2 osc L/R c/u (waveform/FM/sample + pan/gain/
detune). Su piano-roll es un VISUALIZADOR: clasifica las notas MIDI entrantes por
rol (bajo si gap>7 semitonos, luego raíz/3ra/5ta/ext). Presets en localStorage
con export/import. Adoptado en TOLHUIN:

- [x] T13.1 — REVERB EDITABLE: dspSetReverb(room,damp,mix) + synthSetReverb
              (evento 32). Antes era fijo; el HiChord expone reverb amount/feedback.
- [x] T13.2 — PROTOCOLO AMPLIADO (webcfg 1.1): #state (sync de UI al conectar,
              como su state-request SysEx 0x02), #degree (toca grados con el motor
              de armonía real y devuelve notas+símbolo), #timbre/#key/#mode/
              #octave/#voicing/#preset (estado real del equipo, extern AppState),
              #chorus/#reverb/#delay/#trem/#body/#glide/#sustain (fx en vivo;
              sincronizan los flags del menú físico donde existen).
- [x] T13.3 — EDITOR: pestaña ACORDES (7 grados + pad de color 3x3 + selectores
              tónica/modo/octava/voicing/riqueza + visualizador de voicing con
              roles coloreados, clasificación portada del piano-roll del HiChord
              + símbolo del acorde que calcula el firmware con enarmonía).
- [x] T13.4 — EDITOR: pestaña EFECTOS (chorus/reverb/delay/tremolo/body/glide/
              sustain con sliders en vivo) y pestaña PRESETS (captura completa
              del equipo -> localStorage, aplicar, exportar/importar .json,
              opción de persistir en NVS), como la librería del HiChord.
              Verificado en preview (Dm7/G9 con roles correctos). 134 checks verde.

NO adoptado (decisión): arquitectura de 6 roles x 2 osc por acorde (cambio de
motor grande; nuestro EnvCfg por timbre + BODY cubre el grueso), samples de
usuario (sin PSRAM de sobra con el looper), vocoder/flanger (no hay mic; scope).

- [x] T13.5 — TRANSPORTE MIDI del editor (fix: por Serial CDC el RX no andaba en
              placa; el HiChord usa Web MIDI). El MISMO protocolo '#...' viaja en
              SysEx: comando F0 7D 01 <ASCII> F7, respuesta F0 7D 02 <JSON> F7.
              midi.cpp: despacho por CIN (los paquetes SysEx ya no se confunden
              con status), reensamblado + midiSendSysExText; webcfg 1.2: salida
              dual (Serial siempre + buffer SysEx si vino por MIDI, outf()).
              Web: "Conectar por MIDI" (recomendado, requestMIDIAccess sysex) +
              Serial como alternativa; el teclado del visualizador se pinta EN
              VIVO con las notas MIDI OUT del equipo (como el piano-roll del
              HiChord). Verificado en preview con SysEx fragmentado + note on/off
              simulados. 134 checks verde, firmware 59%/73%.
- [x] T13.6 — FIX conexión del editor: estados INDEPENDIENTES Serial/MIDI (antes
              un flag compartido: al conectar MIDI con Serial abierto, la
              desconexión asíncrona pisaba el estado -> "(no conectado)" con el
              MIDI vivo). Selector visible de puerto MIDI (lista en el log, sin
              fallback a ciegas al primer dispositivo; auto-elige "TOLHUIN" si
              está), aviso si #hello no responde en 2 s (firmware viejo / puerto
              equivocado). Firmware: TinyUSBDevice.setProduct/Manufacturer
              ("TOLHUIN Chord") — ojo: Windows cachea el nombre USB por VID/PID.

- [x] T14.1 — MAPAS DE JOYSTICK seleccionables por menú (página JOYSTICK, fila
              JOY): DEFLT (el mapa diatónico original), HICHRD (réplica del modo
              "Default" del HiChord, manual oficial: flip M/m, 7ma por calidad,
              sus4, oscurecer->dim, dom7 blues, add9, 6/sus2, 5ta aumentada) y
              MINI (tipos fijos del minichord/Omnichord sobre la raíz del grado:
              maj/min/7/maj7/m7/6/m6/dim/aug — recetas sacadas del firmware
              open-source de B. Poilve, sin la octava doblada: el refuerzo lo da
              nuestro sub-bajo). HICHORD/MINI son cromáticos OPT-IN (mismo
              espíritu que CQ_DOM7, que sigue mandando sobre cualquier mapa);
              la raíz SIEMPRE es la diatónica del grado y el sub-bajo/voice
              leading/voicing/arp corren igual. harmonyChordJoy + símbolos
              joy-aware en el OLED (nameChord compartido + aug/m#5). Persiste en
              la sesión (SESS_VER 3: la sesión vieja se descarta UNA vez).
              54461 checks harmony (invariantes en todo el espacio + tabla
              puntual en Do mayor) + 157 DSP verdes; firmware 62%/74%.
