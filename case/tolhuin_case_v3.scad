// =====================================================================================
//  TOLHUIN CHORD — Carcasa v3 (ESP32-S3 DevKit tamaño completo)
// -------------------------------------------------------------------------------------
//  Qué cambia respecto del prototipo anterior (tolhuin_case.scad, 24-ago):
//   * NADA se pega con hot-glue. Cada pieza tiene su soporte mecánico:
//       - Botones: switches mecánicos MX (o Kailh Choc) que se ENCASTRAN a presión en
//         el panel (placa de 1.5 mm). Se cablean directo a los GPIO, sin PCB.
//       - OLED y joystick: rieles con ranura colgando del panel (la placa entra
//         deslizando y queda trabada por un resalte).
//       - Placa principal: perfboard 5x7 cm atornillada (M2) a 4 parantes del piso.
//         La DevKitC-1 va enchufada en tiras de headers hembra sobre esa perfboard.
//       - TP4056 y buck-boost: guías con ranura en el piso (entran deslizando).
//       - Batería: bahía con paredes bajas (+ cinta doble faz o velcro si hace falta).
//   * Tamaño pensado para la DevKit S3 completa (63 x 27 mm con antena, medida real).
//   * Tornillos ocultos: 4 x M3 entran por ABAJO y roscan en columnas del panel.
//   * Chequeos automáticos (assert) de choques en altura entre switches, placa,
//     joystick y batería: si cambiás una medida y algo choca, OpenSCAD avisa.
//
//  Ejes: X = ancho (derecha +), Y = profundidad (+Y = fondo, lejos del músico),
//  Z = alto. Todas las medidas en mm. Las marcadas "MEDIR" hay que confirmarlas con
//  calibre antes de imprimir.
//
//  Exportar cada pieza cambiando `part` (o con -D part="top" desde la consola).
// =====================================================================================

part = "assembled";  // "assembled" | "exploded" | "top" | "bottom" | "caps_chord" | "caps_fn"
                     // | "caps_T" | "caps_S" | "caps_D" | "cap_joy" | "test_keys"
btn  = "mx";         // "mx" (Cherry MX y clones: Gateron, Outemu...) | "choc" (Kailh Choc v1, perfil bajo)
show_ghosts = true;  // en "assembled"/"exploded": dibuja componentes de referencia
theme       = "bosque";   // paleta (solo afecta el render; al imprimir = color de filamento):
                          // "bosque" | "glaciar" | "noche" | "lenga" | "mar" | "clasico"
chord_color = "funcion";  // "funcion": grados coloreados por función armónica
                          //   T (I, iii, vi) · S (ii, IV) · D (V, vii°) — vale en mayor y en menor
                          // "uniforme": los 7 grados del mismo color
deco        = true;       // grabados finos en el panel: marco del OLED, aro del joystick,
                          // línea que separa función/acordes (se pueden rellenar con fibra)
$fn = 48;

// ---------------------------------------------------------------- Envolvente
W        = 146;    // ancho exterior
D        = 88;     // profundidad exterior
wall     = 2.4;
floor_th = 2.4;
panel_th = 3.0;    // panel superior (en los switches queda la placa de sw_plate)
int_h    = btn == "mx" ? 30 : 25;  // alto interior (piso -> cara inferior del panel)
outer_r  = 6;      // radio de las esquinas verticales
top_cham = 1.6;    // chaflán del borde superior del panel (más amable para la muñeca)
bot_cham = 1.0;    // chaflán del borde inferior de la base
fit      = 0.3;    // holgura general entre piezas impresas

// ---------------------------------------------------------------- Unión panel <-> base
post_inset  = 7;     // centro de la columna desde el borde exterior
post_d      = 8;
post_pilot  = 2.5;   // M3 autorroscante en plástico (2.5). Para inserto térmico M3: 4.0
pilot_depth = 12;
foot_h      = 3.6;   // pedestal en el piso bajo cada columna (aloja la cabeza del tornillo)
screw_d     = 3.4;
screw_head  = 6.4;
screw_head_h= 3.2;   // -> tornillo M3 x 12
lip_h       = 3;     // pestaña de registro del panel (entra dentro de las paredes)
lip_th      = 1.6;

// ---------------------------------------------------------------- Switches
sw_cut    = btn == "mx" ? 14.0 : 13.8;  // agujero cuadrado de encastre
sw_plate  = btn == "mx" ? 1.5  : 1.3;   // espesor donde trabajan los clips
sw_pocket = btn == "mx" ? 15.4 : 15.2;  // vaciado por debajo (espacio para los clips)
pitch     = btn == "mx" ? 19.05 : 18.0; // paso entre teclas
// desde la cara superior de la placa hasta la punta de los pines (+ soldadura/cable)
sw_below  = btn == "mx" ? 5.0 + 3.3 + 1.5 : 2.2 + 3.0 + 1.5;

// ---------------------------------------------------------------- Layout del panel
key_x0 = 52;                               // centro de la tecla de más a la derecha
lo_y   = -27;                              // fila inferior de acordes (I..IV)
hi_y   = lo_y + pitch;                     // fila superior de acordes (V..VII)
fn_y   = hi_y + pitch + 5;                 // fila de función (separada 5 mm más)
lo_x   = [for (i = [3:-1:0]) key_x0 - i*pitch];
hi_x   = [for (i = [2:-1:0]) key_x0 - pitch/2 - i*pitch];
fn_x   = lo_x;
lo_lbl = ["I", "II", "III", "IV"];
hi_lbl = ["V", "VI", "VII"];
fn_lbl = ["-", "+", "HLD", "VOI"];        // NAV- (GPIO11) · NAV+ (12) · HOLD (18) · VOICING (7)

col_x  = -40;                              // columna izquierda: OLED (fondo) + joystick (frente)

// ---------------------------------------------------------------- OLED 0.96" SSD1306
oled_pos      = [col_x, 24];
oled_pcb      = [26, 26];    // medida real (prototipo anterior, calibre)
oled_pcb_th   = 1.2;         // MEDIR
oled_glass_th = 1.6;         // MEDIR: vidrio + cinta, sobre la PCB
oled_win      = [24, 13];    // ventana visible (área activa 21.7 x 10.9 + tolerancia)
oled_win_off  = [0, 0];      // MEDIR: corrimiento del área activa respecto del centro de la PCB
oled_rail_w   = 2.0;
oled_bite     = 0.9;         // cuánto pisa el riel el borde de la PCB (no tocar el vidrio)

// ---------------------------------------------------------------- Joystick HW-504
joy_pos       = [col_x, -22];  // eje del stick en el panel
joy_pcb       = [26, 33];      // medida real (calibre, confirmada)
joy_pcb_th    = 1.6;
joy_under     = 3.0;           // patas de los potes / soldaduras por debajo de la PCB
joy_stick_dy  = 3.5;           // MEDIR: el eje está corrido hacia el frente (lejos del header)
joy_shaft_top = 13;            // medida real: cara superior de la PCB -> punta del eje SIN capuchón
joy_shaft_out = 0.5;           // cuánto asoma la punta del eje sobre el panel
joy_hole_d    = 16;            // agujero del panel (deja inclinar el eje)
joy_skin      = 1.2;           // el panel se adelgaza alrededor del joystick (el eje es corto)
joy_pocket_d  = 22;            // vaciado por debajo del panel (aire para la cúpula del joystick)
joy_rail_w    = 2.0;
joy_bite      = 1.2;
joy_drop      = joy_shaft_top - panel_th - joy_shaft_out; // cara inf. del panel -> cara sup. PCB
// capuchón impreso (reemplaza al de fábrica)
joy_shaft_d   = 6.0;           // MEDIR: diámetro de la punta del eje
joy_shaft_clr = 0.25;
joy_cap_d     = 12;
joy_cap_h     = 7;

// ---------------------------------------------------------------- Placa principal (piso)
perf      = [50, 70];   // perfboard 5x7 cm (X, Y)
perf_th   = 1.6;
perf_hole = 2.5;        // MEDIR: centro de los agujeros de esquina desde cada borde
perf_so_h = 4;          // parantes
perf_pos  = [12, D/2 - wall - 1.0 - perf[1]/2];
hdr_h     = 8.5;        // tira de headers hembra 2.54 mm
dk        = [27, 63];   // DevKit real: 27 x 50 mm + antena = 63 mm (calibre, usuario)
dk_th     = 1.6;
dk_comp   = 3.3;        // altura del módulo WROOM sobre la PCB
dk_x      = 1;          // centro X de la DevKit (USB-C hacia la pared trasera)
dk_usb_overhang = 1.0;  // cuánto sobresalen los USB-C del borde de la PCB
dk_usb_slot = [26, 8];  // ranura para los 2 USB-C (deja pasar las fichas)

// ---------------------------------------------------------------- Energía
bat      = [30, 40, 8];     // LiPo 803040 (~1000 mAh). Cambiá por la tuya.
bat_pos  = [-45, -18];      // DEBAJO del joystick (el joystick queda bien alto)
bat_wall_h = 4;
tp       = [17, 28];        // módulo TP4056 USB-C (micro-USB: 17 x 26)
tp_pos   = [50, D/2 - wall - 0.5 - tp[1]/2];  // USB contra la pared trasera
tp_usb   = [11, 6];
bb       = [18, 24];        // buck-boost 3.3 V (genérico). MEDIR el tuyo.
bb_pos   = [-45, 22];       // atrás a la izquierda, debajo del OLED
mod_z    = 2.0;             // altura de la ranura de las guías sobre el piso

// ---------------------------------------------------------------- Pared derecha
jack_y = -6;   jack_d = 6.2;   // jack 3.5 mm de panel (tipo PJ-392, rosca M6)
pwr_y  = -24;  pwr_d  = 6.2;   // mini-toggle MTS-101/102 (rosca M6). MEDIR
side_z = 16;                   // altura de ambos sobre el piso interior

// ---------------------------------------------------------------- Marca
brand_txt = "TOLHUIN CHORD";
brand_sz  = 4.5;
url_txt   = "estuariolabs.com.ar";
engrave   = 0.6;

// ---------------------------------------------------------------- Teclas (keycaps)
label_font  = "Liberation Sans:style=Bold";
label_h     = 0.8;
cap_mx      = [18.2, 15.6, 7.5]; // base, tope, alto
cap_mx_wall = 1.2;
cap_mx_roof = 1.6;
mx_cross    = [4.15, 1.35];      // cruz del vástago MX (+ tolerancia de impresión)
mx_cross_d  = 4.0;
cap_choc    = [17.5, 16.5, 3.2]; // ancho, prof, alto
choc_prong  = [1.25, 3.0, 3.0];  // patitas Choc v1 (X, Y, largo)
choc_prong_dx = 5.7;

// ---------------------------------------------------------------- Derivados
PZ  = floor_th + int_h;          // z de la cara inferior del panel
PT  = PZ + panel_th;             // z de la cara superior del panel
IW2 = W/2 - wall;  ID2 = D/2 - wall;
posts = [for (sx = [-1, 1]) for (sy = [-1, 1]) [sx*(W/2 - post_inset), sy*(D/2 - post_inset)]];
dk_pos = [dk_x, ID2 - dk_usb_overhang - 0.5 - dk[1]/2];
// [x, y, leyenda, grupo (0 acorde / 1 función), grado 0..6 (-1 = función)]
keys = concat(
    [for (i = [0:3]) [lo_x[i], lo_y, lo_lbl[i], 0, i]],
    [for (i = [0:2]) [hi_x[i], hi_y, hi_lbl[i], 0, 4 + i]],
    [for (i = [0:3]) [fn_x[i], fn_y, fn_lbl[i], 1, -1]]);
// función armónica de cada grado: 0 = T (I iii vi), 1 = S (ii IV), 2 = D (V vii°)
deg_fn = [0, 1, 0, 1, 2, 0, 2];

// ---------------------------------------------------------------- Paletas
// [base, panel, [acorde T, S, D], función, joystick, leyenda acordes, leyenda función]
// Colores elegidos para que existan como filamento PLA/PETG común.
themes = [
  ["bosque",  "#2F4A3A", "#ECE6D6", ["#D8DFC9", "#A9BF9F", "#6F8F6A"], "#C8642E", "#2F4A3A", "#22332A", "#FFF4E8"],
  ["glaciar", "#E4EAED", "#F7F8F8", ["#CFE0EA", "#94BCD2", "#4F86A8"], "#24506E", "#2B2F33", "#1F3340", "#F2F7FA"],
  ["noche",   "#2A2C30", "#1C1D20", ["#E3E5E8", "#B5B9C0", "#868C96"], "#F0B23C", "#F0B23C", "#1C1D20", "#1C1D20"],
  ["lenga",   "#7A5A3E", "#EFE7D8", ["#EADCC2", "#D4B48A", "#A97E52"], "#B23A2A", "#3A2A1E", "#3A2A1E", "#FBEDE6"],
  ["mar",     "#1F4E5A", "#F2EFE8", ["#F4E3C3", "#E9C98F", "#C99A5B"], "#E07A5F", "#1F4E5A", "#1F4E5A", "#FFF6F1"],
  ["clasico", "#F2F2F2", "#F2F2F2", ["#C9C9C9", "#C9C9C9", "#C9C9C9"], "#FF8C42", "#262628", "#3A3A3A", "#3A3A3A"]];
TH = themes[max(0, search([theme], themes)[0])];
function chord_col(deg) = chord_color == "funcion" ? TH[3][deg_fn[deg]] : TH[3][0];
joy_pcb_c = [joy_pos[0], joy_pos[1] + joy_stick_dy];

// ---------------------------------------------------------------- Chequeos de choque
dk_top    = floor_th + perf_so_h + perf_th + hdr_h + dk_th + dk_comp;
sw_bottom = PT - sw_below;
joy_bot   = PZ - joy_drop - joy_pcb_th - joy_under;
bat_top   = floor_th + bat[2];
echo(str("Medidas exteriores: ", W, " x ", D, " x ", PT, " mm (sin teclas)"));
echo(str("Aire DevKit <-> pines de los switches: ", sw_bottom - dk_top, " mm"));
echo(str("Aire batería <-> joystick: ", joy_bot - bat_top, " mm"));
assert(sw_bottom - dk_top >= 2.5, "La DevKit choca con los pines de los switches: subí int_h");
assert(joy_bot - bat_top >= 1.5, "El joystick choca con la batería: bajá bat[2] o subí int_h");
for (k = keys) for (p = posts)
    assert(max(abs(k[0]-p[0]), abs(k[1]-p[1])) - sw_pocket/2 - post_d/2 >= 1.0,
           str("La tecla ", k[2], " choca con una columna de tornillo"));
assert(abs(oled_pos[0]) + oled_pcb[0]/2 + 2 < IW2, "OLED fuera del interior");

// =====================================================================================
//  HELPERS
// =====================================================================================
module rsq(w, h, r) {
    r2 = max(0.01, min(r, w/2 - 0.01, h/2 - 0.01));
    hull() for (sx = [-1, 1]) for (sy = [-1, 1])
        translate([sx*(w/2 - r2), sy*(h/2 - r2)]) circle(r = r2);
}
function label_size(w, s) = min(w*0.42, w*0.62 / max(1, len(s)*0.68));

// =====================================================================================
//  PANEL SUPERIOR
// =====================================================================================
module panel_slab() {
    translate([0, 0, PZ]) hull() {
        linear_extrude(0.01) rsq(W, D, outer_r);
        translate([0, 0, panel_th - top_cham]) linear_extrude(0.01) rsq(W, D, outer_r);
        translate([0, 0, panel_th - 0.01]) linear_extrude(0.01) rsq(W - 2*top_cham, D - 2*top_cham, outer_r - top_cham);
    }
}
module panel_lip() {
    o = [W - 2*wall - 2*fit, D - 2*wall - 2*fit];
    translate([0, 0, PZ - lip_h]) linear_extrude(lip_h + 0.01) difference() {
        rsq(o[0], o[1], outer_r - wall - fit);
        rsq(o[0] - 2*lip_th, o[1] - 2*lip_th, outer_r - wall - fit - lip_th);
    }
}
module panel_posts() {
    z0 = floor_th + foot_h + 0.2;
    for (p = posts) translate([p[0], p[1], z0]) cylinder(d = post_d, h = PZ - z0 + 0.01);
}
module panel_post_pilots() {
    z0 = floor_th + foot_h + 0.2;
    for (p = posts) translate([p[0], p[1], z0 - 0.1]) cylinder(d = post_pilot, h = pilot_depth);
}
module key_cutouts() {
    for (k = keys) translate([k[0], k[1], 0]) {
        translate([0, 0, PZ - 1]) linear_extrude(panel_th + 2) square(sw_cut, center = true);
        translate([0, 0, PZ - 1]) linear_extrude(1 + panel_th - sw_plate) square(sw_pocket, center = true);
    }
}
// Rieles con ranura que cuelgan del panel. La PCB entra deslizando por el extremo
// abierto (-dir) y frena contra el extremo cerrado. Un resalte de 0.35 mm la traba.
//  c = centro, pcb = [a lo largo del riel, entre rieles], z_top = cara superior PCB
module slide_rails(c, along, across, pcb_th, z_top, rail_w, bite, axis) {
    L   = along + 2*fit + 1.5;
    zg0 = z_top - pcb_th - fit;        // piso de la ranura
    zb  = zg0 - 1.2;                   // fondo del riel (labio inferior)
    e   = across/2 + fit;              // borde de la PCB (+holgura)
    rot = axis == "x" ? 0 : 90;
    translate([c[0], c[1], 0]) rotate([0, 0, rot]) {
        for (s = [-1, 1]) difference() {
            // riel completo (desde el panel hacia abajo)
            translate([-along/2 - fit, s > 0 ? e - bite : -e - rail_w, zb])
                cube([L, bite + rail_w, PZ - zb + 0.01]);
            // ranura (abierta en -X, cerrada en +X)
            translate([-along/2 - fit - 1, s > 0 ? e - bite - 0.1 : -e - 0.01, zg0])
                cube([along + 2*fit + 1, bite + 0.11, pcb_th + fit]);
            // vaciar lo que queda por encima de la ranura del lado interior
            // (deja libre la cara superior de la PCB salvo la franja `bite`)
        }
        // resalte de traba a la entrada
        for (s = [-1, 1])
            translate([-along/2 + 0.5, s > 0 ? e - bite : -e, zg0 - 0.01])
                cube([1.0, bite, 0.35]);
    }
}
module oled_window() {
    c = [oled_pos[0] + oled_win_off[0], oled_pos[1] + oled_win_off[1]];
    translate([c[0], c[1], PZ - 1]) linear_extrude(panel_th + 2) square(oled_win, center = true);
    // bisel superior para ver la pantalla en ángulo
    translate([c[0], c[1], 0]) hull() {
        translate([0, 0, PT - 1.2]) linear_extrude(0.01) square(oled_win, center = true);
        translate([0, 0, PT + 0.01]) linear_extrude(0.01) square([oled_win[0] + 2.4, oled_win[1] + 2.4], center = true);
    }
}
module joy_hole() {
    translate([joy_pos[0], joy_pos[1], PZ - 1]) cylinder(d = joy_pocket_d, h = 1 + panel_th - joy_skin);
    translate([joy_pos[0], joy_pos[1], PZ - 1]) cylinder(d = joy_hole_d, h = panel_th + 2);
    translate([joy_pos[0], joy_pos[1], PT - 1.2]) cylinder(d1 = joy_hole_d, d2 = joy_hole_d + 2.4, h = 1.21);
}
// Grabados decorativos (0.5 mm, ancho de 2 pasadas de boquilla 0.4)
deco_w = 0.8;  deco_d = 0.5;
module deco_engrave() if (deco) {
    c = [oled_pos[0] + oled_win_off[0], oled_pos[1] + oled_win_off[1]];
    translate([0, 0, PT - deco_d]) linear_extrude(deco_d + 1) {
        // marco del OLED
        translate(c) difference() {
            rsq(oled_win[0] + 9, oled_win[1] + 9, 2.5);
            rsq(oled_win[0] + 9 - 2*deco_w, oled_win[1] + 9 - 2*deco_w, 2.5 - deco_w);
        }
        // aro del joystick
        translate(joy_pos) difference() {
            circle(d = joy_hole_d + 9);
            circle(d = joy_hole_d + 9 - 2*deco_w);
        }
        // línea entre la fila de función y los acordes
        translate([(fn_x[0] + fn_x[3])/2, (fn_y + hi_y)/2]) square([fn_x[3] - fn_x[0] + 18, deco_w], center = true);
    }
}
module brand_engrave() {
    translate([(fn_x[0] + fn_x[3])/2, (fn_y + 9.5 + D/2)/2, PT - engrave])
        linear_extrude(engrave + 1)
            text(brand_txt, size = brand_sz, halign = "center", valign = "center", font = label_font);
}
module top_panel() {
    difference() {
        union() {
            panel_slab();
            panel_lip();
            panel_posts();
            // OLED: rieles en Y, la placa entra por el lado izquierdo (-X)
            slide_rails(oled_pos, oled_pcb[0], oled_pcb[1], oled_pcb_th,
                        PZ - oled_glass_th - 0.2, oled_rail_w, oled_bite, "x");
            // Joystick: rieles en X, la placa entra por el frente (-Y), header al fondo
            slide_rails(joy_pcb_c, joy_pcb[1], joy_pcb[0], joy_pcb_th,
                        PZ - joy_drop, joy_rail_w, joy_bite, "y");
        }
        key_cutouts();
        oled_window();
        joy_hole();
        panel_post_pilots();
        brand_engrave();
        deco_engrave();
    }
}

// =====================================================================================
//  BASE (BANDEJA)
// =====================================================================================
module tray_outer() {
    hull() {
        linear_extrude(0.01) rsq(W - 2*bot_cham, D - 2*bot_cham, outer_r - bot_cham);
        translate([0, 0, bot_cham]) linear_extrude(0.01) rsq(W, D, outer_r);
    }
    translate([0, 0, bot_cham]) linear_extrude(PZ - bot_cham) rsq(W, D, outer_r);
}
module standoff(h, d, pilot) difference() {
    cylinder(d = d, h = h);
    translate([0, 0, 1]) cylinder(d = pilot, h = h);
}
// Guía para módulos chicos: 2 rieles con ranura; la placa entra deslizando en Y
module module_guide(c, sz) {
    e = sz[0]/2 + fit;
    translate([c[0], c[1], floor_th]) for (s = [-1, 1]) difference() {
        translate([s > 0 ? e - 1.0 : -e - 1.8, -sz[1]/2, 0]) cube([2.8, sz[1], mod_z + 1.6 + fit + 1.2]);
        translate([s > 0 ? e - 1.1 : -e - 0.01, -sz[1]/2 - 1, mod_z]) cube([1.11, sz[1] + 2, 1.6 + fit]);
    }
}
module battery_bay() {
    o = [bat[0] + 2*fit + 3.2, bat[1] + 2*fit + 3.2];
    translate([bat_pos[0], bat_pos[1], floor_th]) linear_extrude(bat_wall_h) difference() {
        rsq(o[0], o[1], 2);
        rsq(bat[0] + 2*fit, bat[1] + 2*fit, 1);
        // cortes en las paredes para sacar los cables y pasar una cinta/velcro
        square([o[0] + 1, 12], center = true);
        square([10, o[1] + 1], center = true);
    }
}
module rear_slot(x, z, sz) {
    translate([x, D/2 - wall/2, z]) rotate([90, 0, 0])
        linear_extrude(wall + 2, center = true) rsq(sz[0], sz[1], min(2, sz[1]/2));
}
module right_hole(y, z, d) {
    translate([W/2 - wall/2, y, z]) rotate([0, 90, 0]) cylinder(d = d, h = wall + 2, center = true);
}
module bottom_tray() {
    usb_z = floor_th + perf_so_h + perf_th + hdr_h + dk_th + 1.6;
    tp_z  = floor_th + mod_z + 1.6 + 1.6;
    difference() {
        union() {
            difference() {
                tray_outer();
                translate([0, 0, floor_th]) linear_extrude(int_h + 1)
                    rsq(W - 2*wall, D - 2*wall, outer_r - wall);
            }
            for (p = posts) translate([p[0], p[1], 0]) cylinder(d = post_d + 1, h = floor_th + foot_h);
            for (sx = [-1, 1]) for (sy = [-1, 1])
                translate([perf_pos[0] + sx*(perf[0]/2 - perf_hole), perf_pos[1] + sy*(perf[1]/2 - perf_hole), floor_th - 0.01])
                    standoff(perf_so_h + 0.01, 5, 1.8);
            module_guide(tp_pos, tp);
            module_guide(bb_pos, bb);
            battery_bay();
        }
        // tornillos M3 desde abajo
        for (p = posts) translate([p[0], p[1], 0]) {
            translate([0, 0, -1]) cylinder(d = screw_d, h = floor_th + foot_h + 2);
            translate([0, 0, -1]) cylinder(d = screw_head, h = screw_head_h + 1);
        }
        rear_slot(dk_pos[0], usb_z, dk_usb_slot);
        rear_slot(tp_pos[0], tp_z, tp_usb);
        right_hole(jack_y, floor_th + side_z, jack_d);
        right_hole(pwr_y,  floor_th + side_z, pwr_d);
        // alojamientos para patitas de goma (10 mm)
        for (sx = [-1, 1]) for (sy = [-1, 1])
            translate([sx*(W/2 - 16), sy*(D/2 - 14), -0.01]) cylinder(d = 10.5, h = 0.8);
        // URL grabada abajo (espejada para que se lea al dar vuelta el equipo)
        translate([0, 0, -0.01]) mirror([1, 0, 0]) linear_extrude(engrave + 0.01)
            text(url_txt, size = 5, halign = "center", valign = "center", font = label_font);
    }
}

// =====================================================================================
//  TECLAS (keycaps) y capuchón del joystick
// =====================================================================================
module keycap(lbl) { keycap_body(lbl); keycap_legend(lbl); }
// Leyenda en relieve (MX). Para que contraste: cambio de filamento en z = cap_mx[2]
// (pausa M600 en el slicer a esa altura) -> la leyenda sale en otro color.
module keycap_legend(lbl) if (btn == "mx")
    translate([0, 0, cap_mx[2] - 0.01]) linear_extrude(label_h)
        text(lbl, size = label_size(cap_mx[1], lbl), halign = "center", valign = "center", font = label_font);
module keycap_body(lbl) {
    if (btn == "mx") {
        b = cap_mx[0];  t = cap_mx[1];  h = cap_mx[2];
        difference() {
            hull() {
                linear_extrude(0.01) rsq(b, b, 1.5);
                translate([0, 0, h - 0.01]) linear_extrude(0.01) rsq(t, t, 1.5);
            }
            hull() {
                translate([0, 0, -0.01]) linear_extrude(0.01) rsq(b - 2*cap_mx_wall, b - 2*cap_mx_wall, 1);
                translate([0, 0, h - cap_mx_roof]) linear_extrude(0.01) rsq(t - 2*cap_mx_wall, t - 2*cap_mx_wall, 1);
            }
        }
        // vástago con cruz MX
        difference() {
            cylinder(d = 5.6, h = h - cap_mx_roof + 0.01);
            translate([0, 0, -0.01]) linear_extrude(mx_cross_d) {
                square([mx_cross[0], mx_cross[1]], center = true);
                square([mx_cross[1], mx_cross[0]], center = true);
            }
        }
    } else {
        // Choc: se imprime con el TOPE contra la cama; la leyenda va grabada.
        w = cap_choc[0];  d = cap_choc[1];  h = cap_choc[2];
        difference() {
            linear_extrude(h) rsq(w, d, 1.5);
            translate([0, 0, h - 0.6]) linear_extrude(1) mirror([1, 0, 0])
                text(lbl, size = label_size(w - 2, lbl), halign = "center", valign = "center", font = label_font);
        }
        for (s = [-1, 1]) translate([s*choc_prong_dx/2, 0, -choc_prong[2]/2 + 0.01])
            cube(choc_prong, center = true);
    }
}
// group: 0 acordes, 1 función; fsel: -1 todos, 0/1/2 = solo los grados T/S/D
module caps_plate(group, fsel = -1) {
    sel = [for (k = keys) if (k[3] == group && (fsel < 0 || deg_fn[k[4]] == fsel)) k];
    for (i = [0:len(sel) - 1])
        translate([(i % 4)*22, -floor(i/4)*22, btn == "mx" ? 0 : cap_choc[2]])
            rotate([btn == "mx" ? 0 : 180, 0, 0]) keycap(sel[i][2]);
}
module joystick_cap() difference() {
    union() {
        cylinder(d = joy_cap_d, h = joy_cap_h);
        translate([0, 0, joy_cap_h]) scale([1, 1, 2/(joy_cap_d/2)]) sphere(d = joy_cap_d);
    }
    translate([0, 0, -0.1]) cylinder(d = joy_shaft_d + 2*joy_shaft_clr, h = joy_cap_h - 1.5);
}

// =====================================================================================
//  COMPONENTES DE REFERENCIA (solo visual)
// =====================================================================================
module ghosts() {
    // perfboard + DevKit en headers
    color([0.2, 0.5, 0.25, 0.9]) translate([perf_pos[0], perf_pos[1], floor_th + perf_so_h + perf_th/2]) cube([perf[0], perf[1], perf_th], center = true);
    z_dk = floor_th + perf_so_h + perf_th + hdr_h;
    color([0.1, 0.1, 0.1]) for (s = [-1, 1]) translate([dk_pos[0] + s*(dk[0]/2 - 1.5), dk_pos[1], z_dk - hdr_h/2]) cube([2.5, dk[1] - 4, hdr_h], center = true);
    color([0.15, 0.15, 0.2]) translate([dk_pos[0], dk_pos[1], z_dk + dk_th/2]) cube([dk[0], dk[1], dk_th], center = true);
    color([0.75, 0.75, 0.78]) translate([dk_pos[0], dk_pos[1] - 12, z_dk + dk_th + dk_comp/2]) cube([18, 25.5, dk_comp], center = true);
    color([0.75, 0.75, 0.78]) for (s = [-1, 1]) translate([dk_pos[0] + s*6, ID2 - 3.5, z_dk + dk_th + 1.6]) cube([9, 7.5, 3.2], center = true);
    // PCM5102 + ADS1115 (sobre la perfboard, al lado de la DevKit)
    color([0.5, 0.2, 0.6]) translate([perf_pos[0] + 16, perf_pos[1] - 10, floor_th + perf_so_h + perf_th + 6]) cube([16, 32, 1.6], center = true);
    color([0.2, 0.3, 0.7]) translate([perf_pos[0] + 16, perf_pos[1] - 32, floor_th + perf_so_h + perf_th + 6]) cube([16, 10, 1.6], center = true);
    // batería, TP4056, buck-boost
    color([0.3, 0.45, 0.8]) translate([bat_pos[0], bat_pos[1], floor_th + bat[2]/2]) cube(bat, center = true);
    color([0.1, 0.35, 0.6]) translate([tp_pos[0], tp_pos[1], floor_th + mod_z + 0.8]) cube([tp[0], tp[1], 1.6], center = true);
    color([0.1, 0.35, 0.6]) translate([bb_pos[0], bb_pos[1], floor_th + mod_z + 0.8]) cube([bb[0], bb[1], 1.6], center = true);
}
module ghost_switches() {   // todo lo que cuelga del panel
    // OLED y joystick
    color([0.1, 0.2, 0.5]) translate([oled_pos[0], oled_pos[1], PZ - oled_glass_th - 0.2 - oled_pcb_th/2]) cube([oled_pcb[0], oled_pcb[1], oled_pcb_th], center = true);
    color([0.05, 0.05, 0.05]) translate([oled_pos[0], oled_pos[1], PZ - 0.2 - oled_glass_th/2]) cube([oled_pcb[0], 19, oled_glass_th], center = true);
    color([0.1, 0.2, 0.5]) translate([joy_pcb_c[0], joy_pcb_c[1], PZ - joy_drop - joy_pcb_th/2]) cube([joy_pcb[0], joy_pcb[1], joy_pcb_th], center = true);
    color([0.2, 0.2, 0.2]) translate([joy_pos[0], joy_pos[1], PZ - joy_drop]) cylinder(d = 17, h = 11);
    color([0.6, 0.6, 0.6]) translate([joy_pos[0], joy_pos[1], PZ - joy_drop]) cylinder(d = joy_shaft_d, h = joy_shaft_top);
    for (k = keys) {
        color([0.25, 0.25, 0.25]) translate([k[0], k[1], PT - 5]) linear_extrude(5) square(btn == "mx" ? 13.9 : 13.7, center = true);
        color([0.85, 0.3, 0.2]) translate([k[0], k[1], PT]) linear_extrude(btn == "mx" ? 5.6 : 2.6) square(btn == "mx" ? 15 : 14.5, center = true);
    }
}
module ghost_jacks() {
    color([0.7, 0.7, 0.7]) for (y = [jack_y, pwr_y]) translate([IW2 - 12, y, floor_th + side_z]) rotate([0, 90, 0]) cylinder(d = 8, h = 12);
}
module placed_caps() {
    z = PT + (btn == "mx" ? 6.4 : 3.0);
    for (k = keys) translate([k[0], k[1], z]) {
        color(k[3] == 1 ? TH[4] : chord_col(k[4])) keycap_body(k[2]);
        color(k[3] == 1 ? TH[7] : TH[6]) keycap_legend(k[2]);
    }
}

// =====================================================================================
//  SELECCIÓN DE PIEZA
// =====================================================================================
col_base = TH[1];  col_panel = TH[2];
if (part == "top")          // listo para imprimir: cara superior contra la cama
    rotate([180, 0, 0]) translate([0, 0, -PT]) top_panel();
else if (part == "bottom")     bottom_tray();
else if (part == "caps_chord") caps_plate(0);
else if (part == "caps_fn")    caps_plate(1);
else if (part == "caps_T")     caps_plate(0, 0);   // I, iii, vi
else if (part == "caps_S")     caps_plate(0, 1);   // ii, IV
else if (part == "caps_D")     caps_plate(0, 2);   // V, vii°
else if (part == "cap_joy")    joystick_cap();
else if (part == "test_keys") {
    // recorte de prueba (1 hora de impresión): 2 agujeros de switch + espesores reales
    intersection() {
        rotate([180, 0, 0]) translate([0, 0, -PT]) top_panel();
        translate([lo_x[2] + pitch/2, -lo_y, 0]) cube([2*pitch + 6, pitch + 6, 20], center = true);
    }
}
else if (part == "exploded") {
    color(col_base) bottom_tray();
    if (show_ghosts) { ghosts(); ghost_jacks(); }
    translate([0, 0, 40]) {
        color(col_panel, 0.85) top_panel();
        if (show_ghosts) ghost_switches();
        placed_caps();
    }
}
else {
    color(col_base) bottom_tray();
    color(col_panel) top_panel();
    if (show_ghosts) { ghosts(); ghost_jacks(); ghost_switches(); }
    placed_caps();
    translate([joy_pos[0], joy_pos[1], PZ - joy_drop + joy_shaft_top - (joy_cap_h - 1.5)])
        color(TH[5]) joystick_cap();
}
