#!/bin/bash
# =============================================================================
# tools/prova_menu_chiudi.sh — il menu del tasto destro si chiude da solo
# (30 settembre 2026: wserver 0.008 WIN_ST_COMPARSA, exwin.so 0.011)
#
#   1. tasto destro sul vuoto della scrivania: il menu c'e';
#   2. un clic col sinistro altrove: il menu non c'e' piu';
#   3. di nuovo il menu, poi Esc: non c'e' piu'.
#
# Il verdetto e' il colore di un punto dentro il menu: aperto e' il grigio del
# menu, chiuso e' di nuovo il blu della scrivania (204060).
#
#     bash tools/prova_menu_chiudi.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-menu}"
mkdir -p "$D"
rm -f "$D"/*.ppm
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
export EXOS_ISTANZA=menu EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}
casa() { for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done; }
va() { casa; passi_a "$1" "$2"; }

{
    echo "exwin@14"; echo "key:alt-f5@3"
    va 400 300; echo "mon:mouse_button 2;mouse_button 0@3"
    echo "foto:$D/1-aperto.ppm@1"
    va 150 450; echo "mon:mouse_button 1;mouse_button 0@3"
    echo "foto:$D/2-fuori.ppm@1"
    va 400 300; echo "mon:mouse_button 2;mouse_button 0@3"
    echo "foto:$D/3-aperto.ppm@1"
    echo "key:esc@3"
    echo "foto:$D/4-esc.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

python3 - "$D" <<'PY'
import sys
d = sys.argv[1]
def px(f, x, y):
    b = open(f, 'rb').read(); p = b.split(b'\n', 3); w, h = map(int, p[1].split())
    i = (y * w + x) * 3; r = p[3]
    return '%02X%02X%02X' % (r[i], r[i + 1], r[i + 2])
ok, X, Y = 0, 440, 312           # dentro il menu, lontano dal puntatore
try:
    a1, f2, a3, e4 = (px(d + '/%s.ppm' % n, X, Y) for n in ('1-aperto', '2-fuori', '3-aperto', '4-esc'))
except Exception as e:
    print("  [NO]  mancano le fotografie (%s)" % e); sys.exit(1)
if a1 != '204060' and a3 != '204060':
    print("  [OK]  il menu si apre (%s)" % a1); ok += 1
else:
    print("  [NO]  il menu non si e' visto (%s, %s)" % (a1, a3))
if f2 == '204060':
    print("  [OK]  un clic fuori lo chiude"); ok += 1
else:
    print("  [NO]  dopo un clic fuori il menu e' ancora li' (%s)" % f2)
if e4 == '204060':
    print("  [OK]  Esc lo chiude"); ok += 1
else:
    print("  [NO]  dopo Esc il menu e' ancora li' (%s)" % e4)
print("  Le fotografie sono in %s" % d)
sys.exit(0 if ok == 3 else 1)
PY
