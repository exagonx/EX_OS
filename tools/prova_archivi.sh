#!/bin/bash
# =============================================================================
# tools/prova_archivi.sh — l'archiviatore di ExWin, nei due versi
#
# Prova la meta' grafica di @ZIP (/exwin/bin/archivi):
#
#   1. apre un archivio fatto dal comando `zip`, e lo elenca;
#   2. «Estrai tutto...» in una cartella scelta nel dialogo, e `cmp` dice che i
#      file tornati sono IDENTICI agli originali;
#   3. «Nuovo...», due «Aggiungi file...», «Finisci» — e l'archivio che ne esce
#      lo verifica `unzip -t` di Info-ZIP QUI FUORI, che e' l'unico giudice che
#      non ha scritto il formato.
#
# ! SI PILOTA DALLA TASTIERA, E I MENU SI CONTANO. F10 apre il primo menu, la
# freccia destra passa al successivo, la freccia giu' scende di una voce e
# SALTA I SEPARATORI — provato guardando il menu aperto in una fotografia, non
# indovinato. Se si aggiunge una voce a un menu, i conti qui sotto cambiano: e'
# il prezzo di pilotare un programma interattivo, ed e' lo stesso che paga
# tools/mkhd.sh con l'installatore.
#
# ! E IL VERDETTO NON LO DA' LA FINESTRA. Una finestra che scrive «2 estratti»
# ha detto quel che crede di aver fatto: il confronto lo fanno `cmp` dentro e
# `unzip -t` fuori.
#
#     tools/prova_archivi.sh [directory-di-lavoro]   (default /tmp/exos-arc)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), debugfs (e2fsprogs).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-arc}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
[ -x "$SFDISK" ]  || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }

export EXOS_ISTANZA=arc
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

# La scrivania, e l'archiviatore su una finestra sua.
avvia() {
    printf '%s\n' "mount hd0p1 /disk@6" "exwin@20" "key:alt-f1@3" \
                  "$1&@10" "key:alt-f5@4"
}

esito=0

# --- 1. Il disco, due file e un archivio fatto col comando -------------------
echo "=== 1. disco, due file, e un archivio fatto da /bin/zip ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/prova@2" \
    "cp /boot/help.txt /disk/prova/alfa.txt@3" \
    "cp /bin/ls /disk/prova/binario@3" \
    "zip /disk/a.zip /disk/prova/alfa.txt /disk/prova/binario@8" \
    > "$D/1-disco.log" 2>&1

grep -q "/disk/a.zip: 2 file" "$D/1-disco.log" || {
    echo "NON RIUSCITO: l'archivio di partenza non si e' fatto (vedi $D/1-disco.log)"
    exit 1
}

# --- 2. Aprire ed estrarre tutto dalla finestra ------------------------------
#
# Comandi e' il secondo menu (F10 poi destra); «Estrai tutto...» e' la seconda
# voce (una freccia giu').
echo "=== 2. apre l'archivio ed estrae tutto ==="
{
    avvia "/exwin/bin/archivi /disk/a.zip "
    echo "foto:$D/2-aperto.ppm@2"
    echo "key:f10@2"; echo "key:right@1"; echo "key:down@1"; echo "key:ret@3"
    echo "/disk/fuori@5"
    echo "foto:$D/2-estratto.ppm@2"
    echo "key:alt-f1@2"
    echo "cmp /disk/prova/alfa.txt /disk/fuori/alfa.txt@4"
    echo "cmp /disk/prova/binario /disk/fuori/binario@4"
    echo "echo CONFRONTO-FINITO@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/2-estrai.log" 2>&1

if grep -q "CONFRONTO-FINITO" "$D/2-estrai.log" && \
   ! grep -qi "differ\|non riesco" "$D/2-estrai.log"; then
    echo "  [OK]  estratti dalla finestra e identici agli originali"
else
    echo "  [NO]  l'estrazione dalla finestra (vedi $D/2-estrai.log)"
    esito=1
fi

# --- 3. Farne uno dalla finestra, e farlo giudicare fuori --------------------
#
# File e' il primo menu: «Nuovo...» e' la seconda voce, «Finisci» la terza.
# Comandi: «Aggiungi file...» e' la terza voce, ma la freccia giu' salta il
# separatore, quindi due giu'.
echo "=== 3. ne crea uno: Nuovo, due Aggiungi, e si costruisce DA SOLO ==="
{
    avvia "/exwin/bin/archivi "
    echo "key:f10@2"; echo "key:down@1"; echo "key:ret@3"
    echo "/disk/c.zip@4"
    echo "key:f10@2"; echo "key:right@1"; echo "key:down,down@2"; echo "key:ret@3"
    echo "/disk/prova/alfa.txt@5"
    echo "key:f10@2"; echo "key:right@1"; echo "key:down,down@2"; echo "key:ret@3"
    echo "/disk/prova/binario@5"
    # ! NIENTE «Costruisci» (fu «Finisci»): dal 26 settembre 2026 l'archivio
    # si costruisce da solo a ogni aggiunta, ed e' questo che si prova.
    echo "foto:$D/3-finito.ppm@2"
    echo "key:alt-f1@2"
    echo "zip -l /disk/c.zip@5"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/3-crea.log" 2>&1

"$DEBUGFS" -R "dump /c.zip $D/c.zip" "$OFF" > /dev/null 2>&1

if [ -s "$D/c.zip" ] && python3 -c "
import sys, zipfile
z = zipfile.ZipFile('$D/c.zip')
sys.exit(0 if z.testzip() is None and len(z.infolist()) == 2 else 1)
" 2>> "$D/3-estraneo.log"; then
    echo "  [OK]  l'archivio fatto dalla finestra lo apre un lettore estraneo"
else
    echo "  [NO]  l'archivio fatto dalla finestra non si apre fuori"
    esito=1
fi

if command -v unzip > /dev/null; then
    if unzip -t "$D/c.zip" >> "$D/3-estraneo.log" 2>&1; then
        echo "  [OK]  unzip -t di Info-ZIP: nessun errore"
    else
        echo "  [NO]  unzip -t lo rifiuta (vedi $D/3-estraneo.log)"
        esito=1
    fi
fi

# --- 4. L'intestazione sopravvive ai dialoghi --------------------------------
#
# ! NON E' PIGNOLERIA GRAFICA: e' il difetto che il 22 settembre 2026 si e'
# fatto due volte in un giorno — quel che una finestra disegna di suo va nel
# suo EXM_DISEGNA, o il primo dialogo che la copre se lo porta via. Qui si
# guarda che la riga delle colonne ci sia ANCORA dopo che una modale si e'
# aperta e chiusa.
echo "=== 4. la riga delle colonne, dopo che un dialogo l'ha coperta ==="
# ! DOVE SIA LA FINESTRA LO DICE LA FOTOGRAFIA, NON QUESTO SCRIPT. Il server la
# piazza a cascata (EX_AUTO): la prima volta che questa prova ha guardato una
# riga di pixel decisa qui, ha accusato il programma di aver perso
# l'intestazione che nella fotografia si vedeva benissimo.
CLIENT=$(python3 tools/misura_finestre.py --client "$D/2-estratto.ppm" 2>/dev/null)
if [ -n "$CLIENT" ] && python3 - "$D/2-estratto.ppm" "${CLIENT#* }" <<'FINEPY'
import sys
# L'intestazione sta fra la barra dei menu e l'elenco: una ventina di pixel
# sotto l'angolo del client. Se e' stata spazzata via, li' dentro c'e' un
# colore solo; se c'e' del testo, ce ne sono almeno due.
d = open(sys.argv[1], 'rb').read()
cy = int(sys.argv[2])
i = d.index(b'255\n') + 4                      # fine dell'intestazione PPM
larghezza = int(d.split()[1])
for riga in range(cy + 22, cy + 38):           # la banda dell'intestazione
    base = i + (riga * larghezza) * 3
    colori = {d[base + x*3: base + x*3 + 3] for x in range(10, 500)}
    if len(colori) > 1:
        sys.exit(0)
sys.exit(1)
FINEPY
then
    echo "  [OK]  l'intestazione c'e' ancora dopo il dialogo"
else
    echo "  [NO]  l'intestazione e' sparita: vedi EXM_DISEGNA in archivi.c"
    esito=1
fi

# --- 5. quaranta voci: la barra di scorrimento, e un clic su «Byte» --------
#
# ! L'ORDINE LO DICE LA PRIMA RIGA: dopo il clic su «Byte» la prima riga deve
# essere il file piu' piccolo, che si chiama apposta «piccolissimo.txt». Si
# riconosce dalla fotografia contando i pixel scuri nella prima riga della
# colonna «Nome» prima e dopo: cambiano solo se la riga e' un'altra.
echo "=== 5. quaranta voci: scorrimento, e ordinare per Byte ==="
python3 - "$D" <<'FINEPY'
import sys, zipfile, random
d = sys.argv[1]
random.seed(3)
z = zipfile.ZipFile(d + "/grande.zip", "w", zipfile.ZIP_DEFLATED)
for i in range(39):
    z.writestr("cartella/file%02d.txt" % i, "x" * random.randint(200, 5000))
z.writestr("piccolissimo.txt", "x")
z.close()
FINEPY
"$DEBUGFS" -w -R "write $D/grande.zip grande.zip" "$OFF" > /dev/null 2>&1

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do printf '%s\n' "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do printf '%s\n' "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do printf '%s\n' "mon:mouse_move 0 10@0"; done
    printf '%s\n' "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}
{
    avvia "/exwin/bin/archivi /disk/grande.zip "
    echo "foto:$D/5-prima.ppm@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/5a.log" 2>&1
CL=$(python3 tools/misura_finestre.py --client "$D/5-prima.ppm" 2>/dev/null)
if [ -z "$CL" ]; then
    echo "  [NO]  la finestra non si trova nella fotografia"; esito=1
else
    read -r CX CY <<< "$CL"
    # «Byte» e' la terza colonna: 4 + 170 + 170 + 40 dal bordo, a meta' altezza
    # dell'intestazione (22 + 9 sotto l'inizio del client).
    {
        avvia "/exwin/bin/archivi /disk/grande.zip "
        for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
        passi_a $(( CX + 384 )) $(( CY + 31 ))
        echo "mon:mouse_button 1@0"
        echo "mon:mouse_button 0@2"
        echo "foto:$D/5-dopo.ppm@2"
    } > "$D/args.txt"
    mapfile -t A < "$D/args.txt"
    timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/5b.log" 2>&1
    if python3 - "$D/5-prima.ppm" "$D/5-dopo.ppm" "$CX" "$CY" <<'FINEPY'
import sys
def scuri(p, cx, cy):
    d = open(p, "rb").read().split(b"\n", 3)
    w = int(d[1].split()[0]); px = d[3]
    n = 0
    for y in range(cy + 40, cy + 56):              # la prima riga
        for x in range(cx + 8, cx + 170):         # la colonna «Nome»
            i = (y * w + x) * 3
            if px[i] < 80 and px[i + 1] < 80 and px[i + 2] < 80: n += 1
            if (px[i], px[i + 1], px[i + 2]) == (255, 255, 255) and False: pass
    return n
cx, cy = int(sys.argv[3]), int(sys.argv[4])
a, b = scuri(sys.argv[1], cx, cy), scuri(sys.argv[2], cx, cy)
print("        prima riga, pixel del nome: prima %d, dopo %d" % (a, b))
sys.exit(0 if a != b else 1)
FINEPY
    then
        echo "  [OK]  un clic su «Byte» ha cambiato la prima riga"
    else
        echo "  [NO]  il clic su «Byte» non ha riordinato (vedi $D/5-dopo.ppm)"; esito=1
    fi
fi

echo ""
echo "  Le fotografie sono in $D/*.ppm — tools/ppm2png.py le converte."
exit $esito
