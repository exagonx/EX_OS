#!/bin/bash
# =============================================================================
# tools/prova-sh/prova-sh.sh -- sh 0.010 in QEMU: variables, if, test
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-sh/prova-sh.sh [32|64]      64 by default
#
# controlli.sh and aperto.sh go on an ext2 disk next to the CD; EX-OS runs
# `source /a/controlli.sh`, which prints one [OK] or [NO] per check, and the
# line "prova-sh: fine". Must print "=== [OK] ===".
# ! THE SHELL OF THE CD IS THE ONE CHECKED: rebuild the ISO after a change
# to bin/sh/shell.c (make iso-exos / make ARCH=x86_64 iso64).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
BITS=${1:-64}
SBIN=/usr/sbin
if [ "$BITS" = 64 ]; then
    ARCH=x86_64; ISO=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64 EXTRA="-cpu qemu64"
else
    ARCH=i386; ISO=dist/exos.iso; EXTRA=""
fi
D=/tmp/exos-prove/sh-$ARCH
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
rm -rf "$D"; mkdir -p "$D/albero"
cp tools/prova-sh/controlli.sh tools/prova-sh/aperto.sh "$D/albero/"
fakeroot sh -c "chown -R 0:0 '$D/albero'; chmod -R a+rwX '$D/albero';
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L prova -d '$D/albero' '$D/p1.img' 8192" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$D/hd.img" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=16384, type=83\n' | $SBIN/sfdisk "$D/hd.img" > /dev/null 2>&1
dd if="$D/p1.img" of="$D/hd.img" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$D/hd.img,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=sh$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "sh -version@3" "source /a/controlli.sh@~60" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
tr -d '\r' < "/tmp/exos/serialsh$ARCH.txt" > "$S" 2>/dev/null
esito=0
ok=$(grep -a -c '^\[OK\]' "$S"); no=$(grep -a -c '^\[NO\]' "$S")
grep -a '^\[NO\]' "$S" | sed 's/^/  /'
echo "  $ok OK, $no NO (attesi 22 OK)"
[ "$ok" = 22 ] && [ "$no" = 0 ] || esito=1
grep -aq '^\$X$' "$S" && echo "  ok nei '...' il \$ resta un dollaro" || { echo "  NO '\$X' fra apici"; esito=1; }
grep -aq '^\[OK\] 22 fra virgolette \$X e. un dollaro, "x" una virgoletta$' "$S" || { echo "  NO 22: le barre fra virgolette"; esito=1; }
grep -aq '6 \$? dopo false' "$S" || { echo "  NO 6: \\\$ fra virgolette non e' un dollaro"; esito=1; }
grep -aq "manca fi alla fine dello script" "$S" && echo "  ok lo script senza fi lo dice" || { echo "  NO messaggio 'manca fi'"; esito=1; }
grep -aq "dentro lo script rotto" "$S" || { echo "  NO lo script rotto non e' partito"; esito=1; }
grep -aq "^prova-sh: fine" "$S" || { echo "  NO non e' arrivato alla fine"; esito=1; }
grep -aq "0\.010" "$S" && echo "  ok sh 0.010" || { echo "  NO la shell del CD non e' la 0.010"; esito=1; }
[ $esito = 0 ] && echo "=== [OK] ===" || { echo "=== [NO] ==="; echo "  la seriale: $S"; }
exit $esito
