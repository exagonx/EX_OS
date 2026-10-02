#!/bin/bash
# =============================================================================
# tools/exilla/prova-nss.sh — NSS dentro EX-OS (tappa 5 di Exilla)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Prende certutil e tstclnt costruiti da tools/exilla/nss-costruisci.sh e in
# EX-OS, con la rete di QEMU:
#   1. certutil crea un database di certificati (SQLite, il softoken);
#   2. certutil fa un certificato autofirmato RSA (freebl, la casualita');
#   3. certutil lo elenca;
#   4. tstclnt apre una connessione TLS al server di
#      tools/exilla/prova-nss-host.py e legge la risposta, che dice versione e
#      cifrario negoziati.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
N="$B/costruzione-nss/dist/Release/bin"
D="$B/costruzione-prova"
mkdir -p "$D" /tmp/exos
for p in certutil tstclnt; do [ -x "$N/$p" ] || { echo "manca $N/$p: tools/exilla/nss-costruisci.sh" >&2; exit 1; }; done

printf 'GET / HTTP/1.0\r\nHost: 10.0.0.2\r\n\r\n' > "$D/richiesta.txt"
head -c 2048 /dev/urandom > "$D/rumore"                 # per certutil -z

python3 tools/exilla/prova-nss-host.py "$D" > "$D/nss-host.log" 2>&1 &
HOST=$!
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
IMG="$D/nss-hd.img"
CD="$B/costruzione-sistema/albero/dist/exos.iso"; [ -f "$CD" ] || CD="$PWD/dist/exos.iso"
rm -f "$IMG"; qemu-img create -f raw "$IMG" 48M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
export EXOS_ISTANZA=nss EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=128M
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/nss-0.log" 2>&1
for f in "$N/certutil" "$N/tstclnt" "$D/richiesta.txt" "$D/rumore"; do
    "$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $f $(basename "$f")" \
        "$IMG?offset=1048576" > /dev/null 2>&1
done
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
rm -f /tmp/exos/serialnss.txt
timeout 600 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "mount hd0p1 /disk@5" \
    "mkdir /disk/db@2" \
    "/disk/certutil -N --empty-password -d sql:/disk/db@30" \
    "/disk/certutil -S -n prova -s CN=exos -x -t CT,, -k rsa -g 1024 -z /disk/rumore -d sql:/disk/db@90" \
    "/disk/certutil -L -d sql:/disk/db@30" \
    "/disk/tstclnt -h 10.0.0.2 -p 7803 -o -d sql:/disk/db < /disk/richiesta.txt@60" \
    "echo FINE-NSS@2" > "$D/nss-1.log" 2>&1
kill $HOST 2>/dev/null

S=$(tr -d '\r' < /tmp/exos/serialnss.txt | sed 's/\x1b\[[0-9;]*m//g')
echo "$S" | sed -n '/certutil -N/,/FINE-NSS/p' | grep -v "^\s*$" | tail -30
echo "--- host:"; cat "$D/nss-host.log"
no=0
echo "$S" | grep -q "^prova *CTu,u,u\|^prova .*CT" || { echo "[NO] certutil -L non elenca il certificato"; no=1; }
echo "$S" | grep -q "EXILLA TLS OK" || { echo "[NO] nessuna risposta TLS"; no=1; }
[ $no = 0 ] && { echo "=== [OK] NSS gira dentro EX-OS: database, certificato, TLS ==="; exit 0; }
echo "=== [NO] vedi $D/nss-1.log e /tmp/exos/serialnss.txt ==="; exit 1
