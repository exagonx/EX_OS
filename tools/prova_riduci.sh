#!/bin/bash
# =============================================================================
# tools/prova_riduci.sh — ridurre a icona, e la barra dei programmi aperti
#
# Prova @FIN-ICONA, nei pixel del framebuffer:
#
#   1. si apre /exwin/bin/term: sulla barra in basso compare la sua VOCE;
#   2. un clic sul pulsante «_» della barra del titolo: la finestra SPARISCE
#      (nessuna barra del titolo attiva sullo schermo) e la voce resta;
#   3. un clic sulla voce: la finestra TORNA, nello STESSO punto e della STESSA
#      larghezza di prima.
#
# ! IL PUNTATORE SI PILOTA A PASSI DI DIECI, come in prova_ridimensiona.sh
# (vedi li' il perche'): prima si sbatte nell'angolo, poi si conta.
#
# ! ExWin PRENDE LO SCHERMO DA SOLO: Alt+F1 torna alla shell per lanciare il
# programma, Alt+F5 torna a guardare la grafica (SVILUPPO.md, «Provare»).
#
# ! DOVE STA LA FINESTRA LO DICE LA FOTOGRAFIA: la barra del titolo attiva ha
# un colore che non c'e' da nessun'altra parte (C_BARRA_ATT, in wserver.c).
# Il giro e' quindi in due tempi: prima si apre e si fotografa, poi si chiede
# alla fotografia dove cliccare.
#
#     tools/prova_riduci.sh [directory-di-lavoro]      (default /tmp/exos-riduci)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), in grafica 800x600.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-riduci}"
mkdir -p "$D"
rm -f "$D"/*.ppm

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

export EXOS_ISTANZA=riduci EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do printf '%s\n' "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do printf '%s\n' "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do printf '%s\n' "mon:mouse_move 0 10@0"; done
    printf '%s\n' "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}
casa() { for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done; }

# La barra del titolo attiva: "x0 y0 x1 y1" (inclusi), o niente.
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

# Quanti pixel di testo (neri o grigi) ci sono nello spazio delle voci della
# barra in basso: una voce vuol dire un titolo scritto li'.
voci() {
    python3 - "$1" "$2" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
colore = (0, 0, 0) if sys.argv[2] == "nero" else (96, 96, 96)
n = 0
for y in range(h - 26, h - 2):
    for x in range(80, w - 180):
        i = (y * w + x) * 3
        if (px[i], px[i + 1], px[i + 2]) == colore: n += 1
print(n)
EOF
}

# --- 1. si apre, e si guarda -------------------------------------------------
{
    echo "exwin@10"
    echo "key:alt-f1@2"
    echo "/exwin/bin/term &@6"
    echo "key:alt-f5@3"
    echo "foto:$D/1-aperta.ppm@2"
} > "$D/args1.txt"
mapfile -t A < "$D/args1.txt"
timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

[ -s "$D/1-aperta.ppm" ] || { echo "NON RIUSCITO: nessuna fotografia (vedi $D/1.log)"; exit 1; }
B1=$(barra "$D/1-aperta.ppm")
[ -n "$B1" ] || { echo "NON RIUSCITO: nessuna finestra attiva sullo schermo"; exit 1; }
read -r X0 Y0 X1 Y1 <<< "$B1"
echo "=== 1. il terminale e' aperto: barra del titolo ($X0,$Y0)-($X1,$Y1) ==="

esito=0
if [ "$(voci "$D/1-aperta.ppm" nero)" -gt 20 ]; then
    echo "  [OK]  sulla barra in basso c'e' la sua voce"
else
    echo "  [NO]  la barra in basso non mostra nessuna voce"; esito=1
fi

# Il pulsante «_»: il secondo quadrato da destra, alto quanto la barra. La
# barra blu va da f->x+1 a f->x+w-2, quindi il suo lato destro e' X1+2.
RX=$(( X1 + 2 - 40 + 9 ))
RY=$(( Y0 + 9 ))
# La voce sulla barra: la prima, a sinistra, dopo «Avvio».
VX=120
VY=586

# --- 2 e 3. riduci, guarda, riapri, guarda ----------------------------------
{
    echo "exwin@10"
    echo "key:alt-f1@2"
    echo "/exwin/bin/term &@6"
    echo "key:alt-f5@3"
    casa
    passi_a $RX $RY
    echo "mon:mouse_button 1@0"
    echo "mon:mouse_button 0@2"
    echo "foto:$D/2-ridotta.ppm@2"
    casa
    passi_a $VX $VY
    echo "mon:mouse_button 1@0"
    echo "mon:mouse_button 0@2"
    echo "foto:$D/3-riaperta.ppm@2"
} > "$D/args2.txt"
mapfile -t A < "$D/args2.txt"
timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/2.log" 2>&1

echo "=== 2. un clic su «_» in ($RX,$RY) ==="
if [ -s "$D/2-ridotta.ppm" ] && [ -z "$(barra "$D/2-ridotta.ppm")" ]; then
    echo "  [OK]  la finestra non c'e' piu' sullo schermo"
else
    echo "  [NO]  la finestra e' ancora li' (vedi $D/2-ridotta.ppm)"; esito=1
fi
if [ -s "$D/2-ridotta.ppm" ] && [ "$(voci "$D/2-ridotta.ppm" grigio)" -gt 20 ]; then
    echo "  [OK]  la sua voce resta sulla barra, scritta in grigio"
else
    echo "  [NO]  la voce della finestra ridotta non si vede"; esito=1
fi

echo "=== 3. un clic sulla voce in ($VX,$VY) ==="
B3=$(barra "$D/3-riaperta.ppm" 2>/dev/null)
if [ "$B3" = "$B1" ]; then
    echo "  [OK]  e' tornata, nello stesso punto e della stessa larghezza"
else
    echo "  [NO]  prima ($B1), dopo (${B3:-nessuna}) (vedi $D/3-riaperta.ppm)"; esito=1
fi

echo ""
echo "  Le fotografie e i registri sono in $D"
exit $esito
