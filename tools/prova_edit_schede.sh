#!/bin/bash
# =============================================================================
# tools/prova_edit_schede.sh — l'editor con piu' file, uno per scheda
# (@EDIT-SCHEDE e @TOOLKIT-SCHEDE, 29 settembre 2026)
#
#   1. `edit a.txt b.txt` apre due schede, la prima scelta;
#   2. Ctrl+Tab passa a b.txt: si scrive una X e Ctrl+S la salva IN b.txt;
#   3. Ctrl+Tab torna su a.txt: una Y, poi Ctrl+W chiede (il fuoco sul
#      pulsante che non perde niente), si sceglie «Chiudi» e a.txt resta
#      com'era sul disco;
#   4. Ctrl+N apre una scheda vuota, e Ctrl+Q esce SENZA domande: niente e'
#      rimasto da salvare. Se l'editor chiedesse, la shell dopo non
#      risponderebbe al `cat`.
#
# Il verdetto lo danno i file sul disco (debugfs) e le fotografie.
#
#     tools/prova_edit_schede.sh [directory-di-lavoro]   (default /tmp/exos-edit-schede)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-edit-schede}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=editschede EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
printf 'alfa\n' > "$D/a.txt"
printf 'beta\n' > "$D/b.txt"
"$DEBUGFS" -w -R "write $D/a.txt a.txt" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/b.txt b.txt" "$OFF" > /dev/null 2>&1

timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@12" "key:alt-f1@2" \
    "/exwin/bin/edit /disk/a.txt /disk/b.txt &@8" "key:alt-f5@3" \
    "foto:$D/1-aperti.ppm@1" \
    "key:ctrl-tab@2" "X@n1" "foto:$D/2-b-modificato.ppm@1" "key:ctrl-s@3" \
    "key:ctrl-tab@2" "Y@n1" "key:ctrl-w@3" "foto:$D/3-domanda.ppm@1" \
    "key:shift-tab@1" "key:ret@3" "foto:$D/4-chiuso.ppm@1" \
    "key:ctrl-n@2" "foto:$D/5-nuova.ppm@1" "key:ctrl-q@4" \
    "key:alt-f1@2" "echo FINE@2" "sync@3" > "$D/1.log" 2>&1

esito=0
"$DEBUGFS" -R "dump /a.txt $D/a-dopo.txt" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -R "dump /b.txt $D/b-dopo.txt" "$OFF" > /dev/null 2>&1

if [ "$(cat "$D/b-dopo.txt" 2>/dev/null)" = "Xbeta" ]; then
    echo "  [OK]  Ctrl+Tab e Ctrl+S: la X e' finita in b.txt"
else
    echo "  [NO]  b.txt e' diventato: $(cat "$D/b-dopo.txt" 2>/dev/null)"; esito=1
fi
if [ "$(cat "$D/a-dopo.txt" 2>/dev/null)" = "alfa" ]; then
    echo "  [OK]  Ctrl+W ha chiesto e chiuso a.txt senza salvare la Y"
else
    echo "  [NO]  a.txt e' diventato: $(cat "$D/a-dopo.txt" 2>/dev/null)"; esito=1
fi
if tr -d '\r' < "$D/1.log" | grep -q "^FINE"; then
    echo "  [OK]  Ctrl+Q e' uscito senza domande"
else
    echo "  [NO]  dopo Ctrl+Q la shell non ha risposto (vedi $D/*.ppm)"; esito=1
fi
echo "  Le fotografie sono in $D"
exit $esito
