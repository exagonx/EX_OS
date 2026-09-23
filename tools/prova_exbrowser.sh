#!/bin/bash
# =============================================================================
# tools/prova_exbrowser.sh — il navigatore si chiama EXBrowser, e i dati vecchi
# lo seguono
#
# Dal 23 settembre 2026 /exwin/bin/browser e' /exwin/bin/exbrowser, e i suoi
# dati passano da $HOME/.app/browser/ a $HOME/.app/exbrowser/ al primo avvio.
# La prova prepara una casa su ext2 con la directory VECCHIA e un file dentro,
# avvia EXBrowser su quella casa e guarda:
#
#   1. che il programma parta, e la finestra si chiami EXBrowser (fotografia);
#   2. che lo dica: «i dati del vecchio browser passano in ...»;
#   3. che la directory nuova ci sia, con dentro il file di prima, e quella
#      vecchia no.
#
# ! DAL CD NON SI PUO': e' tutto in sola lettura e HOME non c'e'. Serve un
# disco, e si fa come prova_zip.sh: un'immagine con una partizione, formattata
# da EX-OS stesso con mkfs.
#
# ! NIENTE pkill: qemu_drive.py si ripulisce da solo. Una macchina per volta.
#
# Vuole il sistema costruito:  make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-exb}"
mkdir -p "$D"
IMG="$D/prova-hd.img"

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
[ -x "$SFDISK" ] || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

export EXOS_ISTANZA=exb
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG" "$D"/exb_*.ppm "$D"/exb_*.png
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 600 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/casa@1" "mkdir /disk/casa/.app@1" \
    "mkdir /disk/casa/.app/browser@1" \
    "cp /boot/help.txt /disk/casa/.app/browser/segno.txt@3" \
    "export HOME=/disk/casa@1" \
    "exwin@20" \
    "key:alt-f1@2" \
    "/exwin/bin/exbrowser /exwin/doc/exbrowser.html &@25" \
    "key:alt-f5@3" \
    "foto:$D/exb_finestra.ppm@2" \
    "key:alt-f1@2" \
    "ls /disk/casa/.app@3" \
    "ls /disk/casa/.app/exbrowser@3" > "$D/exb.log" 2>&1

esito=0
controlla() {
    if grep -aq "$2" "$D/exb.log"; then echo "  si'  $1"
    else echo "  NO   $1"; esito=1; fi
}
echo "=== EXBrowser ==="
controlla "lo spostamento e' stato detto"   "i dati del vecchio browser passano in /disk/casa/.app/exbrowser"
controlla "la directory nuova c'e'"         "exbrowser$"
controlla "il file vecchio e' li' dentro"   "segno.txt"
if sed -n '/ls \/disk\/casa\/.app$/,/ls \/disk\/casa\/.app\/exbrowser/p' "$D/exb.log" \
       | grep -aq " browser\s*$"; then
    echo "  NO   la directory vecchia se n'e' andata"; esito=1
else
    echo "  si'  la directory vecchia se n'e' andata"
fi

f="$D/exb_finestra.ppm"
[ -f "$f" ] && { python3 tools/ppm2png.py "$f" "${f%.ppm}.png" >/dev/null 2>&1 ||
                 convert "$f" "${f%.ppm}.png"; echo "  la finestra: ${f%.ppm}.png"; }
echo "ESITO: $([ $esito = 0 ] && echo BUONO || echo 'qualcosa non va, vedi '"$D/exb.log")"
exit $esito
