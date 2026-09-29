#!/bin/bash
# =============================================================================
# tools/exilla/prova-socket.sh — @SOCKET-BSD: i socket BSD dentro EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compila tools/exilla/prova-socket.c col gcc i386-exos di questa macchina
# (libc.a rifatta da tools/gcc-exos/prepara-cross.sh: i socket stanno nella
# libc.a, non nella libc.so), accende EX-OS in QEMU con la rete «user» —
# l'host e' 10.0.0.2, la 7700 dell'host arriva alla 7000 di EX-OS — e tiene
# dall'altra parte tools/exilla/prova-socket-host.py.
#
# Il CD e' quello della copia privata (tools/exilla/costruisci-privato.sh)
# se c'e', se no quello di dist/.
#
#     tools/exilla/prova-socket.sh [binario nome]
#
# Senza argomenti prova tools/exilla/prova-socket.c; con un binario e il suo
# nome (per esempio quello di tools/rust-exos/prova-rete.sh) prova quello, con
# lo stesso host dall'altra parte. Il programma stampa «<nome>: N NO» in fondo e
# «IN ASCOLTO 7000» quando aspetta l'host.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TCB="$PWD/cross_build/$MACCHINA/exos-cross/bin"
D="$PWD/cross_build/$MACCHINA/costruzione-prova"
CD="$PWD/cross_build/$MACCHINA/costruzione-sistema/albero/dist/exos.iso"
[ -f "$CD" ] || CD="$PWD/dist/exos.iso"
IMG="$D/socket-hd.img"
SER=/tmp/exos/serialsocket.txt
mkdir -p "$D" /tmp/exos

BIN="$1"; NOME="${2:-prova-socket}"
if [ -z "$BIN" ]; then
    "$TCB/i386-exos-gcc" -O2 -Wall tools/exilla/prova-socket.c -o "$D/prova-socket" || exit 1
    BIN="$D/prova-socket"
fi

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=socket EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/socket-0.log" 2>&1
"$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $BIN $NOME" \
    "$IMG?offset=1048576" > /dev/null 2>&1

export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15,hostfwd=tcp::7700-:7000 -device ne2k_pci,netdev=n1"
rm -f "$SER"
python3 tools/exilla/prova-socket-host.py "$SER" > "$D/socket-host.log" 2>&1 &
HOST=$!
timeout 400 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "mount hd0p1 /disk@5" \
    "/disk/$NOME@90" "echo FINE-SOCKET@2" > "$D/socket-1.log" 2>&1
wait $HOST; esito_host=$?

tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' | sed -n "\#/disk/$NOME#,/^$NOME: [0-9]* NO/p" | sed '1d'
echo "--- host:"; cat "$D/socket-host.log"
if tr -d '\r' < "$SER" | grep -q "^$NOME: 0 NO" && [ $esito_host = 0 ]; then
    echo "=== [OK] i socket BSD girano dentro EX-OS ==="; exit 0
fi
echo "=== [NO] vedi $D/socket-1.log e $SER ==="; exit 1
