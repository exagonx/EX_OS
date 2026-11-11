#!/bin/bash
# =============================================================================
# tools/prova_exwin_presto.sh - exwin typed while the boot jobs still run (voce 97)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_exwin_presto.sh          32 and 64 bit
#     tools/prova_exwin_presto.sh 64       only one
#     QEMU_PIU="-smp 2" ...                two processors
#
# Seen on 9/11 with prova-lampi: `exwin` right after the boot, while
# ehci.drv -avvio, ohci.drv -avvio and sis.drv -2dservizio ran, came back to
# the prompt without one line (every exit of exwin prints one) and without
# wserver. Here: exwin at once, then ps; after the jobs, exwin again.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ok=0; no=0
KVM=""; [ -w /dev/kvm ] && [ -z "${LENTO:-}" ] && KVM="-enable-kvm"
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 iso q D L S cpu=""
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64; cpu="-cpu qemu64"
    else                     iso=dist/exos.iso;   q=qemu-system-i386; fi
    D=/tmp/exos-prove/exwin-presto$bit; rm -rf "$D"; mkdir -p "$D"
    echo "=== $bit bit ${QEMU_PIU:+($QEMU_PIU)} ==="
    # A disk as in prova-lampi: mounted, and the exports pointing into it.
    mkdir -p "$D/s/casa" "$D/s/font" "$D/s/lib"
    "$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 4096 -L presto -d "$D/s" "$D/p.img" 64M || return
    qemu-img create -f raw "$D/hd.img" 66M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
        | "$(command -v sfdisk || echo /sbin/sfdisk)" "$D/hd.img" > /dev/null 2>&1
    dd if="$D/p.img" of="$D/hd.img" bs=1M seek=1 conv=notrunc status=none
    EXOS_CDROM=$iso EXOS_QEMU=$q EXOS_ISTANZA=ep$bit EXOS_NO_FLOPPY=1 EXOS_RAM=1024M \
    EXOS_QEMU_EXTRA="$KVM $cpu ${QEMU_PIU:-} -drive file=$D/hd.img,format=raw,if=ide" \
        timeout 900 python3 tools/qemu_drive.py \
        "mount hd0p1 /disk@1" "export MOZ_FORCE_DISABLE_E10S=1@0" "export HOME=/disk/casa@0" \
        "export LANG=it_IT.UTF-8@0" "export EXILLA_FONTS=/disk/font@0" "export LD_LIBRARY_PATH=/disk/lib@0" \
        "echo PRIMA-EXWIN@0" "exwin@14" "echo DOPO-EXWIN \$?@2" "key:alt-f1@2" "ps@4" \
        "foto:$D/prima.ppm@1" \
        > "$D/qemu.log" 2>&1
    L=$(tr -d '\r' < "$D/qemu.log" | sed 's/\x1b\[[0-9;]*m//g')
    S=$(tr -d '\r' < /tmp/exos/serialep$bit.txt 2>/dev/null)
    echo "$L" | sed -n '/PRIMA-EXWIN/,/DOPO-EXWIN/p' | grep -av "^$" | sed 's/^/      /' | head -20
    if echo "$L" | sed -n '/PRIMA-EXWIN/,/DOPO-EXWIN/p' | grep -aq "^exwin: "; then
        si "$bit: exwin presto dice qualcosa"
    else no "$bit: exwin presto torna senza una riga (voce 97)"; fi
    echo "$L" | grep -aq "wserver" && si "$bit: wserver gira" || no "$bit: wserver non c'e' nel ps"
    echo "$S" | grep -a "FAULT\|terminato per\|exwin\|wserver" | head -8 | sed 's/^/      seriale: /'
}

if [ $# -ge 1 ]; then giro "$1"; else giro 32; giro 64; fi
echo "=== $ok riusciti, $no no ==="
[ "$no" = 0 ]
