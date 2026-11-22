#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/nano-exos/prova.sh — GNU nano on the EX-OS console, in QEMU
#
#     tools/nano-exos/prova.sh            32 and 64 bit
#     tools/nano-exos/prova.sh 32|64      one only
#
# nano from cross_build/nano-exos<bit> (tools/nano-exos/costruisci.sh) on an
# ext2 disk next to the CD, mounted at /a. On the console (TERM=vga):
#   1. nano -V prints its version;
#   2. nano -I /a/prova.txt: the screen is read back (the title bar, the
#      help lines), a line is typed, ^O and Enter save, ^X leaves;
#   3. cat /a/prova.txt gives the line back;
#   4. nano with the system nanorc (include of the syntax files, glob) opens
#      a .c file and leaves without errors on the status line;
#   5. no [FAULT] or PANIC on the serial line.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 D T IMG S EXTRA ISO
    D=/tmp/exos-prove/nano$bit; rm -rf "$D"; mkdir -p "$D"
    if [ "$bit" = 64 ]; then
        ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M; EXTRA="-cpu qemu64"
    else
        ISO=dist/exos.iso; unset EXOS_QEMU; export EXOS_RAM=256M; EXTRA=""
    fi
    echo "=== $bit bit ==="
    [ -x ${EXOS_FUORI}/cross_build/nano-exos$bit/exos/bin/nano ] || { no "$bit: manca nano (tools/nano-exos/costruisci.sh $bit)"; return; }
    T="$D/albero"; mkdir -p "$T"
    cp -r ${EXOS_FUORI}/cross_build/nano-exos$bit/exos/bin ${EXOS_FUORI}/cross_build/nano-exos$bit/exos/nano "$T/"
    # the system nanorc, pointed at the disk
    sed 's|/exos/nano/share/nano/|/a/nano/share/nano/|' "$T/nano/nanorc" > "$T/nano/prova.rc"
    printf 'int main(void)\n{\n    return 0;\n}\n' > "$T/prova.c"
    fakeroot sh -c "chown -R 0:0 '$T'; chmod -R 755 '$T';
        $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L nano -d '$T' '$D/p1.img' 32768" \
        > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; no "$bit: mke2fs"; return; }
    IMG="$D/hd.img"
    qemu-img create -f raw "$IMG" 40M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
    dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    EXOS_ISTANZA=nano$bit EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
        timeout 400 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
        "/a/bin/nano -V@4" \
        "/a/bin/nano -I /a/prova.txt@5" "foto:$D/nano.ppm@1" \
        "ciao da nano su EX-OS@n1" "key:ctrl-o@2" "key:ret@2" "key:ctrl-x@3" \
        "cat /a/prova.txt@3" \
        "/a/bin/nano --rcfile=/a/nano/prova.rc /a/prova.c@6" "foto:$D/sintassi.ppm@1" \
        "key:ctrl-x@3" "umount /a@3" > "$D/qemu.log" 2>&1
    S="$D/seriale.txt"
    cp "/tmp/exos/serialnano$bit.txt" "$S" 2>/dev/null
    tr -d '\r' < "$D/qemu.log" >> "$S"
    python3 tools/schermo_testo.py -i "$D/nano.ppm" > "$D/schermo.txt" 2>&1
    python3 tools/schermo_testo.py -i "$D/sintassi.ppm" > "$D/sintassi.txt" 2>&1
    grep -aq "GNU nano, version 9.3" "$S" && si "$bit: nano -V" || no "$bit: nano -V (vedi $S)"
    grep -q "GNU nano 9.3" "$D/schermo.txt" && si "$bit: la barra del titolo" || no "$bit: barra del titolo ($D/schermo.txt)"
    grep -q "\^X Exit" "$D/schermo.txt" && si "$bit: le righe d'aiuto" || no "$bit: righe d'aiuto"
    grep -aqx "ciao da nano su EX-OS" "$S" && si "$bit: ^O ha salvato, cat la rilegge" || no "$bit: il file salvato non c'e'"
    grep -q "return 0;" "$D/sintassi.txt" && ! grep -qi "error\|errore\|unknown\|No such" "$D/sintassi.txt" \
        && si "$bit: nanorc con la sintassi, prova.c aperto" || no "$bit: nanorc ($D/sintassi.txt)"
    grep -aq "\[FAULT\]\|PANIC" "$S" && no "$bit: [FAULT] o PANIC ($S)" || si "$bit: nessun FAULT"
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
