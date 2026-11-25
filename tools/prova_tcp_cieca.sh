#!/bin/bash
# =============================================================================
# tools/prova_tcp_cieca.sh - blind RST, fake ACKs and guessable ISNs against
# ip.drv's TCP (ip.drv 0.029, debug/bug_IP001)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_tcp_cieca.sh [32|64]
#
# EX-OS from the CD with telnetd -s on 10.9.0.1; on the other end of the
# cable tools/prova_tcp_cieca.py, which writes raw frames (QEMU's user
# network would end the TCP inside QEMU and drop what we forge). At the end
# ipcfg prints the counters: they must have moved, and only by what the
# script sent.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
if [ "${1:-32}" = 64 ]; then CD=dist/exos64.iso; QEMU=qemu-system-x86_64; else CD=dist/exos.iso; QEMU=qemu-system-i386; fi
mkdir -p /tmp/exos
S=/tmp/exos/serialtcpc.txt
rm -f "$S"
PORTA=$((27000 + RANDOM % 2000))

python3 tools/prova_tcp_cieca.py $PORTA "$S" > /tmp/exos/tcpc.log 2>&1 &
PY=$!
sleep 1

EXOS_ISTANZA=tcpc EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_QEMU=$QEMU EXOS_RAM=128M \
EXOS_NET_SOCKET=connect:$PORTA EXOS_NIC=e1000 EXOS_MAC=52:54:00:12:34:0c \
    timeout 300 python3 tools/qemu_drive.py "ipcfg -a 10.9.0.1 -m 255.255.255.0 -d 10.9.0.2@3" \
    "telnetd -s -v &@60" "host prova.exos@8" "host alias.exos@8" "host prova.exos@8" "ipcfg@4" "echo FINE-TCPC@2" > /tmp/exos/tcpc-qemu.log 2>&1
wait $PY
r=$?
cat /tmp/exos/tcpc.log
echo "=== ip.drv ==="
tr -d '\r\000' < "$S" | sed 's/\x1b\[[0-9;]*m//g' | grep -a "ip.drv:\|TCP fuori\|TCP rimandati" | tail -6
exit $r
