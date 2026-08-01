// SPDX-License-Identifier: GPL-3.0-or-later
// TOLHUIN Chord.  Copyright (C) 2026 Mauro Restivo.
// Distribuido bajo GPL-3.0-or-later; ver el archivo LICENSE.

#pragma once

#ifdef ARDUINO
#  include <Arduino.h>
#else
#  include <cstdint>
#endif

// Zona de color del joystick / pad táctil
enum ColorZone : uint8_t {
    CZ_CENTER     = 0,
    CZ_UP         = 1,
    CZ_DOWN       = 2,
    CZ_LEFT       = 3,
    CZ_RIGHT      = 4,
    CZ_UP_RIGHT   = 5,
    CZ_UP_LEFT    = 6,
    CZ_DOWN_RIGHT = 7,
    CZ_DOWN_LEFT  = 8
};

// Disposición de voces (el botón VOICING las cicla). Foco en inversiones.
enum Voicing : uint8_t {
    VC_CLOSE = 0,   // posición fundamental (cerrada)
    VC_INV1  = 1,   // 1ra inversión (sube el bajo una octava)
    VC_INV2  = 2,   // 2da inversión (sube las dos voces más graves)
    VC_OPEN  = 3,   // abierta (sube la voz interna para dar amplitud)
    VC_COUNT = 4
};

// Modo / escala
enum Mode : uint8_t {
    MODE_IONIAN  = 0,   // mayor
    MODE_AEOLIAN = 1    // menor natural
};

// Calidad forzada por grado (edición armónica opt-in con el SW del joystick).
// Rompe a propósito la regla diatónica: CQ_DOM7 convierte el grado en un
// dominante 7 (3ra mayor + b7), estilo acordes prestados / "Creep".
enum ChordQual : uint8_t {
    CQ_DIATONIC = 0,    // sin forzar: acorde diatónico normal
    CQ_DOM7     = 1     // dominante 7 (mayorizado): R 3M 5J b7
};

// Mapa del joystick: cómo las 9 zonas colorean/transforman el acorde (fila JOY
// del menú). DEFAULT es el mapa original de TOLHUIN (100% diatónico, reglas
// duras). Los otros dos son OPT-IN CROMÁTICOS (mismo espíritu que CQ_DOM7):
// elegir el mapa es aceptar dominantes/dim/aug fuera de la escala.
enum JoyMap : uint8_t {
    JM_DEFAULT = 0,   // original TOLHUIN (colores diatónicos seguros)
    JM_HICHORD,       // modo "Default" del HiChord: transforma la tríada del grado
    JM_MINI,          // tipos fijos del minichord/Omnichord (maj/min/7/maj7/m7/6/m6/dim/aug)
    JM_COUNT
};

// CANAL de los 7 switches: qué "instrumento" tocan los botones de grado.
enum Channel : uint8_t {
    CH_CHORDS = 0,   // acordes (comportamiento clásico)
    CH_MELODY,       // melodía monofónica (equivale a MONO on)
    CH_DRUMS,        // los switches TOCAN cuerpos: 1=bombo 2=caja 3=hi-hat
                     // (4..7 reservados para open hat / crash futuros); con GRB
                     // on, los golpes se GRABAN cuantizados al patrón que suena
    CH_DRUMPAT,      // los switches ELIGEN el patrón de batería (1..7) en vivo
    CH_COUNT
};

// Estado global de la aplicación (expandir según se necesite)
struct AppState {
    uint8_t   tonic;        // 0-11 (C=0)
    Mode      mode;
    uint8_t   degree;       // grado actual (0-6)
    ColorZone zone;
    Voicing   voicing;
    int8_t    octaveOffset; // -1..+2
    bool      hold;         // HOLD = acorde sostenido aunque se suelten los botones
    bool      strum;        // modo rasgueo activo (HOLD largo)
    uint8_t   arpMode;      // arpegiador: 0=off, 1=on (usa el slot arpSlot)
    uint8_t   arpSlot;      // preset de arp activo (0..ARP_SLOTS-1), editable por web
    uint16_t  bpm;          // tempo del arpegiador
    bool      mono;         // modo monofónico (SW+HOLD)
    bool      glide;        // glide/portamento (SW+VOICING)
    bool      sustain;      // sustain conmutable: release largo al soltar (menú FX)
    bool      chorus;       // chorus estéreo on/off (menú FX)
    bool      body;         // capa de cuerpo/unísono on/off (menú FX)
    bool      subBass;      // sub-bajo sine dedicado (raíz -1 oct); estilo HiChord
    bool      clockOut;     // MIDI clock OUT (TOLHUIN master de tempo) (menú FX)
    bool      drumsOn;      // batería sonando (toggle instantáneo, fila DRUMS)
    uint8_t   drumPat;      // patrón SELECCIONADO (1..DRUM_PATTERNS-1; nunca 0)
    uint8_t   tremMode;     // tremolo sync BPM: 0=off 1=1/4 2=1/8 3=1/16 (menú FX)
    uint8_t   preset;       // preset de armonía/voicing (menú RIQUEZA)
    uint8_t   joyMap;       // mapa del joystick (JoyMap, fila JOY del menú)
    int8_t    bassDegree;   // grado sostenido usado como bajo (slash/inversión); -1 = ninguno
    uint8_t   chordQual[7]; // calidad forzada por grado (CQ_DIATONIC/CQ_DOM7), SW+stick
    uint8_t   channel;      // Channel: qué tocan los 7 switches (fila CANAL del menú)
    bool      metro;        // metrónomo (click sync BPM, fila MET de SYNC)
};
