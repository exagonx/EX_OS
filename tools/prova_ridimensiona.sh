#!/bin/bash
# =============================================================================
# tools/prova_ridimensiona.sh — la prova del ridimensionamento delle finestre
#
# Fa le due cose che si possono fare, e le misura nei pixel del framebuffer:
#
#   1. col MOUSE: apre /exwin/bin/term, acchiappa la presa nell'angolo, tira, e
#      guarda che mentre si tira si muova solo il CONTORNO e che al rilascio la
#      finestra finisca esattamente li'. La griglia del terminale deve
#      rimpicciolire con lei;
#   2. coi TASTI: apre winprova, la trascina altrove, poi la allarga con le
#      frecce e controlla che NON torni dov'era nata — il difetto che si aveva
#      quando la misura viaggiava insieme alla posizione.
#
# ! IL PUNTATORE SI PILOTA, MA A PASSI PICCOLI. In RIPRENDERE.md c'era scritto
# che i movimenti relativi del monitor di QEMU si perdono e che le prove col
# mouse non sono ripetibili: si perdono quelli GRANDI. A dieci pixel per volta
# il puntatore arriva dove lo si manda, al pixel — provato. La ricetta e':
# saturare in un angolo con qualche movimento enorme (li' il troncamento non
# fa danno, si sbatte contro il bordo), e da li' contare a passi di dieci.
#
# ! E CIO' CHE CONTA LO DICE LA FOTOGRAFIA, non il log. Un'applicazione puo'
# dire di essersi ridimensionata e il compositore averla disegnata di
# un'altra misura: sono due processi. tools/misura_finestre.py conta i pixel.
#
# ! UNA MACCHINA SOLA PER VOLTA, come per prova_ssh.sh.
#
# Vuole il sistema costruito in grafica:  make SVGA=800x600 && make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos}"
mkdir -p "$D"
pkill -9 -f qemu-system 2>/dev/null
sleep 2

# Il puntatore parte in mezzo allo schermo: prima lo si sbatte nell'angolo.
CASA=""
for i in $(seq 1 5); do CASA="$CASA mon:mouse_move${IFS}-600${IFS}-600@0"; done

# Da (0,0) a (x,y) a passi di dieci. passi_a DX DY
#
# ! SI VA IN DIAGONALE FINCHE' SI PUO', POI DIRITTI. Qui c'era «in diagonale per
# quanto e' ALTO il bersaglio, poi in orizzontale», e sbaglia in silenzio quando
# il bersaglio e' piu' in basso che a destra: la diagonale supera la x, il resto
# viene negativo, il ciclo bash gira zero volte e il puntatore finisce decine di
# pixel piu' in la'. Le prove di questo script hanno tutte dx > dy, quindi non
# se n'e' mai accorto nessuno; prova_doppioclic.sh, che deve colpire un segno «+»
# vicino al bordo sinistro, ci ha perso un giro di prove.
passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do printf '%s\n' "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do printf '%s\n' "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do printf '%s\n' "mon:mouse_move 0 10@0"; done
    printf '%s\n' "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}

# --- 1. il terminale, tirato per l'angolo ------------------------------------
#
# ! DOVE SIA LA PRESA LO DICE LA FOTOGRAFIA, e non un numero scritto qui. Da
# quando le finestre nascono a cascata — cioe' da quando si puo' aprire due
# volte lo stesso programma — la posizione di /exwin/bin/term dipende da quante
# finestre c'erano prima, e uno script che se la ricorda a memoria si rompe la
# prima volta che la scrivania cambia. Percio' il giro e' in due tempi: prima
# si apre e si fotografa, poi si chiede alla fotografia dove tirare.
{
    echo "exwin@10"
    echo "/exwin/bin/term &@6"
    echo "key:alt-f2@3"
    echo "foto:$D/rid_prima.ppm@2"
} > "$D/rid_args.txt"

mapfile -t A < "$D/rid_args.txt"
EXOS_ISTANZA=rid EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_ATTESA_FINALE=120 \
    timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/rid.log" 2>&1 &
QRID=$!

# La macchina resta accesa (EXOS_ATTESA_FINALE): si aspetta la prima foto,
# si guarda dove sta la presa, e si finisce il giro con un secondo pilota.
for i in $(seq 1 60); do [ -s "$D/rid_prima.ppm" ] && break; sleep 2; done
sleep 2
PRESA=$(python3 tools/misura_finestre.py --presa "$D/rid_prima.ppm")
echo "=== 1. IL TERMINALE, TIRATO PER LA PRESA ==="
echo "    la presa sta in ($PRESA)"
kill $QRID 2>/dev/null
wait $QRID 2>/dev/null

{
    echo "exwin@10"
    echo "/exwin/bin/term &@6"
    echo "key:alt-f2@3"
    echo "foto:$D/rid_prima.ppm@2"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    passi_a $PRESA
    echo "mon:mouse_button 1@1"
    for i in $(seq 1 8);  do echo "mon:mouse_move -10 -10@0"; done
    for i in $(seq 1 3);  do echo "mon:mouse_move -10 0@0"; done
    echo "mon:mouse_move -4 -2@0"
    echo "foto:$D/rid_tirando.ppm@1"
    echo "mon:mouse_button 0@2"
    echo "foto:$D/rid_dopo.ppm@2"
} > "$D/rid_args.txt"

pkill -9 -f qemu-system 2>/dev/null
sleep 2
mapfile -t A < "$D/rid_args.txt"
EXOS_ISTANZA=rid EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/rid.log" 2>&1

grep -a "term:" "$D/rid.log" | tail -2
python3 tools/misura_finestre.py "$D/rid_prima.ppm" "$D/rid_tirando.ppm" "$D/rid_dopo.ppm"

# --- 2. winprova: si sposta, poi si allarga ----------------------------------
pkill -9 -f qemu-system 2>/dev/null
sleep 2
{
    echo "exwin@10"
    echo "winprova &@6"
    echo "key:alt-f2@3"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    passi_a 200 50                     # la barra del titolo, che nasce in (80,60)
    echo "mon:mouse_button 1@1"
    for i in $(seq 1 12); do echo "mon:mouse_move 10 10@0"; done
    echo "mon:mouse_button 0@2"
    echo "foto:$D/mos_prima.ppm@2"
    echo "key:right,right@3"
    echo "foto:$D/mos_dopo.ppm@2"
} > "$D/mos_args.txt"

mapfile -t B < "$D/mos_args.txt"
EXOS_ISTANZA=mos EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 300 python3 tools/qemu_drive.py "${B[@]}" > "$D/mos.log" 2>&1

echo
echo "=== 2. SPOSTATA, POI ALLARGATA COI TASTI ==="
echo "    (l'angolo in alto a sinistra deve restare LO STESSO)"
grep -a "winprova: adesso" "$D/mos.log" | tail -4
python3 tools/misura_finestre.py "$D/mos_prima.ppm" "$D/mos_dopo.ppm"
