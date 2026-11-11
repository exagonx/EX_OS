#!/bin/bash
# =============================================================================
# tools/prova_sottofondo.sh - a script and its background child, one file (voce 94)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_sottofondo.sh          32 and 64 bit
#     tools/prova_sottofondo.sh 64       only one
#     QEMU_PIU="-smp 2" ...              two processors
#
# Seen on the laptop (9/11): `sh /r12.sh > /r12.txt &` with `/dev/nforce.drv &`
# inside, and the lines of the two overwrote each other. POSIX: after fork
# parent and child share ONE open file description, so one offset, and their
# writes follow each other. Here: t.sh writes P01..P40 and starts f.sh in the
# background, which writes F01..F40; both into the same file, on ext2 (/a)
# and on FAT16 (/b). Every one of the 80 lines must be there once (the
# shell's own echo of each script line, "/a/t.sh> echo P01", and its job
# number "[1] 29" are skipped).
# And `mount` names /proc "proc" (voce 93, kernel 0.325).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 iso q D L i
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64
    else                     iso=dist/exos.iso;   q=qemu-system-i386; fi
    D=/tmp/exos-prove/sottofondo$bit; rm -rf "$D"; mkdir -p "$D/albero"
    { echo "echo P01"; echo "sh /a/f.sh &"
      for i in $(seq -w 2 40); do echo "echo P$i"; done; } > "$D/albero/t.sh"
    for i in $(seq -w 1 40); do echo "echo F$i"; done > "$D/albero/f.sh"
    fakeroot sh -c "chown -R 0:0 '$D/albero'; $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L sotto -d '$D/albero' '$D/p1.img' 4096" \
        > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
    rm -f "$D/p2.img"; $SBIN/mkfs.fat -C -F 16 -n SOTTO "$D/p2.img" 20480 > /dev/null || exit 1
    qemu-img create -f raw "$D/hd.img" 32M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=8192, type=83\nstart=10240, size=40960, type=6\n' | $SBIN/sfdisk "$D/hd.img" > /dev/null 2>&1
    dd if="$D/p1.img" of="$D/hd.img" bs=512 seek=2048 conv=notrunc status=none
    dd if="$D/p2.img" of="$D/hd.img" bs=512 seek=10240 conv=notrunc status=none
    echo "=== $bit bit ${QEMU_PIU:+($QEMU_PIU)} ==="
    EXOS_CDROM=$iso EXOS_QEMU=$q EXOS_ISTANZA=so$bit EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
    EXOS_QEMU_EXTRA="-drive file=$D/hd.img,format=raw,if=ide${QEMU_PIU:+ $QEMU_PIU}" \
        timeout 900 python3 tools/qemu_drive.py "root@4" "root@6" \
        "mount hd0p1 /a@5" "mount hd0p2 /b@5" "mount@3" \
        "sh /a/t.sh > /a/t.txt &@1" "sleep 8@10" \
        "sh /a/t.sh > /b/t2.txt &@1" "sleep 8@10" \
        "echo ===A===@1" "cat /a/t.txt@4" "echo ===B===@1" "cat /b/t2.txt@4" "echo ===C===@1" \
        "umount /a@4" "umount /b@4" > "$D/qemu.log" 2>&1
    L=$(tr -d '\r' < "$D/qemu.log" | sed 's/\x1b\[[0-9;]*m//g')
    for parte in A B; do
        local fine; [ $parte = A ] && fine=B || fine=C
        local testo; testo=$(echo "$L" | sed -n "/^===$parte===\$/,/^===$fine===\$/p" | grep -av "===")
        local manca=0 doppie=0 strane=0 r
        for r in $(seq -w 1 40 | sed 's/^/P/') $(seq -w 1 40 | sed 's/^/F/'); do
            local n; n=$(echo "$testo" | grep -ac "^$r\$")
            [ "$n" = 0 ] && manca=$((manca+1)); [ "$n" -gt 1 ] && doppie=$((doppie+1))
        done
        strane=$(echo "$testo" | grep -av "^[PF][0-9][0-9]\$" | grep -av "cat /\|ex-os\|^\s*\$\|^/a/[tf]\.sh> \|^\[[0-9]*\] [0-9]*\$" | wc -l)
        local dove; [ $parte = A ] && dove="ext2 (/a/t.txt)" || dove="FAT16 (/b/t2.txt)"
        if [ $manca = 0 ] && [ $doppie = 0 ] && [ "$strane" = 0 ]; then si "$bit: $dove, 80 righe su 80"
        else no "$bit: $dove: $manca mancano, $doppie doppie, $strane righe strane (vedi $D/qemu.log)"; fi
    done
    # voce 93 too, while we are here: /proc named, not "FAT4294967295"
    if echo "$L" | grep -aq "^/proc .* proc  *sola lettura" && ! echo "$L" | grep -aq "FAT4294967295"; then
        si "$bit: mount scrive /proc come proc (voce 93)"
    else no "$bit: mount e /proc (voce 93): $(echo "$L" | grep -a '^/proc' | head -1)"; fi
    grep -aq "FAULT\|KERNEL PANIC" /tmp/exos/serialso$bit.txt 2>/dev/null && no "$bit: [FAULT] sulla seriale" || si "$bit: niente FAULT"
}

if [ $# -ge 1 ]; then giro "$1"; else giro 32; giro 64; fi
echo "=== $ok riusciti, $no no ==="
[ "$no" = 0 ]
