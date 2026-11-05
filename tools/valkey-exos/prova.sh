#!/bin/bash
# =============================================================================
# tools/valkey-exos/prova.sh — Valkey on EX-OS, in QEMU (voce 33 i)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/valkey-exos/prova.sh [32|64]      64 by default
#
# cross_build/valkey-exos<bit>/exos/valkey (tools/valkey-exos/costruisci.sh)
# on an ext2 disk next to the CD, the shared libraries it names in /a/lib.
# Inside EX-OS:
#   1. valkey-server in the background, its data in /a/valkey/dati;
#   2. PING, SET and GET with valkey-cli; DEBUG POPULATE 200000 keys;
#   3. BGSAVE: the server forks, the child writes dump.rdb from the memory
#      it was born with; meanwhile valkey-benchmark loads the parent (SET and
#      GET, 20000 requests): copy on write under a real load;
#   4. INFO persistence: the save ended well; valkey-check-rdb reads it;
#   5. SHUTDOWN, a new server, DBSIZE: the 200001 keys came back from disk;
#   6. BGREWRITEAOF (the second fork), SHUTDOWN.
# Must print "=== [OK] ===". Wants dist/exos.iso / dist/exos64.iso with
# kernel 0.317 or later (fork, the alarm, the restart of interrupted calls).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
BITS=${1:-64}
X=cross_build/nasi5deb
SBIN=/usr/sbin
if [ "$BITS" = 64 ]; then
    ARCH=x86_64; ISO=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=${EXOS_RAM:-1024M} EXTRA="-cpu qemu64"
else
    ARCH=i386; ISO=dist/exos.iso
    export EXOS_RAM=${EXOS_RAM:-1024M} EXTRA=""
fi
V=cross_build/valkey-exos$BITS/exos/valkey
D=/tmp/exos-prove/valkey-$ARCH
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x "$V/bin/valkey-server" ] || { echo "manca $V: tools/valkey-exos/costruisci.sh $BITS" >&2; exit 1; }
rm -rf "$D"; mkdir -p "$D"
T="$D/albero"
IMG="$D/hd.img"
mkdir -p "$T/lib"
cp -a "$V" "$T/valkey"
# the paths of the configuration on the test disk, as tools/prova_httpd.sh does
sed -i 's#/exos/valkey#/a/valkey#g' "$T/valkey/etc/valkey.conf"
# the shared libraries the programs name, from the ones built here
for l in $(for f in "$T"/valkey/bin/*; do readelf -d "$f" 2>/dev/null; done |
           sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort -u); do
    f=$(ls cross_build/*-exos$BITS/exos/lib/"$l" 2>/dev/null | head -1)
    [ -n "$f" ] || { echo "manca $l (per valkey a $BITS bit)" >&2; exit 1; }
    cp "$f" "$T/lib/"
done
fakeroot sh -c "chown -R 0:0 '$T'; chmod -R a+rX '$T'; chmod 777 '$T/valkey/dati';
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L valkey -d '$T' '$D/p1.img' 98304" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 100M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=196608, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"

B=/a/valkey/bin
CLI="$B/valkey-cli"
SERVER="$B/valkey-server /a/valkey/etc/valkey.conf --logfile /a/valkey/dati/valkey.log --enable-debug-command yes --save 3600 1"
echo "=== $ARCH ==="
EXOS_ISTANZA=vk$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 1500 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "export LD_LIBRARY_PATH=/a/lib@1" \
    "$SERVER &@10" "ps@5" \
    "$CLI ping@~20" "$CLI set chiave valore@~20" "$CLI get chiave@~20" \
    "$CLI debug populate 200000@~120" "$CLI dbsize@~20" \
    "$CLI bgsave@~20" \
    "$B/valkey-benchmark -q -n 20000 -c 20 -t set,get@~300" \
    "sleep 3000@~10" "$CLI info persistence@~20" "ps@5" \
    "ls -a -l /a/valkey/dati@5" \
    "$B/valkey-check-rdb /a/valkey/dati/dump.rdb@~120" \
    "$CLI shutdown@~60" "sleep 2000@~10" \
    "$SERVER &@20" "$CLI dbsize@~60" \
    "$CLI bgrewriteaof@~20" "sleep 8000@~20" "$CLI info persistence@~20" \
    "$CLI shutdown nosave@~60" "sleep 2000@~10" "ps@5" \
    "cat /a/valkey/dati/valkey.log@10" "umount /a@5" > "$D/qemu.log" 2>&1
# ! ONLY THE SERIAL: qemu.log repeats the same lines, and a count (the
# 200001 keys seen twice) would pass with one sighting
S="$D/seriale.txt"
tr -d '\r' < "/tmp/exos/serialvk$ARCH.txt" > "$S" 2>/dev/null

esito=0
si()  { echo "  ok $1"; }
no()  { echo "  NO $1"; esito=1; }
grep -aq "^PONG" "$S" && si "valkey-server risponde (PING)" || no "PING"
grep -aq "^\"\?valore\"\?$" "$S" && si "SET e GET" || no "SET e GET"
grep -a "^(integer) 200001" "$S" | head -1 | grep -q . && si "DEBUG POPULATE: 200001 chiavi" || no "DBSIZE dopo POPULATE"
grep -aq "Background saving started" "$S" && si "BGSAVE partito (fork del server)" || no "BGSAVE"
grep -aq "SET: [0-9.]* requests per second" "$S" && grep -aq "GET: [0-9.]* requests per second" "$S" && si "valkey-benchmark durante il salvataggio: $(grep -ao '[SG]ET: [0-9.]* requests per second' "$S" | sed 's/requests per second/al secondo/' | tr '\n' ' ')" \
    || no "valkey-benchmark"
grep -aq "rdb_last_bgsave_status:ok" "$S" && si "il figlio ha finito il salvataggio (rdb_last_bgsave_status:ok)" || no "rdb_last_bgsave_status"
grep -aq "Background saving terminated with success" "$S" && si "il padre ha raccolto il figlio (terminated with success)" \
    || no "il log non dice terminated with success"
grep -aiq "RDB looks OK" "$S" && si "valkey-check-rdb: dump.rdb e' sano" || no "valkey-check-rdb"
# after the restart: the keys it says it loaded, DBSIZE says the same, and
# they are at least the 200001 (SHUTDOWN saves again, with the benchmark's
# key:__rand_int__ too: 200002)
caricate=$(grep -ao "keys loaded: [0-9]*" "$S" | tail -1 | sed 's/.*: //')
if [ -n "$caricate" ] && [ "$caricate" -ge 200001 ] && grep -aq "^(integer) $caricate$" "$S" &&
   [ "$(grep -a -c '^(integer) 20000[0-9]$' "$S")" -ge 2 ]; then
    si "riavviato, le $caricate chiavi tornano dal disco"
else
    no "le chiavi dopo il riavvio (caricate: ${caricate:-nessuna})"
fi
grep -aq "Background append only file rewriting started" "$S" && si "BGREWRITEAOF partito (seconda fork)" || no "BGREWRITEAOF"
grep -aq "aof_last_bgrewrite_status:ok" "$S" && si "la riscrittura dell'AOF e' finita bene" || no "aof_last_bgrewrite_status"
grep -aq "^smontato /a" "$S" && si "umount /a (nessun file rimasto aperto)" || no "umount /a"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] && echo "=== [OK] ===" || { echo "=== [NO] ==="; echo "  la seriale: $S"; }
exit $esito
