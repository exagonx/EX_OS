#!/bin/bash
# =============================================================================
# tools/prova_mariadb_netinst.sh — MariaDB from netinst, the way a user gets it
#
#     tools/prova_mariadb_netinst.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_mariadb_netinst.sh [dir]    64 bit
#
# dist/netinst (dist/netinst_64), made by `make netinst` / `make netinst64`,
# served inside QEMU only by tools/prova_netupdate_http.py (guestfwd to
# 10.0.2.100, no socket on the host), as in tools/prova_sorgenti.sh.
#   1. a disk installed from the CD (tools/mkhd.sh), with QEMU's e1000
#   2. netupdate -install:mariadb-connector, the connectors alone (the user,
#      26 October 2026: two entries, the whole server or only the
#      connectors): libmariadbclient and the headers come, mariadbd does
#      not; then netupdate -install:mariadb: the server too;
#      netupdate -install adds /exos/bin to /boot/percorsi.txt, which the
#      shell reads at start, and (0.035) to the PATH of kernel.cfg, where
#      collect2 looks for ld
#   3. mariadb-svc -install by its name (the PATH), a table through the
#      client; tools/prove/connprova.c compiled INSIDE EX-OS by the gcc of
#      [base] against the connector, and run against the server; reboot
#   4. the server ready before the login page, the row still there,
#      mariadb-svc -ferma, and from Linux "Shutdown complete" in the log
# The same service without a network card is tools/prova_mariadb_svc.sh.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/mariadb-netinst-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; SRV=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            SRV=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
grep -q "^\[mariadb\]" "$SRV/catalogo.txt" 2>/dev/null || { echo "manca [mariadb] in $SRV: make netinst" >&2; exit 1; }
IMG="$D/hd.img"
OFF="$IMG?offset=1048576"

echo "=== 1. disco installato dal CD ($ARCH) ==="
sed "s|^IMG=dischi/hd.img|IMG=$IMG|; s|^MB=\"\${1:-512}\"|MB=\"\${1:-2048}\"|; \
     s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=mdn$ARCH EXOS_SUPPORTO=cd bash "$D/mkhd.sh" > "$D/1-disco.log" 2>&1 || {
    echo "NON RIUSCITO: il disco non si e' installato (vedi $D/1-disco.log)"; exit 1; }

# the root is dist/: netupdate asks for .../netinst, with "_64" on the 64-bit system
# the C program for step 3, in /root of the installed disk
$SBIN/debugfs -w -R "write tools/prove/connprova.c /root/connprova.c" "$OFF" > /dev/null 2>&1
printf 'exec python3 %s/tools/prova_netupdate_http.py %s/dist\n' "$RADICE" "$RADICE" > "$D/http.sh"
chmod +x "$D/http.sh"
gira() {    # $1 = name, then the commands; the installed disk is Italian: keymap us
    local nome=$1; shift
    EXOS_QEMU=$Q EXOS_ISTANZA=mdn$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=1024M EXOS_MARCA="utente:" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http.sh" \
        timeout 3600 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" "$@" > "$D/$nome.log" 2>&1
    cp "/tmp/exos/serialmdn$ARCH.txt" "$D/$nome-seriale.txt" 2>/dev/null
}
Q1="select concat('MARIADB ', 'riga ', nome) as r from prova.t"

echo "=== 2a. netupdate -install:mariadb-connector ==="
gira 2a-connettori \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" \
    "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -install:mariadb-connector@15" "si@~900" "poweroff@60"
c_lib=$($SBIN/debugfs -R 'stat /exos/lib/libmariadbclient.a' "$OFF" 2>/dev/null | grep -c 'Size: [1-9]')
c_cpp=$($SBIN/debugfs -R 'stat /exos/lib/libmariadbcpp.a' "$OFF" 2>/dev/null | grep -c 'Size: [1-9]')
c_srv=$($SBIN/debugfs -R 'stat /exos/mariadb/bin/mariadbd' "$OFF" 2>/dev/null | grep -c 'Size: [1-9]')

echo "=== 2b. netupdate -install:mariadb ==="
gira 2-netupdate "netupdate -install:mariadb@15" "si@~2400" "reboot@60"

echo "=== 3. mariadb-svc -install, una riga, reboot ==="
gira 3-install \
    "mariadb-svc -install@~900" \
    "mariadb -e \"create database prova; create table prova.t (nome varchar(20)) engine=innodb; insert into prova.t values ('EX-OS')\"@~60" \
    "mariadb -N -e \"$Q1\"@~60" \
    "gcc -O2 /root/connprova.c -o /root/connprova -I/exos/include/mariadb -lmariadbclient -lssl -lcrypto -lz -lzstd -lpthread -lm@~600" \
    "/root/connprova 127.0.0.1 3306@~60" "reboot@60"

echo "=== 4. riavvio: il server prima del login ==="
gira 4-riavvio \
    "mariadb -N -e \"$Q1\"@~60" "mariadb-svc -stato@~30" "mariadb-svc -ferma@~150" "poweroff@60"

$SBIN/debugfs -R "cat /var/log/mariadb/mariadb.log" "$OFF" > "$D/mariadb.log" 2>/dev/null
esito=0
controlla() {
    if eval "$2" > /dev/null 2>&1; then echo "  ok  $1"; else echo "  [NO] $1"; esito=1; fi
}
S2="$D/2-netupdate-seriale.txt"; S3="$D/3-install-seriale.txt"; S4="$D/4-riavvio-seriale.txt"
controlla "solo i connettori: C e C++"          "[ '$c_lib' = 1 ] && [ '$c_cpp' = 1 ]"
controlla "solo i connettori: niente server"    "[ '$c_srv' = 0 ]"
controlla "connprova.c compilato in EX-OS"      "tr -d '\\r' < '$S3' | grep -aq '^CONNC fine 0'"
controlla "netupdate: mariadbd sul disco"       "$SBIN/debugfs -R 'stat /exos/mariadb/bin/mariadbd' '$OFF' 2>/dev/null | grep -q 'Size: [1-9]'"
controlla "netupdate: anche [base] (gcc)"       "$SBIN/debugfs -R 'stat /exos/bin/gcc' '$OFF' 2>/dev/null | grep -q 'Size: [1-9]'"
controlla "netupdate: /exos/bin nei percorsi"   "$SBIN/debugfs -R 'cat /boot/percorsi.txt' '$OFF' 2>/dev/null | grep -qx '/exos/bin'"
controlla "netupdate: /exos/bin nel PATH"       "$SBIN/debugfs -R 'cat /boot/kernel.cfg' '$OFF' 2>/dev/null | grep -q '^PATH.*/exos/bin'"
controlla "-install: certificato TLS"           "grep -aq 'certificato TLS in' '$S3'"
controlla "-install: il server pronto"          "grep -aq 'MariaDB pronto su 127.0.0.1:3306' '$S3'"
controlla "la riga, prima del riavvio"          "grep -aq 'MARIADB riga EX-OS' '$S3'"
controlla "lo spegnimento chiude mariadbd"      "grep -aq 'Chiudo i programmi' '$S3' && ! grep -aq 'non si sono chiusi' '$S3'"
p=$(grep -an "MariaDB pronto su" "$S4" | head -1 | cut -d: -f1)
l=$(grep -an "EX-OS - accesso" "$S4" | head -1 | cut -d: -f1)
controlla "al riavvio, pronto prima del login"  "[ -n '$p' ] && [ -n '$l' ] && [ '$p' -lt '$l' ]"
controlla "la riga, dopo il riavvio"            "grep -aq 'MARIADB riga EX-OS' '$S4'"
controlla "-ferma"                              "grep -aq 'mariadbd fermato' '$S4'"
controlla "mariadb.log: Shutdown complete"      "grep -q 'Shutdown complete' '$D/mariadb.log'"
controlla "nessun recupero dopo un crash"       "! grep -q 'crash recovery' '$D/mariadb.log'"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S2" "$S3" "$S4"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S2" "$S3" "$S4" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  i registri: $D"
exit $esito
