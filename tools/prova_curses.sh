#!/bin/bash
# =============================================================================
# tools/prova_curses.sh — ncurses on the EX-OS console (kernel 0.279)
#
#     tools/prova_curses.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_curses.sh [dir]    64 bit
#
# tools/prove/cursesprova.c, built static with -lncurses from the cross
# sysroot (tools/ncurses-exos/costruisci.sh), on an ext2 disk next to the CD.
# On the console (TERM=vga): the screen is read back while it waits for keys
# (tools/schermo_testo.py: box, reverse video, colour, deleteln, a window),
# then Up, F1, Delete, Page Down and q are typed and it must print their
# KEY_ names on the serial line.
#
# Then, if the tools CD is there (make iso / tools/mktools64.sh), the same
# program is compiled INSIDE EX-OS from /cdrom/prova-curses.c with the CD's
# gcc and -lncurses, and the keys are tried again on what it built.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/curses-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    STRUMENTI=dist/exos64-tools.iso
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    STRUMENTI=dist/exos-tools.iso
    export EXOS_RAM=256M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
$GCC -O2 tools/prove/cursesprova.c -o "$T/cursesprova" -lncurses || {
    echo "  (manca ncurses nel sysroot? tools/ncurses-exos/costruisci.sh)"; exit 1; }
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/cursesprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L curses -d '$T' '$D/p1.img' 32768" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 40M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=cu$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/cursesprova@5" "foto:$D/curses.ppm@1" "key:up,f1,delete,pgdn@1" "q@n3" \
    "umount /a@3" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialcu$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
python3 tools/schermo_testo.py -i "$D/curses.ppm" > "$D/schermo.txt" 2>&1
esito=0
si() { echo "  ok  $1"; }
no() { echo "  [NO] $1"; esito=1; }
riga() { sed -n "${1}p" "$D/schermo.txt" | cut -c4-; }
case "$(riga 1)" in "+--"*"--+") si "il riquadro (box) con i caratteri di linea" ;; *) no "riga 1: $(riga 1)" ;; esac
grep -q "CURSES PRONTO [0-9]*x[0-9]* TERM=vga" "$D/schermo.txt" \
    && si "$(grep -o 'CURSES PRONTO [0-9]*x[0-9]* TERM=vga' "$D/schermo.txt")" || no "manca CURSES PRONTO"
case "$(riga 3)" in "| INVERSO"*) si "testo" ;; *) no "riga 3: $(riga 3)" ;; esac
grep -q '^ 3#  #######$' "$D/schermo.txt" && si "A_REVERSE" || no "A_REVERSE: $(grep '^ 3#' "$D/schermo.txt")"
case "$(riga 4)" in "| COLORE"*) si "COLOR_PAIR" ;; *) no "riga 4: $(riga 4)" ;; esac
case "$(riga 5)" in "| riga che sale"*) si "deleteln: la riga sotto e' salita" ;; *) no "riga 5: $(riga 5)" ;; esac
grep -q "sparira" "$D/schermo.txt" && no "la riga cancellata e' ancora sullo schermo"
case "$(riga 8)" in "|         +------------------+"*) si "newwin con il suo riquadro" ;; *) no "riga 8: $(riga 8)" ;; esac
grep -q "| FINESTRA" "$D/schermo.txt" && si "la finestra scrive dentro di se'" || no "manca FINESTRA"
L=$(grep -a "^CURSES tasti:" "$S" | tail -1 | tr -d '\r')
[ "$L" = "CURSES tasti: KEY_UP KEY_F(1) KEY_DC KEY_NPAGE q" ] && si "$L" || no "tasti: '$L'"
# --- the same, compiled inside EX-OS ------------------------------------------
if [ -f "$STRUMENTI" ]; then
    export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide,index=0 -drive file=$STRUMENTI,media=cdrom,if=ide,index=3,readonly=on"
    EXOS_ISTANZA=cn$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
        timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "mount cd1 /cdrom@6" \
        "export TMPDIR=/a@2" "cd /a@2" \
        "/cdrom/exos/bin/gcc -O2 /cdrom/prova-curses.c -o /a/nativo -lncurses@~400" \
        "ls -l /a/nativo@3" "/a/nativo@6" "key:up,f1,delete,pgdn@1" "q@n3" \
        "umount /a@3" > "$D/nativo.log" 2>&1
    N="$D/seriale-nativo.txt"
    cp "/tmp/exos/serialcn$ARCH.txt" "$N" 2>/dev/null
    cat "$D/nativo.log" >> "$N"
    L=$(grep -a "^CURSES tasti:" "$N" | tail -1 | tr -d '\r')
    [ "$L" = "CURSES tasti: KEY_UP KEY_F(1) KEY_DC KEY_NPAGE q" ] \
        && si "compilato dentro EX-OS (gcc -lncurses del CD strumenti): $L" \
        || { no "compilato dentro EX-OS: '$L' (vedi $N)"; grep -a "error\|errore\|undefined" "$N" | head -5 | sed 's/^/    /'; }
else
    echo "  (manca $STRUMENTI: la prova col gcc di EX-OS si salta)"
fi

if grep -aq "KERNEL PANIC\|terminato per fault" "$S" ${N:+"$N"}; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" ${N:+"$N"} | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  lo schermo: $D/schermo.txt, la seriale: $S"
exit $esito
