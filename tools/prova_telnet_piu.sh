#!/bin/bash
# =============================================================================
# tools/prova_telnet_piu.sh - telnetd serves several sessions at once
# (telnetd 0.015, voce 62). Up to 0.014 a second connection stayed mute
# until the first ended.
#
#     tools/prova_telnet_piu.sh          32 bit
#     tools/prova_telnet_piu.sh 64       64 bit
#
# Same network as prova_telnet.sh (10.0.0.0/24, host port 2340 -> 23), then
# tools/telnet_piu_prova.py: three sessions together, one closed, a fourth.
# telnetd -s -v: the shell without login, so the test needs no password.
# telnetd 0.016 (voci 21 and 100): a session's dmesg -segui & go with it.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=${1:-32}
TN_PORTA="${TN_PORTA:-2340}"
if [ "$BIT" = 64 ]; then ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M
else                     ISO=dist/exos.iso;   export EXOS_RAM=256M; fi
D=/tmp/exos-prove/telnetpiu$BIT; rm -rf "$D"; mkdir -p "$D"
SER=/tmp/exos/serialtp$BIT.txt; rm -f "$SER"
if ss -Hltn "sport = :$TN_PORTA" | grep -q .; then
    echo "NON RIUSCITO: la porta $TN_PORTA dell'host e' occupata; TN_PORTA=<altra> $0" >&2
    exit 2
fi
EXOS_ISTANZA=tp$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" EXOS_ATTESA_FINALE=120 \
EXOS_QEMU_EXTRA="-netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15,hostfwd=tcp::$TN_PORTA-:23 -device ne2k_pci,netdev=n1" \
timeout 300 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "telnetd -s -v@10" \
    > "$D/qemu.log" 2>&1 &
QPID=$!
for i in $(seq 1 60); do
    grep -aq "telnetd: in ascolto" "$SER" 2>/dev/null && break
    sleep 3
done
sleep 2
timeout 120 python3 tools/telnet_piu_prova.py $TN_PORTA | tee "$D/client.log"
rc=${PIPESTATUS[0]}
echo "=== SERVER ==="
grep -a "telnetd:\|FAULT\|PANIC" "$SER" 2>/dev/null | tr -d '\000' | sed 's/\x1b\[[0-9;]*m//g' | tail -20
grep -aq "\[FAULT\]\|PANIC" "$SER" && { echo "  [NO] FAULT o PANIC sulla seriale"; rc=1; }
wait $QPID 2>/dev/null
[ $rc = 0 ] && echo "=== $BIT bit: A POSTO ===" || echo "=== $BIT bit: NON VA ==="
exit $rc
