#!/bin/bash
# =============================================================================
# tools/prova_exbrowser_misura.sh — EXBrowser si ridimensiona (@EXBROWSER-1024)
#
#   1. si apre EXBrowser e si fotografa: la presa nell'angolo deve esserci —
#      il server la disegna solo a chi ha chiesto EX_RIDIM;
#   2. la si tira in su e a sinistra di circa 120x80, e si rifotografa: la
#      barra del titolo dev'essere piu' corta di circa tanto, e il pulsante «?» (l'ultimo
#      a destra della barra dell'indirizzo) deve essersi spostato con il bordo
#      destro invece di restare fuori dalla finestra.
#
# ! IL PUNTATORE A PASSI DI DIECI e ExWin con Alt+F1 / Alt+F5: vedi
# tools/prova_riduci.sh.
#
#     tools/prova_exbrowser_misura.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), in grafica 800x600.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-exbrowser-misura}"
mkdir -p "$D"
rm -f "$D"/*.ppm

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
export EXOS_ISTANZA=exbmisura EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=128M

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do printf '%s\n' "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do printf '%s\n' "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do printf '%s\n' "mon:mouse_move 0 10@0"; done
    printf '%s\n' "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}

barra() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
xs, ys = [], []
for y in range(h):
    r = y * w
    for x in range(w):
        i = (r + x) * 3
        if px[i] == 30 and px[i + 1] == 77 and px[i + 2] == 125:
            xs.append(x); ys.append(y)
if xs: print(min(xs), min(ys), max(xs), max(ys))
EOF
}

# The right edge of the «?» button: the rightmost column, in the address
# row, that has the button's white highlight — searched inside the window.
destra_pulsanti() {
    python3 - "$1" "$2" "$3" "$4" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
x0, x1, y = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
best = -1
for yy in range(y, y + 22):
    for x in range(x0, min(x1, w)):
        i = (yy * w + x) * 3
        if px[i] == 255 and px[i + 1] == 255 and px[i + 2] == 255: best = max(best, x)
print(best)
EOF
}

esito=0
{
    echo "exwin@10"
    echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser &@10"
    echo "key:alt-f5@4"
    echo "foto:$D/1-prima.ppm@2"
} > "$D/args1.txt"
mapfile -t A < "$D/args1.txt"
timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

[ -s "$D/1-prima.ppm" ] || { echo "NON RIUSCITO: nessuna fotografia (vedi $D/1.log)"; exit 1; }
B1=$(barra "$D/1-prima.ppm")
[ -n "$B1" ] || { echo "NON RIUSCITO: nessuna finestra attiva sullo schermo"; exit 1; }
read -r X0 Y0 X1 Y1 <<< "$B1"

# ! LA PRESA SI CALCOLA, NON SI CERCA PER COLORE: il suo grigio (64,64,64)
# c'e' anche dentro le pagine, e misura_finestre.py --presa trova quello. La
# finestra nasce alta FIN_H = 540 sotto la barra del titolo; la presa e' il
# quadrato di 14 pixel nell'angolo in basso a destra dell'area del client.
PX=$(( X1 - 5 )); PY=$(( Y1 + 1 + 540 - 7 ))
PRESA="$PX $PY"
N_PRESA=$(python3 -c "
import sys
d = open('$D/1-prima.ppm', 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
print(sum(1 for y in range($PY - 7, $PY + 7) for x in range($PX - 7, $PX + 7)
          if tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) == (64, 64, 64)))")
echo "=== 1. aperto: barra del titolo ($B1), presa in ($PRESA) ==="
if [ "$N_PRESA" -ge 6 ]; then
    echo "  [OK]  la finestra ha la presa per ridimensionarsi"
else
    echo "  [NO]  nessuna presa li': la finestra non si ridimensiona"; exit 1
fi

{
    echo "exwin@10"
    echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser &@10"
    echo "key:alt-f5@4"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    passi_a $PRESA
    echo "mon:mouse_button 1@1"
    for i in $(seq 1 8);  do echo "mon:mouse_move -10 -10@0"; done
    for i in $(seq 1 4);  do echo "mon:mouse_move -10 0@0"; done
    echo "mon:mouse_button 0@4"
    echo "foto:$D/2-dopo.ppm@3"
} > "$D/args2.txt"
mapfile -t A < "$D/args2.txt"
timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/2.log" 2>&1

B2=$(barra "$D/2-dopo.ppm")
read -r Z0 W0 Z1 W1 <<< "$B2"
echo "=== 2. tirata di 120x80: barra del titolo ($B2) ==="
# ! NON ESATTAMENTE 120: l'angolo si aggancia al puntatore, non al passo
# contato. Conta che si sia stretta di circa tanto e che non si sia spostata.
if [ -n "$B2" ] && [ $(( X1 - Z1 )) -ge 100 ] && [ $(( X1 - Z1 )) -le 140 ] && [ "$Z0" = "$X0" ]; then
    echo "  [OK]  la finestra e' $(( X1 - Z1 )) pixel piu' stretta, e non si e' spostata"
else
    echo "  [NO]  la misura non torna (prima $B1, dopo ${B2:-niente})"; esito=1
fi

# The address row is 20 (menu) + 4 pixels below the client top, i.e. the
# title bar bottom + 1 + 24.
RIGA=$(( W1 + 1 + 24 ))
P1=$(destra_pulsanti "$D/1-prima.ppm" "$X0" "$(( X1 + 1 ))" "$RIGA")
P2=$(destra_pulsanti "$D/2-dopo.ppm" "$Z0" "$(( Z1 + 1 ))" "$RIGA")
echo "      il pulsante «?» finiva a x=$P1, adesso a x=$P2"
# Il pulsante si sposta ESATTAMENTE quanto il bordo destro.
if [ "$P2" -gt 0 ] && [ $(( P1 - P2 )) -eq $(( X1 - Z1 )) ]; then
    echo "  [OK]  i pulsanti di destra si sono spostati con il bordo"
else
    echo "  [NO]  i pulsanti di destra non hanno seguito il bordo"; esito=1
fi

echo ""
echo "  Le fotografie e i registri sono in $D"
exit $esito
