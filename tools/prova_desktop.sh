#!/bin/bash
# =============================================================================
# tools/prova_desktop.sh — le icone della scrivania e le unita', nei pixel
#
# Prova @PM-DESKTOP e @PM-UNITA:
#
#   1. ExWin parte SENZA il disco: nella colonna di destra non c'e' nessuna
#      unita'. Si torna alla shell, si monta hd0p1 su /disk, si torna alla
#      grafica: entro un giro dell'orologio (DESK_GIRO, 2 s) l'icona «disk»
#      compare da sola, in alto a destra;
#   2. con HOME=/disk/casa la cartella $HOME/desktop si vede a sinistra:
#      «progetti» (cartella), «nota.txt» (file), «Editor» (un .lnk). Un
#      doppio clic su Editor apre l'editor; su nota.txt l'editor col file;
#      sull'unita' il file manager. Ogni volta si guarda che una finestra
#      attiva sia comparsa, e quanto e' larga: l'editor e il file manager non
#      hanno la stessa larghezza.
#
# ! IL DOPPIO CLIC VA IN UN `mon:` SOLO, come in prova_doppioclic.sh: in
# quattro argomenti i clic arriverebbero troppo distanti per essere un doppio.
#
# ! IL PUNTATORE SI PILOTA A PASSI DI DIECI, come in prova_riduci.sh.
#
#     tools/prova_desktop.sh [directory-di-lavoro]     (default /tmp/exos-desktop)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos) in grafica 800x600,
# sfdisk e debugfs (e2fsprogs).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-desktop}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=desktop EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

# --- il disco: una partizione ext2 con casa/desktop dentro --------------------
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
printf 'una nota\n' > "$D/nota.txt"
printf '# collegamento\npercorso = /exwin/bin/edit\nnome = Editor\n' > "$D/editor.lnk"
for c in "mkdir casa" "mkdir casa/desktop" "mkdir casa/desktop/progetti" \
         "write $D/nota.txt casa/desktop/nota.txt" \
         "write $D/editor.lnk casa/desktop/editor.lnk"; do
    "$DEBUGFS" -w -R "$c" "$OFF" > /dev/null 2>&1
done

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

# Quanti pixel bianchi (le didascalie) ci sono nel rettangolo x0 y0 x1 y1.
bianchi() {
    python3 - "$@" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
x0, y0, x1, y1 = map(int, sys.argv[2:6])
n = 0
for y in range(y0, y1):
    for x in range(x0, x1):
        i = (y * w + x) * 3
        if px[i] == 255 and px[i + 1] == 255 and px[i + 2] == 255: n += 1
print(n)
EOF
}

# La barra del titolo attiva: la sua larghezza, o 0.
barra() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
xs = []
for y in range(h):
    for x in range(w):
        i = (y * w + x) * 3
        if px[i] == 30 and px[i + 1] == 77 and px[i + 2] == 125: xs.append(x)
print(max(xs) - min(xs) + 1 if xs else 0)
EOF
}

esito=0

# --- 1. un'unita' montata a sistema acceso ----------------------------------
timeout 300 python3 tools/qemu_drive.py \
    "exwin@12" "key:alt-f5@3" "foto:$D/1-prima.ppm@1" \
    "key:alt-f1@2" "mount hd0p1 /disk@4" "key:alt-f5@5" \
    "foto:$D/1-dopo.ppm@1" > "$D/1.log" 2>&1

echo "=== 1. si monta /disk con ExWin gia' acceso ==="
P=$(bianchi "$D/1-prima.ppm" 700 40 800 70 2>/dev/null || echo -1)
Q=$(bianchi "$D/1-dopo.ppm"  700 40 800 70 2>/dev/null || echo -1)
if [ "$P" = 0 ]; then echo "  [OK]  prima: nessuna unita' a destra"
else echo "  [NO]  prima c'era gia' qualcosa a destra ($P pixel)"; esito=1; fi
if [ "$Q" -gt 20 ]; then echo "  [OK]  dopo: l'icona dell'unita' e' comparsa da sola"
else echo "  [NO]  dopo il mount nessuna icona (vedi $D/1-dopo.ppm)"; esito=1; fi

# --- 2. i doppi clic -----------------------------------------------------------
# Le celle sono di 76 pixel: le voci a sinistra da (8,8), l'unita' a destra
# in 800-84. Si clicca al centro dell'icona.
giro() {    # nome x y
    {
        echo "mount hd0p1 /disk@6"; echo "export HOME=/disk/casa@1"
        echo "exwin@12"; echo "key:alt-f5@3"
        casa; passi_a "$2" "$3"
        echo "mon:mouse_button 1;mouse_button 0;mouse_button 1;mouse_button 0@8"
        echo "foto:$D/2-$1.ppm@2"
    } > "$D/2-$1.args"
    mapfile -t A < "$D/2-$1.args"
    timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/2-$1.log" 2>&1
}
giro editor 46 190
giro nota   46 114
giro disco  754 38

echo "=== 2. la cartella del profilo sulla scrivania ==="
S=$(bianchi "$D/2-editor.ppm" 0 0 1 1 2>/dev/null)   # la foto c'e'?
[ -n "$S" ] || { echo "  [NO]  nessuna fotografia (vedi $D/2-editor.log)"; exit 1; }
LE=$(barra "$D/2-editor.ppm"); LN=$(barra "$D/2-nota.ppm"); LD=$(barra "$D/2-disco.ppm")
if [ "$LE" -gt 0 ]; then echo "  [OK]  Editor.lnk: si e' aperta una finestra larga $LE"
else echo "  [NO]  il collegamento non ha aperto niente"; esito=1; fi
if [ "$LN" -gt 0 ] && [ "$LN" = "$LE" ]; then echo "  [OK]  nota.txt: si e' aperto l'editor"
else echo "  [NO]  nota.txt: finestra larga $LN, l'editor e' $LE"; esito=1; fi
if [ "$LD" -gt 0 ] && [ "$LD" != "$LE" ]; then echo "  [OK]  l'unita': si e' aperto il file manager (largo $LD)"
else echo "  [NO]  l'unita': finestra larga $LD"; esito=1; fi

echo ""
echo "  Le fotografie e i registri sono in $D"
exit $esito
