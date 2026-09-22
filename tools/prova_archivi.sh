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
echo "=== 3. ne crea uno: Nuovo, due Aggiungi, Finisci ==="
{
    avvia "/exwin/bin/archivi "
    echo "key:f10@2"; echo "key:down@1"; echo "key:ret@3"
    echo "/disk/c.zip@4"
    echo "key:f10@2"; echo "key:right@1"; echo "key:down,down@2"; echo "key:ret@3"
    echo "/disk/prova/alfa.txt@5"
    echo "key:f10@2"; echo "key:right@1"; echo "key:down,down@2"; echo "key:ret@3"
    echo "/disk/prova/binario@5"
    echo "key:f10@2"; echo "key:down,down@2"; echo "key:ret@4"
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

echo ""
echo "  Le fotografie sono in $D/*.ppm — tools/ppm2png.py le converte."
exit $esito
