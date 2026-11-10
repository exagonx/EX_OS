#!/bin/bash
# =============================================================================
# tools/prova-invio/prova-invio.sh - TCP sending from EX-OS to EX-OS over a
# line with a real delay (ip.drv 0.024: the send queue that grows and the
# congestion control, voce 1a of @CODA-20-10)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-invio/prova-invio.sh            64 bit (dist/exos64.iso)
#     tools/prova-invio/prova-invio.sh 32         32 bit (dist/exos.iso)
#
# Two EX-OS machines (e1000 each) joined by rele.py: 25 ms each way (50 ms
# round trip, like the repository's 47), 20 Mbit/s, a 100-frame router
# queue. Machine A receives with prova-invio -s, machine B sends BYTE bytes
# (2 MB, BYTE=...) with prova-invio -c. Cases, one boot pair each:
#
#   1. a clean line                      every byte right, the speed printed
#   2. 1% of the frames lost             every byte right (fast retransmit)
#   3. -W 4 on the sender (as up to 0.023) every byte right, for comparison
#   4. the sender's e1000.drv killed and started again, ip.drv linked to
#      the new one (ipcfg -l rete0): every byte right (voce 12)
#
# PASS = every case "prova-invio: OK" and no FAULT/panic. The speeds are
# printed, not judged: with 4 KB and 50 ms the ceiling is ~80 KB/s.
# RITARDO, KBIT, CODA change the line; CASI="1 2" chooses the cases.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""; CD="$RADICE/dist/exos.iso"; QEMU=qemu-system-i386; CPU=""
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$PATH"
else
    T=x86_64-exos; ARCH=x86_64; CD="$RADICE/dist/exos64.iso"; QEMU=qemu-system-x86_64; CPU="-cpu qemu64"
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross64/bin:$PATH"
fi
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-invio-$T"
BYTE="${BYTE:-2097152}"
mkdir -p "$D" /tmp/exos
$T-gcc -O2 -o "$D/prova-invio" tools/prova-invio/prova-invio.c || exit 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
SFDISK=$(command -v sfdisk || echo /sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /sbin/debugfs)

# a scratch ext2 disk with the program, one per machine
disco() {
    local img="$D/disco-$1.img"
    rm -f "$img"
    qemu-img create -f raw "$img" 16M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | "$SFDISK" "$img" > /dev/null 2>&1
    EXOS_ISTANZA=invio$1 EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_QEMU=$QEMU \
    EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$img,format=raw,if=ide" \
        timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/mkfs-$1.log" 2>&1
    "$DEBUGFS" -w -R "write $D/prova-invio prova-invio" "$img?offset=1048576" > /dev/null 2>&1
}
disco a; disco b

PORTA_A=$((25000 + RANDOM % 2000)); PORTA_B=$((PORTA_A + 1))
esito=0

caso() {   # caso N NOME PERDI IPDRV_B
    local n="$1" nome="$2" perdi="$3" opz="$4"
    python3 tools/prova-invio/rele.py $PORTA_A $PORTA_B --ritardo "${RITARDO:-25}" \
        --kbit "${KBIT:-20000}" --coda "${CODA:-100}" --perdi "$perdi" --traccia "$D/traccia-$n.txt" > "$D/rele-$n.log" 2>&1 &
    local rele=$!
    sleep 1
    local avvio_b=()
    # ! ip.drv WITH OTHER OPTIONS: the one started at boot is stopped and a
    # new one takes the card. The EX-OS shell has no $(...), and in QEMU the
    # boot always gives ip.drv the pid 15 (8 November 2026, IPDRV_PID): the
    # ps after it shows whether it was the right one.
    [ -n "$opz" ] && avvio_b=("kill ${IPDRV_PID:-15}@2" "/dev/ip.drv $opz &@4" "ps@2")
    # case 4: the card's driver (pid 14 at boot, E1000_PID) instead
    [ "$n" = 4 ] && avvio_b=("kill ${E1000_PID:-14}@3" "/dev/e1000.drv &@6" "ipcfg -l rete0@3" "ps@2")
    EXOS_ISTANZA=invioa EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_QEMU=$QEMU EXOS_RAM=256M \
    EXOS_NET_SOCKET=connect:$PORTA_A EXOS_NIC=e1000 EXOS_MAC=52:54:00:12:34:0a \
    EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$D/disco-a.img,format=raw,if=ide" \
        timeout 400 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" \
        "ipcfg -a 10.9.0.1 -m 255.255.255.0@3" \
        "/disk/prova-invio -s 5001 $BYTE@${SECONDI:-150}" "ipcfg@3" "echo FINE-INVIO@2" > "$D/qemu-a-$n.log" 2>&1 &
    local qa=$!
    sleep 3
    EXOS_ISTANZA=inviob EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_QEMU=$QEMU EXOS_RAM=256M \
    EXOS_NET_SOCKET=connect:$PORTA_B EXOS_NIC=e1000 EXOS_MAC=52:54:00:12:34:0b \
    EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$D/disco-b.img,format=raw,if=ide" \
        timeout 400 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "${avvio_b[@]}" \
        "ipcfg -a 10.9.0.2 -m 255.255.255.0@3" "ping 10.9.0.1@4" \
        "/disk/prova-invio -c 10.9.0.1 5001 $BYTE@${SECONDI:-150}" "ipcfg@3" "echo FINE-INVIO@2" > "$D/qemu-b-$n.log" 2>&1 &
    local qb=$!
    wait $qa $qb
    kill $rele 2>/dev/null; wait $rele 2>/dev/null
    local sa sb
    sa=$(tr -d '\r' < /tmp/exos/serialinvioa.txt | sed 's/\x1b\[[0-9;]*m//g')
    sb=$(tr -d '\r' < /tmp/exos/serialinviob.txt | sed 's/\x1b\[[0-9;]*m//g')
    echo "--- caso $n: $nome"
    echo "$sb" | grep -a "prova-invio: mandati\|prova-invio: .*connect\|write"
    echo "$sa" | grep -a "prova-invio: ricevuti\|prova-invio: OK\|prova-invio: NO"
    echo "$sb" | grep -a "TCP rimandati\|TCP code grandi"
    grep -a "rele:" "$D/rele-$n.log" | grep -v pronto
    if ! echo "$sa" | grep -aq "prova-invio: OK"; then echo "[NO] caso $n"; esito=1; fi
    if echo "$sa$sb" | grep -aq "\[FAULT\]\|PANIC\|panic"; then echo "[NO] caso $n: FAULT/panic"; esito=1; fi
}

for n in ${CASI:-1 2 3 4}; do
    case $n in
        1) caso 1 "linea pulita" 0 "" ;;
        2) caso 2 "1% dei frame persi" 10 "" ;;
        3) caso 3 "-W 4 sul mittente, come fino alla 0.023" 0 "-W 4" ;;
        4) caso 4 "e1000.drv ripartito sul mittente" 0 "" ;;
    esac
done
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
