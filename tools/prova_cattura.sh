#!/bin/bash
# =============================================================================
# tools/prova_cattura.sh — la schermata col tasto Stamp (voce 44)
#
# ExWin parte dal CD (QEMU con l'EDID del portatile, 1366x768, ma dal CD si
# arriva a 800x600: la prova segue la misura che trova), con un disco ext2 per
# riprendere i file dall'host. I PNG si decodificano con tools/png_confronta.py:
#
#   1. senza grafica cattura lo dice;
#   2. il tasto Stamp: un file in $HOME/Images, identico pixel per pixel alla
#      fotografia di QEMU dello stesso momento (il puntatore compreso);
#   3. Alt+Stamp: solo la finestra col fuoco, piu' piccola dello schermo;
#   4. dalla console di testo cattura FILE (cosi' si usa da telnet) e -f.
#
#     tools/prova_cattura.sh [directory-di-lavoro]
#     EXOS_ARCH=x86_64 tools/prova_cattura.sh ...       (a 64 bit)
#
# Vuole dist/exos.iso o dist/exos64.iso, sfdisk e debugfs sull'host.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos/cattura}"
mkdir -p "$D"
rm -f "$D"/*.ppm "$D"/*.png

if [ "${EXOS_ARCH:-i386}" = x86_64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
IMG="$D/disco.img"
OFF="$IMG?offset=1048576"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
export EXOS_ISTANZA=cattura EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-512M}"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -vga none -device VGA,edid=on,xres=1366,yres=768"
SER=/tmp/exos/serialcattura.txt
esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

rm -f "$IMG"
qemu-img create -f raw "$IMG" 64M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=128000, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1

timeout 420 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "cattura /disk/no.png@4" \
    "export HOME=/disk@1" "exwin@20" "key:alt-f1@2" \
    "/exwin/bin/finestreprova -t 200 &@3" "key:alt-f5@4" \
    "key:print@1" "foto:$D/qemu.ppm@25" "key:alt-print@25" "key:alt-f1@2" \
    "cattura /disk/testo.png@25" \
    "cattura -f /disk/f.png@25" \
    "ls -l /disk/Images@2" "umount /disk@5" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" > "$D/seriale.txt" 2>/dev/null

grep -a -q "la grafica non e' attiva" "$D/seriale.txt" \
    && ok "senza grafica lo dice" || no "senza grafica"

"$DEBUGFS" -R "ls -l Images" "$OFF" 2>/dev/null > "$D/images.txt"
mapfile -t nomi < <(grep -o 'schermata-[0-9-]*\.png' "$D/images.txt" | sort)
if [ "${#nomi[@]}" -eq 2 ]; then ok "due file in Images: ${nomi[*]}"; else no "file in Images: ${#nomi[@]}"; fi
for i in 0 1; do "$DEBUGFS" -R "dump Images/${nomi[$i]} $D/s$i.png" "$OFF" > /dev/null 2>&1; done
for n in testo f; do "$DEBUGFS" -R "dump $n.png $D/$n.png" "$OFF" > /dev/null 2>&1; done

r=$(python3 tools/png_confronta.py "$D/s0.png" "$D/qemu.ppm" 2>&1); echo "  Stamp: $r"
case "$r" in *"diversi 0") ok "Stamp uguale alla fotografia di QEMU";;
             *) no "Stamp diverso dalla fotografia";; esac
S=${r%% *}
r=$(python3 tools/png_confronta.py "$D/s1.png" 2>&1); echo "  Alt+Stamp: $r"
case "$r" in "$S"|*[!0-9x]*|"") no "Alt+Stamp non e' la finestra";; *) ok "Alt+Stamp: la finestra";; esac
r=$(python3 tools/png_confronta.py "$D/testo.png" 2>&1); echo "  da console: $r"
[ "$r" = "$S" ] && ok "da console, schermo intero" || no "da console"
grep -a -q "c'e' una console di testo" "$D/seriale.txt" && ok "avvisa che la console e' a video" || no "avviso console"
r=$(python3 tools/png_confronta.py "$D/f.png" 2>&1); echo "  -f: $r"
case "$r" in "$S"|*[!0-9x]*|"") no "cattura -f";; *) ok "cattura -f";; esac

[ $esito -eq 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA (vedi $D)"
exit $esito
