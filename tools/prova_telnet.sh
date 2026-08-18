#!/bin/bash
# =============================================================================
# tools/prova_telnet.sh — la prova che conta per telnetd: un client VERO
#
# Accende EX-OS col CD, aspetta che telnetd ascolti, si collega con il telnet
# dell'ospite attraverso un inoltro di porta, RIDIMENSIONA il terminale e
# guarda se la misura e' arrivata dall'altra parte (NAWS, RFC 1073).
#
# ! LA RETE DI QEMU DEVE STARE NELLA STESSA /24 DEL FILE DI CONFIGURAZIONE.
# /boot/telnetd.cfg ammette 10.0.0.0/24; l'utente-mode di QEMU predefinito da'
# all'ospite 10.0.2.2, che NON ci sta dentro — e telnetd rifiuta, giustamente.
# Si e' perso un giro a capirlo: la riga «rifiutato: 10.0.2.2 non e' fra gli
# indirizzi ammessi» diceva la verita' e sembrava un difetto della prova.
# Per questo la rete si sposta con net=/host=/dhcpstart=.
#
# ! E LA PORTA DELL'OSPITE NON E' LA 23: e' la 2323, per non finire addosso a
# un telnet vero.
#
# ! UNA MACCHINA SOLA PER VOLTA, come per prova_ssh.sh.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
pkill -9 -f qemu-system 2>/dev/null
sleep 2
rm -f /tmp/exos/serialtn.txt

EXOS_ISTANZA=tn EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_ATTESA_FINALE=90 \
EXOS_QEMU_EXTRA="-netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15,hostfwd=tcp::2323-:23 -device ne2k_pci,netdev=n1" \
timeout 200 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "telnetd -s -v@10" \
    > /tmp/exos/tn.log 2>&1 &
QPID=$!

for i in $(seq 1 40); do
    grep -aq "telnetd: in ascolto" /tmp/exos/serialtn.txt 2>/dev/null && break
    sleep 3
done
sleep 2

timeout 60 python3 tools/telnet_client_prova.py 2323 > /tmp/exos/tnclient.log 2>&1

echo "=== SERVER ==="
grep -a "telnetd:" /tmp/exos/serialtn.txt 2>/dev/null | tr -d '\000' | sed 's/\x1b\[[0-9;]*m//g' | tail -12
wait $QPID 2>/dev/null

echo "=== CLIENT ==="
tr -d '\000' < /tmp/exos/tnclient.log | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' | tail -8
