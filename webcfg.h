// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

/* ============================================================================
   webcfg.h  -  Configuración por USB (Web Serial) + persistencia NVS.
   Protocolo de texto por líneas sobre Serial (el mismo CDC del monitor):
   los comandos entran con prefijo '#', las respuestas salen como UNA línea
   JSON (empiezan con '{'), así el editor web filtra fácil entre los logs.

   Comandos:
     #hello                      -> info del firmware (timbres, patrones, SR)
     #state                      -> estado global (timbre, key, modo, fx...)
     #getenv <t>                 -> parámetros del timbre t (JSON, tiempos en ms)
     #setenv <t> k=v [k=v ...]   -> patch parcial del timbre t (aplica en vivo)
     #factory <t>                -> valores de fábrica del timbre t (no aplica)
     #getdrum <p>                -> patrón p (kick/snare como máscaras de 8 bits)
     #setdrum <p> <kick> <snare> -> escribe el patrón p (1..7)
     #drums <p> <bpm>            -> selecciona el patrón en vivo (0 = off)
     #noteon <n> / #noteoff <n>  -> audición de una nota MIDI
     #chord <n1,n2,...> / #chordoff -> audición de un acorde literal
     #degree <1..7> [zona 0..8]  -> toca el grado con el motor de armonía real
                                    (voice-leading + voicing + bajo) y devuelve
                                    las notas + símbolo del acorde (visualizador)
     #timbre <t>                 -> selecciona el timbre activo
     #key <0..11> / #mode <0|1> / #octave <-1..2> / #voicing <0..3> / #preset <0..3>
     #chorus <on> [rate depth mix]   #reverb <room> <damp> <mix>
     #delay <ms> <fb> <mix>          #trem <on> [rate depth]
     #body <on> [amt]                #glide <on> [ms]      #sustain <on> [ms]
     #save / #load               -> persiste / recarga timbres+drums desde NVS

   TRANSPORTES: el mismo protocolo viaja por dos vías (como el HiChord, que usa
   Web MIDI): (a) líneas de texto por el Serial USB (Web Serial), y (b) SysEx
   USB-MIDI — comando F0 7D 01 <texto ASCII> F7, respuesta F0 7D 02 <JSON> F7.
   La vía MIDI es la recomendada: convive con el monitor serie y con el DAW.

   webcfgInit() carga lo guardado en NVS y lo aplica (llamar tras synthInit);
   webcfgPoll() atiende el puerto serie (llamar en cada loop());
   webcfgFeedMidi() procesa un comando llegado por SysEx (lo llama midi.cpp).
   ============================================================================ */
#pragma once

void webcfgInit();
void webcfgPoll();
void webcfgFeedMidi(const char* text, int len);

// Guardar/recargar TODO en NVS (timbres + drums + arps + nombres + SESIÓN del
// intérprete: key/modo/riqueza/octava/bpm/timbre/patrón/arp/toggles de fx).
// Lo usan el comando #save/#load de la web Y la página MEMORIA del menú físico.
void webcfgSave();
bool webcfgLoad();
// Timbre guardado en la sesión (o `def` si no hay sesión): el .ino lo aplica al
// terminar el arranque (después del jingle, que pisa el timbre).
int  webcfgSessionTimbre(int def);
