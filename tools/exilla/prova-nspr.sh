#!/bin/bash
# =============================================================================
# tools/exilla/prova-nspr.sh — NSPR dentro EX-OS (tappa 5 di Exilla)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compila tools/exilla/prova-nspr.c contro l'NSPR costruita da mozbuild
# (tools/exilla/js-costruisci.sh build, con --enable-nspr-build) e la esegue
# ! Si collegano gli OGGETTI di NSPR: mozbuild fa l'archivio solo quando un
# programma lo usa, e la shell JS non lo usa.
# in EX-OS con la rete di QEMU; dall'altra parte l'eco di
# tools/exilla/prova-socket-host.py.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
O="$PWD/cross_build/exilla-obj/js"
D="$B/costruzione-prova"
mkdir -p "$D" /tmp/exos
L="$O/config/external/nspr"
"$B/exos-cross/bin/i386-exos-gcc" -O2 -Wall -march=pentium-mmx -DEXOS_SOLO_POSIX -DNO_NSPR_10_SUPPORT \
    -I "$O/dist/include/nspr" tools/exilla/prova-nspr.c \
    $(ls "$L"/pr/*.o "$L"/libc/*.o "$L"/ds/*.o) \
    -o "$D/prova-nspr" || exit 1
echo "prova-nspr: $(stat -c %s "$D/prova-nspr") byte"

python3 tools/exilla/prova-socket-host.py /tmp/exos/serialnspr.txt solo-eco > "$D/nspr-host.log" 2>&1 &
HOST=$!
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
IMG="$D/nspr-hd.img"
CD="$B/costruzione-sistema/albero/dist/exos.iso"; [ -f "$CD" ] || CD="$PWD/dist/exos.iso"
rm -f "$IMG"; qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
export EXOS_ISTANZA=nspr EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/nspr-0.log" 2>&1
"$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $D/prova-nspr prova-nspr" \
    "$IMG?offset=1048576" > /dev/null 2>&1
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
rm -f /tmp/exos/serialnspr.txt
timeout 400 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "mount hd0p1 /disk@5" \
    "/disk/prova-nspr@40" "echo FINE-NSPR@2" > "$D/nspr-1.log" 2>&1
kill $HOST 2>/dev/null

tr -d '\r' < /tmp/exos/serialnspr.txt | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/prova-nspr: NSPR/,/prova-nspr: [0-9]* NO/p'
tr -d '\r' < /tmp/exos/serialnspr.txt | grep -q "^prova-nspr: 0 NO" && { echo "=== [OK] NSPR gira dentro EX-OS ==="; exit 0; }
echo "=== [NO] vedi $D/nspr-1.log e /tmp/exos/serialnspr.txt ==="; exit 1
