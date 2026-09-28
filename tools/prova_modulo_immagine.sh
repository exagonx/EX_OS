#!/bin/bash
# =============================================================================
# tools/prova_modulo_immagine.sh — due cose di EXBrowser 0.014 (28 settembre
# 2026), provate in un giro solo:
#
#   1. <input type="image"> manda `nome.x` e `nome.y` DEL PUNTO CLICCATO
#      (prima sempre zero) — @NAVMETA;
#   2. un indirizzo file: con una domanda («r.html?b.x=3») apre il file
#      r.html: prima cercava un file chiamato «r.html?b.x=3», e un modulo GET
#      mandato a una pagina locale finiva nel nulla.
#
# Il modulo va a risultato.html, che fa `throw` con location.search: l'errore
# EXBrowser lo scrive sulla seriale. Se la pagina non si apre (difetto 2) la
# riga non c'e'; se c'e' con x=0 e y=0 e' il difetto 1.
#
#     tools/prova_modulo_immagine.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-modimm}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialmodimm.txt

cat > "$D/modulo.html" <<'PAGINA'
<html><body>
<form action="risultato.html" method="get"><input type="image" name="b" src="non-c-e.png" value="premi qui il pulsante immagine, largo apposta per il mouse"></form>
</body></html>
PAGINA
cat > "$D/risultato.html" <<'PAGINA'
<html><body><p>arrivato</p>
<script>throw new Error('QUERY[' + location.search + ']');</script>
</body></html>
PAGINA

export EXOS_ISTANZA=modimm EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in modulo.html risultato.html; do "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1; done

# Il pulsante sta in cima alla pagina, da (10, 80) circa: (120, 90) ci cade.
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/modulo.html &@15"; echo "key:alt-f5@4"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 12); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@6"
    echo "foto:$D/dopo.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

esito=0
q=$(tr -d '\r' < "$SER" | grep -o 'QUERY\[[^]]*\]' | tail -1)
echo "        sulla seriale: ${q:-niente}"
if [ -z "$q" ]; then
    echo "  [NO]  risultato.html non si e' aperta (o il clic non e' arrivato): foto $D/dopo.ppm"; esito=1
else
    echo "  [OK]  un indirizzo file: con la domanda apre il file giusto"
    x=$(echo "$q" | sed -n 's/.*b\.x=\([0-9]*\).*/\1/p'); y=$(echo "$q" | sed -n 's/.*b\.y=\([0-9]*\).*/\1/p')
    if [ -n "$x" ] && [ -n "$y" ] && [ "$x" -gt 0 ] && [ "$y" -gt 0 ]; then
        echo "  [OK]  il pulsante immagine manda il punto cliccato (x=$x, y=$y)"
    else
        echo "  [NO]  b.x=$x b.y=$y: il punto del clic non c'e'"; esito=1
    fi
fi
exit $esito
