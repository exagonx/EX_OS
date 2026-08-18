#!/bin/bash
# =============================================================================
# tools/prova_ssh.sh — la prova che conta per sshd: un client VERO
#
# Accende EX-OS col CD, aspetta che sshd ascolti, si collega con OpenSSH
# dall'host attraverso un inoltro di porta, batte un comando e stampa cio' che
# hanno visto le due parti.
#
# ! LE PROVE DI RETE VOGLIONO UNA MACCHINA SOLA PER VOLTA. Un QEMU rimasto
# acceso da un giro precedente tiene il log e la porta, e si finisce per
# leggere l'output di ieri credendolo di oggi — e' costato mezz'ora il
# 18 agosto, con un'entropia che sembrava deterministica e non lo era. Per
# questo la prima cosa che fa e' ammazzare quello che trova.
#
# ! E LA PORTA DELL'HOST NON E' LA 22: e' la 2225, per non finire addosso a un
# ssh vero dell'ospite.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
pkill -9 -f qemu-system 2>/dev/null
sleep 2
rm -f /tmp/exos/serialpr.txt /tmp/exos/pr.pcap

EXOS_ISTANZA=pr EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso \
EXOS_ATTESA_FINALE=160 \
EXOS_QEMU_EXTRA="-netdev user,id=n1,hostfwd=tcp::2225-:22 -device ne2k_pci,netdev=n1 -object filter-dump,id=f1,netdev=n1,file=/tmp/exos/pr.pcap" \
timeout 200 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "sshd -s -v@15" \
    > /tmp/exos/pr.log 2>&1 &
QPID=$!

for i in $(seq 1 40); do
    grep -aq "in ascolto sulla porta" /tmp/exos/serialpr.txt 2>/dev/null && break
    sleep 3
done

cd "$(dirname "$0")"

timeout 55 python3 ssh_client_prova.py > /tmp/exos/client.log 2>&1

echo "=== SERVER ==="
grep -a "sshd:" /tmp/exos/serialpr.txt 2>/dev/null | tr -d '\000' | sed 's/\x1b\[[0-9;]*m//g' | tail -14
wait $QPID 2>/dev/null

echo "=== CLIENT ==="
tr -d '\000' < /tmp/exos/client.log | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' | tail -12
