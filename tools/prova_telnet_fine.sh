#!/bin/bash
# =============================================================================
# tools/prova_telnet_fine.sh [32|64] - a telnet client must hear the end
#
# See tools/telnet_fine_prova.py. Commands at the console are timed for two
# boots: the second telnetd must be up after `guardia` has restarted the machine.
# One machine at a time, as for prova_telnet.sh.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
TN_PORTA="${TN_PORTA:-2323}"
if ss -Hltn "sport = :$TN_PORTA" | grep -q .; then
    echo "NON RIUSCITO: la porta $TN_PORTA dell'host e' gia' occupata"
    exit 2
fi
case "$1" in
    64) iso=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M ;;
    *)  iso=dist/exos.iso;   export EXOS_RAM=128M ;;
esac
log=/tmp/exos/serialtnf.txt
rm -f "$log"
export EXOS_RIAVVIO=1 EXOS_ISTANZA=tnf EXOS_NO_FLOPPY=1 EXOS_CDROM="$iso" EXOS_ATTESA_FINALE=120
export EXOS_QEMU_EXTRA="-netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15,hostfwd=tcp::$TN_PORTA-:23 -device ne2k_pci,netdev=n1"
timeout 330 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "telnetd -s -v@45" \
    "netdetect -c@12" "dhcp@8" "telnetd -s -v@10" > /tmp/exos/tnf.log 2>&1 &
QPID=$!
timeout 320 python3 tools/telnet_fine_prova.py "$TN_PORTA" "$log"
rc=$?
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null
exit $rc
