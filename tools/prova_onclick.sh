#!/bin/bash
# =============================================================================
# tools/prova_onclick.sh — i gestori scritti negli attributi girano anche su
# una pagina SENZA <script> (EXBrowser 0.013, 28 settembre 2026)
#
# Il difetto: il motore JavaScript si apriva solo davanti a uno <script>, e una
# pagina con i soli onclick= (i siti semplici e vecchi sono fatti cosi') non
# ne eseguiva nessuno. Trovato con tools/prova_cursore.sh.
#
# La pagina ha un pulsante e basta, col gestore nell'attributo che fa `throw`:
# l'errore EXBrowser lo scrive sulla seriale, ed e' li' che si legge.
#
#     tools/prova_onclick.sh [directory-di-lavoro]   (default /tmp/exos-onclick)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-onclick}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialonclick.txt

cat > "$D/onclick.html" <<'PAGINA'
<html><body>
<button onclick="throw new Error('ONCLICK-GIRATO')">Premi qui il pulsante di prova, largo apposta per il mouse</button>
</body></html>
PAGINA

export EXOS_ISTANZA=onclick EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/onclick.html onclick.html" "$OFF" > /dev/null 2>&1

# Il pulsante sta in cima alla pagina, da (10, 80) circa: (100, 90) ci cade dentro.
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/onclick.html &@15"; echo "key:alt-f5@4"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 10); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@3"
    echo "foto:$D/pulsante.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

if tr -d '\r' < "$SER" | grep -q "ONCLICK-GIRATO"; then
    echo "  [OK]  il gestore nell'attributo ha girato, su una pagina senza <script>"
    exit 0
fi
echo "  [NO]  sulla seriale non c'e' ONCLICK-GIRATO: il gestore non ha girato (foto: $D/pulsante.ppm)"
exit 1
