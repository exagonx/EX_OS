#!/bin/bash
# =============================================================================
# tools/prova_ridisegno.sh — il compositore ridisegna solo quel che cambia
#
# Dal 22 settembre 2026 wserver non ridipinge piu' lo schermo intero: ogni
# cambiamento dichiara il suo rettangolo (una lista, non un riquadro solo), e
# dentro ogni rettangolo lo sfondo si riempie solo dove non c'e' nessuno e ogni
# finestra si disegna solo dove quelle sopra non la coprono. Quel che puo'
# andare storto e' sempre la stessa cosa: un rettangolo dichiarato troppo
# piccolo, e pixel vecchi che restano sullo schermo.
#
# ! QUINDI IL CONFRONTO E' CON UNA RICOMPOSIZIONE COMPLETA, NON CON L'OCCHIO.
# Dopo ogni passo si fotografa, poi si va su Alt+F1 e si torna su Alt+F5: al
# ritorno il server ricompone TUTTO da capo (sporca_tutto in kbd_giro), e si
# fotografa di nuovo. Le due fotografie devono essere uguali pixel per pixel.
# Una differenza e' un rettangolo che qualcuno non ha dichiarato.
#
#   1. term, e una riga scritta dentro          WIN_MSG_AGGIORNA
#   2. winprova sopra, trascinata via           il trascinamento
#   3. un clic sul terminale                    porta_su e il fuoco
#   4. winprova chiusa dal suo pulsante         distruggi
#
# ! L'OROLOGIO DELLA BARRA SI ESCLUDE DAL CONFRONTO (x >= 620, y >= 570): puo'
# cambiare minuto fra le due fotografie, ed e' giusto che cambi.
#
# E `exwin -conta` fa scrivere al server, sulla seriale, quanti fotogrammi e
# quanti pixel al secondo: sono in fondo all'uscita.
#
# OPZ passa altre opzioni al server: OPZ=-contorno prova il trascinamento a
# contorno (la finestra si sposta al rilascio), che deve dare le stesse
# fotografie al passo 2.
#
# ! NIENTE pkill QUI: fa cadere l'intero comando Bash in silenzio, e
# qemu_drive.py si ripulisce da solo. Una macchina per volta (EXOS_ISTANZA=rdg).
#
# Vuole il sistema costruito:  make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos}"
mkdir -p "$D"
rm -f "$D"/rdg_*.ppm "$D"/rdg_*.png

# Il puntatore a passi di dieci, relativo: passi DX DY (anche negativi). Il
# perche' dei passi piccoli sta in prova_ridimensiona.sh.
passi() {
    local dx=$1 dy=$2 sx=10 sy=10 i
    [ "$dx" -lt 0 ] && { sx=-10; dx=$(( -dx )); }
    [ "$dy" -lt 0 ] && { sy=-10; dy=$(( -dy )); }
    for ((i = 10; i <= dx; i += 10)); do printf '%s\n' "mon:mouse_move $sx 0@0"; done
    for ((i = 10; i <= dy; i += 10)); do printf '%s\n' "mon:mouse_move 0 $sy@0"; done
    printf '%s\n' "mon:mouse_move $(( (sx < 0 ? -1 : 1) * (dx % 10) )) $(( (sy < 0 ? -1 : 1) * (dy % 10) ))@0"
}

# Fotografa, ricompone tutto (Alt+F1, Alt+F5), fotografa di nuovo.
doppia() {
    echo "foto:$D/rdg_$1_a.ppm@1"
    echo "key:alt-f1@1"
    echo "key:alt-f5@2"
    echo "foto:$D/rdg_$1_b.ppm@1"
}

{
    echo "exwin -conta $OPZ@20"
    echo "key:alt-f1@2"                 # exwin mostra la scrivania da solo
    echo "/exwin/bin/term &@6"
    echo "key:alt-f5@3"
    echo "echo RIDISEGNO@3"
    doppia 1_scritto

    echo "key:alt-f1@2"
    echo "winprova &@6"
    echo "key:alt-f5@3"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    passi 200 50                        # la barra di winprova, nata in (80,60)
    echo "mon:mouse_button 1@1"
    for i in $(seq 1 20); do echo "mon:mouse_move 10 10@0"; done
    for i in $(seq 1 15); do echo "mon:mouse_move 10 0@0"; done
    echo "mon:mouse_button 0@2"
    doppia 2_spostata                   # puntatore in (550,250)

    passi -450 50                       # dentro il terminale, in (100,300)
    echo "mon:mouse_button 1@0"
    echo "mon:mouse_button 0@2"
    doppia 3_clic

    passi 679 -49                       # il pulsante di chiusura di winprova
    echo "mon:mouse_button 1@0"
    echo "mon:mouse_button 0@3"
    doppia 4_chiusa
} > "$D/rdg_args.txt"

mapfile -t A < "$D/rdg_args.txt"
EXOS_ISTANZA=rdg EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
    timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$D/rdg.log" 2>&1

python3 - "$D" <<'PY'
import sys, os

def leggi(via):
    d = open(via, "rb").read()
    campi, i = [], 2
    while len(campi) < 3:
        while d[i:i + 1].isspace(): i += 1
        j = i
        while not d[j:j + 1].isspace(): j += 1
        campi.append(int(d[i:j])); i = j
    return campi[0], campi[1], d[i + 1:]

D = sys.argv[1]
male = 0
for passo in ("1_scritto", "2_spostata", "3_clic", "4_chiusa"):
    a, b = "%s/rdg_%s_a.ppm" % (D, passo), "%s/rdg_%s_b.ppm" % (D, passo)
    if not (os.path.exists(a) and os.path.exists(b)):
        print("%-11s  MANCA LA FOTOGRAFIA" % passo); male += 1; continue
    w, h, pa = leggi(a)
    _, _, pb = leggi(b)
    n, box = 0, None
    for y in range(h):
        ra, rb = pa[y * w * 3:(y + 1) * w * 3], pb[y * w * 3:(y + 1) * w * 3]
        if ra == rb: continue
        for x in range(w):
            if x >= 620 and y >= 570: continue          # l'orologio
            if ra[x * 3:x * 3 + 3] != rb[x * 3:x * 3 + 3]:
                n += 1
                box = (x, y, x, y) if box is None else \
                      (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    if n == 0:
        print("%-11s  uguale alla ricomposizione completa" % passo)
    else:
        print("%-11s  ! %d pixel DIVERSI in (%d,%d)-(%d,%d)" % ((passo, n) + box))
        male += 1
print("ESITO: %s" % ("BUONO" if male == 0 else "%d passi sbagliati" % male))
PY

for f in "$D"/rdg_*_a.ppm; do
    python3 tools/ppm2png.py "$f" "${f%.ppm}.png" >/dev/null 2>&1 ||
        convert "$f" "${f%.ppm}.png"
done
echo "--- il server, sulla seriale:"
grep -a "fotogrammi" "$D/rdg.log" | tail -30
