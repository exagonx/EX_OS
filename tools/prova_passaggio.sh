#!/bin/bash
# =============================================================================
# tools/prova_passaggio.sh — il puntatore che passa sopra una finestra
# (@EXWIN-PASSAGGIO, 30 settembre 2026: wserver 0.008, exwin.so 0.011,
# exbrowser 0.027)
#
# Una pagina con dei collegamenti in alto e niente in basso:
#   A. il puntatore fuori dalla pagina: fotografia;
#   B. sopra un collegamento: la barra di stato deve mostrare il suo
#      indirizzo, cioe' cambiare rispetto ad A;
#   C. dieci passi sopra LO STESSO collegamento: EXBrowser non deve chiedere
#      nessun aggiornamento in piu' (lo dice `exwin -conta` sulla seriale);
#   D. in basso, dove non ci sono collegamenti: la barra di stato torna
#      com'era in A.
#
# ! LA BARRA DI STATO SI GUARDA A STRISCE DI RIGHE, non per pixel: si
# confrontano le righe dell'ultima fascia della finestra, lontane dal
# puntatore, cosi' la freccia non entra mai nel confronto.
#
#     bash tools/prova_passaggio.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-passaggio}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialpassaggio.txt

{
    echo '<html><body>'
    for i in 1 2 3 4 5 6; do
        echo "<p><a href=\"http://esempio.it/passaggio-$i\">COLLEGAMENTO NUMERO $i, LUNGO APPOSTA PER IL MOUSE, LUNGO LUNGO LUNGO</a></p>"
    done
    echo '</body></html>'
} > "$D/passaggio.html"

export EXOS_ISTANZA=passaggio EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG" "$D"/*.ppm "$SER"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/passaggio.html passaggio.html" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "exwin -conta@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/passaggio.html &@15"; echo "key:alt-f5@4"
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    echo "foto:$D/a-fuori.ppm@2"
    # sulla prima riga di collegamenti: 150 a destra, 110 in giu' (a passi di
    # dieci: il mouse di QEMU e' relativo)
    for i in $(seq 1 15); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 11); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move 1 0@2"
    echo "foto:$D/b-sopra.ppm@1"
    # i segnaposto sulla seriale li scrive la shell: con la grafica davanti
    # i tasti andrebbero al navigatore
    echo "key:alt-f1@2"; echo "echo PASSAGGIO-INIZIO@1"; echo "key:alt-f5@3"
    for i in $(seq 1 10); do echo "mon:mouse_move 5 0@0"; done
    echo "key:alt-f1@3"; echo "echo PASSAGGIO-FINE@1"; echo "key:alt-f5@3"
    for i in $(seq 1 25); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move 1 0@2"
    echo "foto:$D/c-sotto.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

python3 - "$D" "$SER" <<'PY'
import sys
d, ser = sys.argv[1], sys.argv[2]
def leggi(f):
    b = open(f, 'rb').read()
    p = b.split(b'\n', 3)
    w, h = map(int, p[1].split())
    return w, h, p[3]
try:
    w, h, a = leggi(d + '/a-fuori.ppm'); _, _, b = leggi(d + '/b-sopra.ppm'); _, _, c = leggi(d + '/c-sotto.ppm')
except Exception as e:
    print("  [NO]  mancano le fotografie (%s)" % e); sys.exit(1)
# the bottom of the browser window: rows 440-560, far from the pointer in A
# and B (y ~ 110); in C the pointer is at y ~ 360, still above
def fascia(img, y0, y1):
    return img[y0 * w * 3 : y1 * w * 3]
ok = 0
if fascia(a, 440, 560) != fascia(b, 440, 560):
    print("  [OK]  sopra un collegamento la parte bassa della finestra cambia (la barra di stato)"); ok += 1
else:
    print("  [NO]  sopra un collegamento non cambia niente in basso")
if fascia(a, 440, 560) == fascia(c, 440, 560):
    print("  [OK]  lasciato il collegamento, la barra di stato torna com'era"); ok += 1
else:
    print("  [NO]  lasciato il collegamento la parte bassa e' diversa da prima")
righe = open(ser, 'rb').read().decode('latin-1').replace('\r', '').split('\n')
dentro, n = False, 0
for r in righe:
    if 'PASSAGGIO-INIZIO' in r: dentro = True
    elif 'PASSAGGIO-FINE' in r: dentro = False
    elif dentro and 'aggiorna finestra' in r and '168x20' not in r: n += 1
# ! AL PIU' UNO: tornando alla grafica con Alt+F5 un programma puo' essere
# chiamato a ridisegnarsi una volta. Senza EX_NON_RIDISEGNARE sarebbero dieci.
if n <= 1:
    print("  [OK]  dieci passi sopra lo stesso collegamento: %d aggiornamenti (al piu' 1)" % n); ok += 1
else:
    print("  [NO]  dieci passi sopra lo stesso collegamento: %d aggiornamenti" % n)
print("  Le fotografie sono in %s" % d)
sys.exit(0 if ok == 3 else 1)
PY
