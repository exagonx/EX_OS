#!/bin/sh
# =============================================================================
# tools/verifica-chiave.sh — is this a boot image that loads WHOLE INTO RAM?
#
#     tools/verifica-chiave.sh <image>
#
# The user's rule (14 October 2026, after weeks of USB keys that did not
# boot): EVERY image written to a USB key is a floppy image that Stage 2
# copies entirely into RAM, and after the boot nothing is read from USB
# again. That shape is the only one proven on the real PC (the MCP73: its
# BIOS boots the key as a floppy; the kernel does not read USB at boot).
# The two that failed there - an installed disk on the key, and a partition
# read with INT 13h/42h - must not come back by accident.
#
# Checked, and any failure stops the write:
#   - the size: 1474560 (1.44 MB, dist/installa.img) or 2949120 (2.88 MB,
#     make usb / make usb64), nothing else - no partition table, no disk;
#   - the boot sector: 0x55AA, and in its BPB 512 bytes per sector and the
#     sectors per track of that floppy (18 or 36);
#   - LOADER.BIN on the FAT12 is a Stage 2 assembled with RAMDISCO=1: the
#     byte after its 'RAMDISCO' signature is 1 (bootloader/stage2/loader.asm).
# =============================================================================
F="$1"
no() { echo "verifica-chiave: $F: $*" >&2; echo "  NON e' un'immagine che si carica tutta in RAM: non si scrive." >&2; exit 1; }
[ -f "$F" ] || no "il file non c'e'"
B=$(stat -c%s "$F")
case "$B" in
    1474560) SPT=18 ;;
    2949120) SPT=36 ;;
    *) no "misura $B byte: deve essere un dischetto da 1,44 o 2,88 MB" ;;
esac
firma=$(od -An -tx1 -j510 -N2 "$F" | tr -d ' ')
[ "$firma" = "55aa" ] || no "il settore di avvio non finisce con 55AA"
bps=$(od -An -tu2 -j11 -N2 "$F" | tr -d ' ')
spt=$(od -An -tu2 -j24 -N2 "$F" | tr -d ' ')
[ "$bps" = 512 ] && [ "$spt" = "$SPT" ] || no "BPB: $bps byte per settore, $spt settori per traccia (attesi 512 e $SPT)"
T=$(mktemp -d) || exit 1
mcopy -n -i "$F" ::/LOADER.BIN "$T/l.bin" 2>/dev/null || { rm -rf "$T"; no "manca LOADER.BIN (lo Stage 2) nel FAT12"; }
modo=$(python3 -c '
import sys
d = open(sys.argv[1], "rb").read()
i = d.find(b"RAMDISCO")
print(d[i + 8] if i >= 0 and i + 8 < len(d) else -1)' "$T/l.bin")
rm -rf "$T"
[ "$modo" = 1 ] || no "lo Stage 2 ha RAMDISCO=$modo: serve 1 (il dischetto copiato tutto in RAM)"
echo "verifica-chiave: $F - dischetto da $B byte, Stage 2 con RAMDISCO=1: si carica tutto in RAM"
