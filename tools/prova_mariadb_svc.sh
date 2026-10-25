#!/bin/bash
# =============================================================================
# tools/prova_mariadb_svc.sh — the MariaDB service on an installed EX-OS
#
#     tools/prova_mariadb_svc.sh [dir]        (default /tmp/exos-prove/mariadb-svc)
#
# 32 bit: tools/mkhd.sh installs the 32-bit system from dist/exos.iso.
# The [mariadb] group of the tools CD (cross_build/mariadb-exos32/exos, made
# by tools/mariadb-exos/costruisci.sh 32) is written into /exos of the
# installed disk from Linux (debugfs), as `netupdate -install:mariadb` would.
#
#   1. the disk, installed from the CD (kept in [dir] for the next run;
#      delete it to start again from scratch)
#   2. login as root, `mariadb-svc -install`: user mysql, /etc/my.cnf, the
#      system tables, the block in /boot/avvio.sh, the server up; a table
#      with a row through the client (it reads [client] in /etc/my.cnf);
#      `reboot`: the kernel sends SIGTERM to mariadbd and waits for it
#   3. the machine again: the server is ready BEFORE the login page (the
#      line of `mariadb-svc -avvia` comes first on the serial); as root, the
#      row is still there, `mariadb-svc -stato`
#   4. from Linux: /var/log/mariadb/mariadb.log says "Shutdown complete"
#      after the first run, with no crash recovery at the second start
#
# Wants: dist/exos.iso (make iso-exos), debugfs and fakeroot.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-prove/mariadb-svc}"
mkdir -p "$D"
IMG="$D/hd-mariadb.img"
OFF="$IMG?offset=1048576"
SBIN=/usr/sbin
N=cross_build/mariadb-exos32/exos
STRIP=cross_build/nasi5deb/exos-cross/bin/i386-exos-strip

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: make iso-exos" >&2; exit 1; }
[ -x "$N/mariadb/bin/mariadbd" ] || { echo "manca $N: tools/mariadb-exos/costruisci.sh 32" >&2; exit 1; }

export EXOS_ISTANZA=mdsvc
export EXOS_RAM=1024M
# ! NO NETWORK CARD, on purpose (-nic none: QEMU adds an e1000 by itself): 127.0.0.1 is inside ip.drv, which without a
# card stays with the loopback alone (0.014); mariadb-svc -install starts it
# and writes `ip.drv &` in its block. With a card it is tools/prova_mariadb.sh.

# --- 1. the installed disk ---------------------------------------------------
if [ ! -f "$IMG" ]; then
    echo "=== 1. disco installato dal CD ($IMG), qualche minuto ==="
    sed "s|^IMG=dischi/hd.img|IMG=$IMG|; s|^MB=\"\${1:-512}\"|MB=\"\${1:-1024}\"|; \
         s|/tmp/exos-mkhd.log|$D/mkhd.log|g; \
         s|^EXOS_QEMU_EXTRA=\"-drive file=\$IMG,format=raw,if=ide\"|EXOS_QEMU_EXTRA=\"-drive file=\$IMG,format=raw,if=ide -nic none\"|" \
        tools/mkhd.sh > "$D/mkhd.sh"
    grep -q -- "-nic none" "$D/mkhd.sh" || { echo "mkhd.sh e' cambiato: non riesco a togliere la scheda"; exit 1; }
    EXOS_SUPPORTO=cd bash "$D/mkhd.sh" > "$D/1-disco.log" 2>&1 || {
        echo "NON RIUSCITO: il disco non si e' installato (vedi $D/1-disco.log)"
        exit 1
    }
    cp "$IMG" "$D/hd-pulito.img"
else
    echo "=== 1. disco gia' pronto ($IMG): torno alla copia appena installata ==="
    cp "$D/hd-pulito.img" "$IMG"
fi

# the group, as netupdate puts it in /exos: mariadb/, the programs in bin/
T="$D/albero"
rm -rf "$T"; mkdir -p "$T/bin"
cp -rp "$N/mariadb" "$T/"
cp -p "$N"/bin/* "$T/bin/"
# debugfs writes a name into the current directory: a cd before each file
{
    echo "mkdir /exos"
    (cd "$T" && find . -mindepth 1 -type d | sed 's|^\./||' | sort) | while read -r d; do
        echo "mkdir /exos/$d"
    done
    (cd "$T" && find . -type f | sed 's|^\./||' | sort) | while read -r f; do
        echo "cd /exos/$(dirname "$f")"
        echo "write $T/$f $(basename "$f")"
    done
} > "$D/debugfs.cmd"
fakeroot sh -c "chown -R 0:0 '$T'; $SBIN/debugfs -w -f '$D/debugfs.cmd' '$OFF'" > "$D/debugfs.log" 2>&1
$SBIN/debugfs -R "stat /exos/mariadb/bin/mariadbd" "$OFF" 2>/dev/null | grep -q "Size: [1-9]" || {
    echo "NON RIUSCITO: mariadbd non e' sul disco (vedi $D/debugfs.log)"; exit 1; }

export EXOS_NO_FLOPPY=1
export EXOS_MARCA="utente:"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -nic none"
# full paths: netupdate puts /exos/bin in the PATH of kernel.cfg when it
# installs [base] (toolinst -c), which this test does not go through
SVC=/exos/bin/mariadb-svc
CLI=/exos/bin/mariadb
Q="select concat('MARIADB ', 'riga ', nome) as r from prova.t"

# --- 2. -install, a row, reboot ----------------------------------------------
echo "=== 2. mariadb-svc -install, una riga, reboot ==="
timeout 1800 python3 tools/qemu_drive.py \
    "root@2" "root@5" "keymap us@3" \
    "$SVC -install@~900" \
    "$SVC -stato@~30" \
    "$CLI -e \"create database prova; create table prova.t (nome varchar(20)) engine=innodb; insert into prova.t values ('EX-OS')\"@~60" \
    "$CLI -N -e \"$Q\"@~60" \
    "reboot@60" > "$D/2-install.log" 2>&1
cp "/tmp/exos/serial$EXOS_ISTANZA.txt" "$D/2-seriale.txt" 2>/dev/null

# --- 3. the machine again ----------------------------------------------------
echo "=== 3. riavvio: il server prima del login ==="
timeout 900 python3 tools/qemu_drive.py \
    "root@2" "root@5" "keymap us@3" \
    "$CLI -N -e \"$Q\"@~60" \
    "$SVC -stato@~30" \
    "$SVC -ferma@~150" \
    "poweroff@60" > "$D/3-riavvio.log" 2>&1
cp "/tmp/exos/serial$EXOS_ISTANZA.txt" "$D/3-seriale.txt" 2>/dev/null

# --- the verdict -------------------------------------------------------------
$SBIN/debugfs -R "cat /var/log/mariadb/mariadb.log" "$OFF" > "$D/mariadb.log" 2>/dev/null
$SBIN/debugfs -R "cat /boot/avvio.sh" "$OFF" > "$D/avvio.sh" 2>/dev/null
esito=0
controlla() {   # $1 what, $2 command that must succeed
    if eval "$2" > /dev/null 2>&1; then echo "  ok  $1"; else echo "  [NO] $1"; esito=1; fi
}
S2="$D/2-seriale.txt"; S3="$D/3-seriale.txt"
controlla "-install: le tabelle di sistema"       "grep -aq 'tabelle di sistema' '$S2' && ! grep -aq 'bootstrap non e. riuscito' '$S2'"
controlla "-install: il server pronto"            "grep -aq 'MariaDB pronto su 127.0.0.1:3306' '$S2'"
controlla "il blocco in /boot/avvio.sh"           "grep -q 'mariadb-svc -avvia' '$D/avvio.sh' && grep -q '^ip.drv &' '$D/avvio.sh'"
controlla "ip.drv senza scheda: solo 127.0.0.1"   "grep -aq 'solo 127.0.0.1' '$S3'"
controlla "/boot/utenti: mysql"                   "$SBIN/debugfs -R 'cat /boot/utenti' '$OFF' 2>/dev/null | grep -q '^mysql:'"
controlla "la riga, prima del riavvio"            "grep -aq 'MARIADB riga EX-OS' '$S2'"
controlla "lo spegnimento chiude mariadbd"        "grep -aq 'Chiudo i programmi' '$S2' && ! grep -aq 'non si sono chiusi' '$S2'"
controlla "mariadb.log: Shutdown complete"        "grep -q 'Shutdown complete' '$D/mariadb.log'"
p=$(grep -an "MariaDB pronto su" "$S3" | head -1 | cut -d: -f1)
l=$(grep -an "EX-OS - accesso" "$S3" | head -1 | cut -d: -f1)
controlla "al riavvio, pronto prima del login"    "[ -n '$p' ] && [ -n '$l' ] && [ '$p' -lt '$l' ]"
controlla "la riga, dopo il riavvio"              "grep -aq 'MARIADB riga EX-OS' '$S3'"
controlla "nessun recupero dopo un crash"         "! grep -q 'crash recovery\|Starting crash recovery' '$D/mariadb.log'"
controlla "-ferma"                                "grep -aq 'mariadbd fermato' '$S3'"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S2" "$S3"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S2" "$S3" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  i registri: $D (2-seriale.txt, 3-seriale.txt, mariadb.log)"
exit $esito
