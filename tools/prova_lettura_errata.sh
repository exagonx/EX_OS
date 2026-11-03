#!/bin/bash
# =============================================================================
# tools/prova_lettura_errata.sh — a disk error is an error (voce 59, kernel 0.308)
#
#     tools/prova_lettura_errata.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_lettura_errata.sh [dir]    64 bit
#     EXOS_ISO=old.iso tools/prova_lettura_errata.sh [dir]    another CD
#
# QEMU's blkdebug fails chosen sectors of an ext2 made by Linux:
#   dati.bin    a data block in the middle of the file
#   ind.bin     the indirect block
#   ls3         a page of a program
# Before 0.308 the first came out cut short as if it had ended, the second full
# of zeros with a good exit, the third ran with zeros in place of code.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/lettura-$ARCH}"
SBIN=/usr/sbin; [ -x /sbin/mke2fs ] && SBIN=/sbin
rm -rf "$D"; mkdir -p "$D/albero"
if [ "$ARCH" = x86_64 ]; then
    ISO=${EXOS_ISO:-dist/exos64.iso}; LS=build-64/iso-exos/bin/ls
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M EXTRA="-cpu qemu64"
else
    ISO=${EXOS_ISO:-dist/exos.iso}; LS=build/iso-exos/bin/ls
    export EXOS_RAM=64M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

python3 -c "
import sys
b=bytes((i*7+1)%251+1 for i in range(251))*2000
open('$D/albero/buono.bin','wb').write(b[:400000])
open('$D/albero/dati.bin','wb').write(b[:400000])
open('$D/albero/ind.bin','wb').write(b[:400000])"
cp "$LS" "$D/albero/ls3"; chmod 755 "$D/albero/ls3"
IMG="$D/disco.img"; O=2048; S=65536
qemu-img create -f raw "$IMG" 40M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\n' $O $S | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if=/dev/zero of="$D/p.img" bs=512 count=$S status=none
$SBIN/mke2fs -q -F -t ext2 -b 1024 -d "$D/albero" "$D/p.img" || exit 1
blocco() { $SBIN/debugfs -R "bmap $1 $2" "$D/p.img" 2>/dev/null; }
dd if="$D/p.img" of="$IMG" bs=512 seek=$O conv=notrunc status=none
DATI=$(blocco /dati.bin 100)
PROG=$(blocco /ls3 16)
# the indirect block of a file on 1 KB blocks: 13th entry of the inode
IND=$($SBIN/debugfs -R "stat /ind.bin" "$D/p.img" 2>/dev/null | grep -o '(IND):[0-9]*' | cut -d: -f2)
[ -n "$DATI" ] && [ -n "$IND" ] && [ -n "$PROG" ] || { echo "blocchi non trovati"; exit 1; }
{
  for b in $DATI $IND $PROG; do
    printf '[inject-error]\nevent = "read_aio"\nerrno = "5"\nsector = "%d"\nonce = "off"\n\n' $((O + b * 2))
  done
} > "$D/guasti.cfg"
echo "guasti: dati $DATI, indiretto $IND, programma $PROG"

export EXOS_ISTANZA=le$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=blkdebug:$D/guasti.cfg:$IMG,format=raw,if=ide"
timeout 400 python3 tools/qemu_drive.py "mount -r hd0p1 /a@8" \
    "echo R1@1" "sha256sum /a/buono.bin@6" "echo R2@1" "sha256sum /a/dati.bin@25" \
    "echo R3@1" "sha256sum /a/ind.bin@25" "echo R4@1" "/a/ls3 /boot@25" \
    "echo R5@1" "dmesg@3" "echo R6@1" > "$D/a.log" 2>&1
tr -d '\r' < "$D/a.log" | sed 's/\x1b\[[0-9;]*m//g' > "$D/pulito.log"
parte() { awk -v a="R$1" -v b="R$(( $1 + 1 ))" '$0 == a {s=1; next} $0 == b {s=0} s' "$D/pulito.log"; }
BUONO=$(sha256sum "$D/albero/buono.bin" | cut -c1-64)
parte 1 | grep -q "$BUONO" && si "file sano: impronta giusta" || no "file sano (vedi $D/pulito.log)"
parte 2 | grep -qE '[0-9a-f]{64}' \
    && no "blocco dati guasto: impronta data lo stesso, come fine file" \
    || si "blocco dati guasto: nessuna impronta, errore"
parte 3 | grep -qE '[0-9a-f]{64}' \
    && no "blocco indiretto guasto: impronta data lo stesso, zeri come dati" \
    || si "blocco indiretto guasto: nessuna impronta, errore"
parte 5 | grep -q "PF: PID .*fallita" \
    && si "programma guasto: fermato dal kernel, che lo dice" \
    || no "programma guasto: nel registro nessun PF"
echo "=== $ok riusciti, $no no ==="
[ "$no" -eq 0 ]
