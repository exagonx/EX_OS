#!/bin/bash
# =============================================================================
# tools/prova_calctor_nastro.sh — Calctor da ufficio (cronologia accesa)
#
# Con la casella Cronologia accesa Calctor e' una calcolatrice scrivente:
# niente =, + e - sommano al totale e lo mostrano (vedi nastro() in
# calctor.c). Si parte con `cronologia = si` nel profilo, si battono i conti
# TASTO PER TASTO — una riga con Invio no: in questa modalita' Invio vale + —
# e il verdetto lo da' il nastro salvato con Ctrl+S, come in prova_calctor.sh.
#
#   12 +   5 +   3 -          -> 14
#   2 * 3 +                   -> il prodotto si chiude e si somma: 20
#   +                         -> «20  subtotale»
#
# Si controlla anche il fuoco: il nastro si apre per ultimo e se lo prendeva,
# e i tasti non arrivavano alla calcolatrice.
#
#     tools/prova_calctor_nastro.sh [directory-di-lavoro]   (default /tmp/exos-calcnastro)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk e debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-calcnastro}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=calcnastro EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
printf 'modo       = normale\nbase       = dec\nangoli     = deg\ncronologia = si\n' > "$D/calctor.cfg"
for d in casa casa/.exwin casa/.exwin/config; do "$DEBUGFS" -w -R "mkdir $d" "$OFF" > /dev/null 2>&1; done
"$DEBUGFS" -w -R "write $D/calctor.cfg casa/.exwin/config/calctor.cfg" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "export HOME=/disk/casa@1"
    echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/calctor &@8"; echo "key:alt-f5@3"
    echo "key:1,2,shift-equal@1"; echo "key:5,shift-equal@1"; echo "key:3,minus@1"
    echo "key:2,shift-8,3,shift-equal@1"; echo "key:shift-equal@1"
    echo "foto:$D/1-totale.ppm@1"
    echo "key:ctrl-s@3"
    for i in $(seq 1 40); do echo "key:backspace@0"; done
    echo "/disk/nastro.txt@4"
    echo "key:ret@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

rm -f "$D/nastro.txt"
"$DEBUGFS" -R "dump /nastro.txt $D/nastro.txt" "$OFF" > /dev/null 2>&1
echo "=== il nastro salvato ==="
sed 's/^/    /' "$D/nastro.txt" 2>/dev/null

esito=0
controlla() {   # riga-attesa descrizione
    if grep -qxF "$1" "$D/nastro.txt" 2>/dev/null; then echo "  [OK]  $2"
    else echo "  [NO]  $2: manca «$1»"; esito=1; fi
}
controlla "12 +"            "il fuoco e' della calcolatrice, e + somma (12 +)"
controlla "3 -"             "il - sottrae (3 -)"
controlla "2 * 3 = 6"       "+ chiude il prodotto"
controlla "6 +"             "... e lo somma"
controlla "20  subtotale"   "+ senza un numero nuovo scrive il subtotale (20)"
echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
