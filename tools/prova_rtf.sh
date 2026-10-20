#!/bin/bash
# =============================================================================
# tools/prova_rtf.sh — ExEditor in modalita' RTF (@RTF tappa 3, 30 settembre 2026)
#
#   1. sul disco un RTF come lo scrive WordPad: titolo centrato, grassetto,
#      corsivo, rosso sottolineato, Arial 14;
#   2. exeditor lo apre (fotografia 1-aperto.ppm: gli stili a schermo);
#   3. Ctrl+Fine, Invio, Ctrl+B, «NUOVO GRASSETTO», Ctrl+B, « normale»,
#      Ctrl+S;
#   4. il file salvato si legge da fuori, con tools/prove/rtfprova sull'host:
#      la riga nuova deve avere il suo grassetto, e gli stili di prima devono
#      essere ancora li'.
#
#     bash tools/prova_rtf.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-rtf}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

cc -I lib/exrtf -o "$D/rtfprova" tools/prove/rtfprova.c lib/exrtf/rtf.c || exit 1

cat > "$D/prova.rtf" <<'RTF'
{\rtf1\ansi\ansicpg1252\deff0\nouicompat\deflang1040{\fonttbl{\f0\fnil\fcharset0 Calibri;}{\f1\fswiss\fcharset0 Arial;}}
{\colortbl ;\red255\green0\blue0;}
{\*\generator Riched20 10.0.19041}\viewkind4\uc1
\pard\sa200\sl276\slmult1\qc\f0\fs32\lang16 Titolo \b grassetto\b0  e \i corsivo\i0\par
\pard\sa200\sl276\slmult1\fs22\cf1\ul rosso sottolineato\ulnone\cf0  poi \f1\fs28 Arial 14\par
}
RTF

export EXOS_ISTANZA=rtf EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG" "$D"/*.ppm
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/prova.rtf prova.rtf" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exeditor /disk/prova.rtf &@12"; echo "key:alt-f5@4"
    echo "foto:$D/1-aperto.ppm@1"
    echo "key:ctrl-end@1"; echo "key:ret@1"
    echo "key:ctrl-b@1"; echo "NUOVO GRASSETTO@n2"
    echo "key:ctrl-b@1"; echo " normale@n2"
    echo "foto:$D/2-scritto.ppm@1"
    echo "key:ctrl-s@3"
    echo "foto:$D/3-salvato.ppm@1"
    echo "key:alt-f1@2"; echo "sync@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

"$DEBUGFS" -R "dump prova.rtf $D/salvato.rtf" "$OFF" > /dev/null 2>&1
"$D/rtfprova" "$D/salvato.rtf" > "$D/salvato.txt"
cat "$D/salvato.txt" | sed 's/^/    /'

ok=0; no=0
controlla() {       # descrizione, espressione per grep -E sulla lettura
    if grep -Eq "$2" "$D/salvato.txt"; then echo "  [OK]  $1"; ok=$((ok+1))
    else echo "  [NO]  $1"; no=$((no+1)); fi
}
controlla "la riga nuova e' in grassetto"          'B[IU]* col [0-9A-F]+ \[NUOVO GRASSETTO\]'
controlla "e dopo Ctrl+B si torna al normale"      ' col [0-9A-F]+ \[ normale\]'
controlla "il grassetto di prima c'e' ancora"      'B[IU]* col [0-9A-F]+ \[grassetto\]'
controlla "il rosso sottolineato c'e' ancora"      'U col FF0000 \[rosso sottolineato\]'
controlla "il titolo resta centrato"               'paragrafo 0: allineamento 1'
controlla "Arial 14 resta sans e 14"               'fam 1 f [0-9]+ corpo 14 .*\[Arial 14'
echo "  prova_rtf: $ok OK, $no NO (fotografie e file in $D)"
[ "$no" -eq 0 ]
