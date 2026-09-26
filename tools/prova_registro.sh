#!/bin/bash
# =============================================================================
# tools/prova_registro.sh — il registro di sistema della scrivania (@EXWIN-LOG)
#
# Si accende la grafica, si apre il menu Avvio con un clic su «Avvio», si
# sceglie «Registro di sistema», e si guarda:
#   - che si sia aperta una finestra (la barra del titolo attiva);
#   - che dentro ci sia del TESTO: le righe di wserver che dal 22 settembre
#     2026 restano nelle celle della console della grafica, e che adesso
#     arrivano con SYS_CONSOLE_TESTO.
#
# ! LA VOCE DEL MENU SI CLICCA A UN PUNTO CONTATO, ed e' il punto debole di
# questa prova: il menu e' alto quanto le applicazioni in applicazioni.txt.
# Con quelle del CD di oggi (quattro voci in cima) «Registro di sistema» sta
# in (113, 481) a 800x600. Se il menu cambia, la prima fotografia
# ($D/menu.ppm) dice dove sta adesso.
#
# ! IL PUNTATORE A PASSI DI DIECI: vedi tools/prova_riduci.sh.
#
#     tools/prova_registro.sh [directory-di-lavoro]    (default /tmp/exos-registro)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), in grafica 800x600.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-registro}"
mkdir -p "$D"
rm -f "$D"/*.ppm
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

A=("exwin@12" "key:alt-f5@3")
for i in 1 2 3 4 5; do A+=("mon:mouse_move -600 -600@0"); done
# (0,0) -> (37,586): «Avvio»
for i in $(seq 1 58); do A+=("mon:mouse_move 0 10@0"); done
A+=("mon:mouse_move 0 6@0")
for i in 1 2 3; do A+=("mon:mouse_move 10 0@0"); done
A+=("mon:mouse_move 7 0@0" "mon:mouse_button 1@0" "mon:mouse_button 0@2"
    "foto:$D/menu.ppm@1")
# (37,586) -> (113,481): «Registro di sistema»
for i in $(seq 1 7); do A+=("mon:mouse_move 10 -10@0"); done
for i in 1 2 3; do A+=("mon:mouse_move 0 -10@0"); done
A+=("mon:mouse_move 6 -5@0" "mon:mouse_button 1@0" "mon:mouse_button 0@4"
    "foto:$D/registro.ppm@2")

EXOS_ISTANZA=registro EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/registro.log" 2>&1

[ -s "$D/registro.ppm" ] || { echo "NON RIUSCITO: nessuna fotografia (vedi $D/registro.log)"; exit 1; }

python3 - "$D/registro.ppm" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
xs, ys = [], []
for y in range(h):
    for x in range(w):
        i = (y * w + x) * 3
        if px[i] == 30 and px[i + 1] == 77 and px[i + 2] == 125:
            xs.append(x); ys.append(y)
if not xs:
    print("  [NO]  nessuna finestra si e' aperta"); sys.exit(1)
x0, x1, y1 = min(xs), max(xs), max(ys)
print("  [OK]  la finestra c'e': barra del titolo da x=%d a x=%d" % (x0, x1))
# Il testo: pixel scuri nelle prime righe dell'area, sotto la barra.
n = sum(1 for y in range(y1 + 6, y1 + 60) for x in range(x0 + 6, x1 - 6)
        if max(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) < 80)
print("        pixel di testo nelle prime righe: %d" % n)
if n > 200:
    print("  [OK]  dentro c'e' il registro (le righe di wserver)")
    sys.exit(0)
print("  [NO]  la finestra e' vuota"); sys.exit(1)
EOF
