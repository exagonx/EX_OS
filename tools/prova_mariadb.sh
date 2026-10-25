#!/bin/bash
# =============================================================================
# tools/prova_mariadb.sh — MariaDB 11.8 in EX-OS
#
#     tools/prova_mariadb.sh [dir]                     64 bit
#     EXOS_ARCH=i386 tools/prova_mariadb.sh [dir]      32 bit
#
# The MariaDB of tools/mariadb-exos/costruisci.sh (cross_build/mariadb-exosNN)
# on an ext2 disk next to the CD:
#   1. mariadbd --bootstrap makes the system tables from bootstrap.sql, what
#      mariadb-install-db would feed it (a shell script EX-OS cannot run);
#   2. mariadbd starts with '&', listening on 127.0.0.1:3306;
#   3. the mariadb client over TCP: version, an InnoDB table, rows in, a
#      select, a transaction rolled back;
#   4. mariadb-admin shutdown, and mariadbd stops by itself;
#   5. Linux reads the data directory back (ibdata1, the table's .ibd).
# innodb_buffer_pool_size_max: InnoDB reserves address space for the largest
# buffer pool it may grow to, 8 TB by default at 64 bit; an EX-OS program has
# 3 GB, so it is said (MariaDB does the same on ARM with 39-bit addresses).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/mariadb-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
OFF="$IMG?offset=1048576"
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; BIT=64; STRIP=cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-strip
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; BIT=32; STRIP=cross_build/nasi5deb/exos-cross/bin/i386-exos-strip
    export EXOS_RAM=1024M EXTRA=""
fi
M=cross_build/mariadb-exos$BIT/exos/mariadb
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x "$M/bin/mariadbd" ] || { echo "manca $M: tools/mariadb-exos/costruisci.sh" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T/bin" "$T/share" "$T/dati" "$T/tmp"
for p in mariadbd mariadb mariadb-admin; do
    $STRIP -o "$T/bin/$p" "$M/bin/$p" || exit 1
done
cp -r "$M/share/charsets" "$M/share/english" "$T/share/"
# what mariadb-install-db pipes into mariadbd --bootstrap (cat_sql), with
# its cross-bootstrap filter: no root@<this host>, the name is not known
{
    echo "create database if not exists mysql;"
    echo "use mysql;"
    echo "SET @auth_root_socket=NULL;"
    for f in mariadb_system_tables mariadb_performance_tables mariadb_system_tables_data \
             fill_help_tables maria_add_gis_sp_bootstrap mariadb_sys_schema mariadb_test_db; do
        cat "$M/share/$f.sql"
    done
} | sed -e '/@current_hostname/d' > "$T/bootstrap.sql"
cat > "$T/prova.sql" <<'EOF'
select concat('MARIADB versione ', version()) as r;
create database prova;
create table prova.t (id int primary key, nome varchar(40)) engine=innodb;
insert into prova.t values (1, 'EX-OS'), (2, 'MariaDB'), (3, 'InnoDB');
select concat('MARIADB righe ', count(*)) as r from prova.t;
select concat('MARIADB motore ', engine) as r from information_schema.tables where table_schema='prova' and table_name='t';
start transaction;
delete from prova.t;
rollback;
select concat('MARIADB dopo il rollback ', group_concat(nome order by id)) as r from prova.t;
EOF
: > "$T/vuoto"
OPZ="--no-defaults --basedir=/a --datadir=/a/dati --lc-messages-dir=/a/share --character-sets-dir=/a/share/charsets --tmpdir=/a/tmp --user=root --innodb-buffer-pool-size=32M --innodb-buffer-pool-size-max=64M"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/*;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L mariadb -d '$T' '$D/p1.img' 393216" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 400M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=786432, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device ne2k_pci,netdev=n1"
echo "=== $ARCH ==="
CLI="/a/bin/mariadb --no-defaults --character-sets-dir=/a/share/charsets -h 127.0.0.1 -u root"
EXOS_ISTANZA=md$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 2400 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" \
    "mount hd0p1 /a@5" "cd /a@2" \
    "/a/bin/mariadbd $OPZ --bootstrap --log-warnings=0 < /a/bootstrap.sql@~900" \
    "echo BOOTSTRAP-FINITO@2" \
    "/a/bin/mariadbd $OPZ --bind-address=127.0.0.1 --port=3306 --skip-name-resolve &@60" \
    "$CLI -N < /a/prova.sql@~120" \
    "/a/bin/mariadb-admin --no-defaults -h 127.0.0.1 -u root shutdown@~120" \
    "echo FERMATO@30" "ls -l /a/dati /a/dati/prova@5" \
    "cd /@2" "umount /a@10" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialmd$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
controlla() {   # $1 what, $2 command that must succeed
    if eval "$2" > /dev/null 2>&1; then echo "  ok  $1"; else echo "  [NO] $1"; esito=1; fi
}
grep -a "^MARIADB " "$S" | sort -u | sed 's/^/  /'
controlla "versione 11.8"                  "grep -aq '^MARIADB versione 11\.8' '$S'"
controlla "3 righe nella tabella"          "grep -aq '^MARIADB righe 3' '$S'"
controlla "tabella InnoDB"                 "grep -aq '^MARIADB motore InnoDB' '$S'"
controlla "il rollback rimette le righe"   "grep -aq '^MARIADB dopo il rollback EX-OS,MariaDB,InnoDB' '$S'"
for f in ibdata1 prova/t.ibd prova/t.frm; do
    controlla "/a/dati/$f sul disco" "$SBIN/debugfs -R 'stat /dati/$f' '$OFF' 2>/dev/null | grep -q 'Size: [1-9]'"
done
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
