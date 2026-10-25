#!/bin/bash
# =============================================================================
# tools/prova_termios.sh — termios on a pty and on the console (kernel 0.279)
#
#     tools/prova_termios.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_termios.sh [dir]    64 bit
#
# From the CD: libctest (its pty and termios checks, through ioctl: it runs
# on the shared libc) and tools/prove/termiosprova.c, built static with the
# cross compiler, through tcgetattr/tcsetattr/cfmakeraw as ported software
# calls them. termiosprova runs on the console, so its last lines check the
# console's own termios and size too.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/termios-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
$GCC -O2 tools/prove/termiosprova.c -o "$T/termiosprova" || exit 1
$GCC -O2 tools/prove/vgaprova.c -o "$T/vgaprova" || exit 1
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/termiosprova '$T'/vgaprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L termios -d '$T' '$D/p1.img' 8192" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=16384, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=tm$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/termiosprova@30" "/a/termiosprova tasti@4" $'ab\x01\nq@n4' \
    "echo DOPO-GREZZO-ok@3" "/a/vgaprova@4" "foto:$D/vga.ppm@9" "libctest@120" "umount /a@3" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialtm$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^TERMIOS " "$S" | sed 's/^/  /'
[ "$(grep -ac "^TERMIOS fine 0" "$S")" = 2 ] || { echo "  [NO] termiosprova: non tutte giuste"; esito=1; }
# back to lines after raw mode: the shell reads and runs a command again
grep -aq "^DOPO-GREZZO-ok" "$S" && echo "  la shell torna a righe: ok" || { echo "  [NO] la shell non legge piu' le righe dopo il modo grezzo"; esito=1; }
sed -n '/^Pseudo-terminali:/,/^[A-Z][a-z].*:$/p; /^Termios:/,/^$/p' "$S" | grep -a 'FALLITO' | sed 's/^/  libctest /'
grep -a "prove superate" "$S" | tail -1 | sed 's/^/  libctest: /'
grep -aq "\[FALLITO\].*\(pty\|Termios\|TCGETS\|TCSETS\|ONLCR\|ICRNL\|VMIN\|grezzo\|eco\|isatty\|TIOCGWINSZ\)" "$S" && esito=1
# the console's sequences for curses, read back from the screen
python3 tools/schermo_testo.py -i "$D/vga.ppm" > "$D/vga.txt" 2>&1
atteso=' 1|AA  AA
 2|
 3|BB
 4|D  D
 5|EEEE
 6|
 7|    x
 8|s
 9|INV N
10|
11|R2
12|R3
13|FUORI
14|ZY'
if [ "$(head -14 "$D/vga.txt")" = "$atteso" ] && grep -q '^ 9####$' "$D/vga.txt"; then
    echo "  console: ICH DCH ECH IL DL VPA CHA ESC7/8 SGR7 DECSTBM RI: ok"
else
    echo "  [NO] console: lo schermo non e' quello atteso ($D/vga.txt)"; esito=1
    diff <(echo "$atteso") <(head -14 "$D/vga.txt") | sed 's/^/    /'
fi
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
exit $esito
