#!/bin/bash
# =============================================================================
# tools/prova_selezione.sh — scegliere del testo in EXBrowser e incollarlo in
# exeditor (@NAV-SELEZIONE, 30 settembre 2026)
#
#   1. EXBrowser apre una pagina con una riga nota;
#   2. si trascina col mouse sopra la riga e si preme Ctrl+C;
#   3. exeditor apre un file vuoto, Ctrl+V, Ctrl+S;
#   4. il file si legge dal disco, da fuori: deve contenere la riga.
#
# ! IL MOUSE DI QEMU E' RELATIVO e si muove a passi di dieci (vedi
# prova_passaggio.sh, da cui vengono le coordinate: la prima riga della
# pagina sta a y 90..102 sullo schermo).
#
#     bash tools/prova_selezione.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-selezione}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

printf '<html><body><p>ALFA BETA GAMMA DELTA</p><p>dopo</p></body></html>\n' > "$D/sel.html"
: > "$D/copia.txt"

export EXOS_ISTANZA=selezione EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG" "$D"/*.ppm
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/sel.html sel.html" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/copia.txt copia.txt" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/sel.html &@15"; echo "key:alt-f5@4"
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    # a destra della fine della prima riga, (300, 96), e da li' verso sinistra
    # FIN OLTRE il bordo della finestra: la parola piu' vicina a ciascun capo e'
    # DELTA e ALFA.
    # ! OLTRE IL BORDO APPOSTA: li' x e' negativa, e fino al 30 settembre 2026
    # EX_X la leggeva senza segno (-2 = 65534), cosi' la scelta saltava
    # all'ultima parola. E partendo da sinistra, come faceva la prima stesura,
    # il clic cadeva sul bordo della finestra e non arrivava a nessuno.
    for i in $(seq 1 30); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move 0 6@1"
    echo "foto:$D/0-prima.ppm@1"
    echo "mon:mouse_button 1@1"
    for i in $(seq 1 32); do echo "mon:mouse_move -10 0@0"; done
    echo "mon:mouse_button 0@2"
    echo "foto:$D/1-scelto.ppm@1"
    echo "key:ctrl-c@2"
    echo "key:alt-f1@2"
    echo "/exwin/bin/exeditor /disk/copia.txt &@10"; echo "key:alt-f5@4"
    echo "key:ctrl-v@2"
    echo "foto:$D/2-incollato.ppm@1"
    echo "key:ctrl-s@3"
    echo "key:alt-f1@2"; echo "sync@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

COPIA=$("$DEBUGFS" -R "cat copia.txt" "$OFF" 2>/dev/null)
echo "  copia.txt: [$COPIA]"
if echo "$COPIA" | grep -q "ALFA BETA GAMMA DELTA"; then
    echo "  [OK]  la riga scelta nel navigatore e' arrivata in exeditor"
    echo "  Le fotografie sono in $D"
    exit 0
fi
echo "  [NO]  in copia.txt non c'e' la riga (fotografie in $D)"
exit 1
