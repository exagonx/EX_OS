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
#   4. the connectors of [mariadb-connector], each a program built here
#      for EX-OS against the server: C (tools/prove/connprova.c, Connector/C),
#      C++ (connprova.cpp, Connector/C++), Rust (tools/rust-exos/prova-mariadb,
#      the "mariadb" crate; when the host has the Rust of tools/rust-exos)
#      and FreeBASIC (connprova.bas, mariadb.bi; 32 bit, where libfb is);
#   5. mariadb-admin shutdown, and mariadbd stops by itself;
#   6. Linux reads the data directory back (ibdata1, the table's .ibd).
# innodb_buffer_pool_size_max: InnoDB reserves address space for the largest
# buffer pool it may grow to, 8 TB by default at 64 bit; an EX-OS program has
# 3 GB, so it is said (MariaDB does the same on ARM with 39-bit addresses).
# A certificate made here: without one MariaDB 11.8 makes a 4096-bit RSA key
# at every start, from 20 s to minutes in QEMU at 32 bit, and the client came
# before the server was ready (mariadb-svc makes one at -install). Not
# --skip-ssl: Connector/C 3.4 asks for TLS by default and refuses a server
# without it.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/mariadb-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
OFF="$IMG?offset=1048576"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; BIT=64; TC=$X/exos-cross64; TT=x86_64-exos; RS=x86_64-unknown-exos
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; BIT=32; TC=$X/exos-cross; TT=i386-exos; RS=i686-unknown-exos
    export EXOS_RAM=1024M EXTRA=""
fi
E=cross_build/mariadb-exos$BIT/exos
M=$E/mariadb
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x "$M/bin/mariadbd" ] || { echo "manca $M: tools/mariadb-exos/costruisci.sh" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T/bin" "$T/share" "$T/dati" "$T/tmp"
# already stripped by costruisci.sh (installa)
cp "$M/bin/mariadbd" "$E/bin/mariadb" "$E/bin/mariadb-admin" "$T/bin/"
cp -r "$M/share/charsets" "$M/share/english" "$T/share/"
# what mariadb-install-db would pipe into mariadbd --bootstrap: costruisci.sh
# writes it, mariadb-svc -install uses it
cp "$M/share/bootstrap.sql" "$T/bootstrap.sql"
# the connectors' programs; the libraries come from the sysroot, where
# tools/mariadb-connector-c-exos and -cpp-exos put them
INC=$TC/$TT/include/mariadb
LIBS="-lmariadbclient -lssl -lcrypto -lz -lzstd -lpthread -lm"
CONN="connprova connprovapp"
$TC/bin/$TT-gcc -O2 -s tools/prove/connprova.c -o "$T/bin/connprova" -I"$INC" $LIBS || exit 1
$TC/bin/$TT-g++ -O2 -s tools/prove/connprova.cpp -o "$T/bin/connprovapp" -I"$INC" -lmariadbcpp $LIBS || exit 1
if EXOS_ARCH=$ARCH PROVA=prova-mariadb tools/rust-exos/prova-std.sh --solo-compila > "$D/rust.log" 2>&1 ||
   grep -q "^collegato" "$D/rust.log"; then   # its CPU check counts the ed25519 cmov() by name
    cp "$X/costruzione-rust/$RS/release/prova-mariadb" "$T/bin/connprovars"; CONN="$CONN connprovars"
else
    echo "  (Rust per EX-OS non c'e' su questa macchina: $D/rust.log)"
fi
# fb-build-110, the build the Makefile puts on the CD (FB_NATIVO): the older
# fb-build-exos has an fbextra.x without "INSERT AFTER .data", which then
# replaces ld's script and links the program at address 0
FB=cross_build/fb-build-110
if [ "$BIT" = 32 ] && [ -f "$FB/libfb.a" ]; then
    P="$D/fb"; mkdir -p "$P/lib/freebasic/exos-x86" "$P/include" "$P/bin"
    cp "$FB/fbextra.x" "$FB/fbrt0.o" "$FB/libfb.a" "$TC/$TT/lib/crt0.o" "$P/lib/freebasic/exos-x86/"
    ln -s "$PWD/cross_build/fb64/albero/inc" "$P/include/freebasic"
    for s in as ld gcc ar; do ln -s "$PWD/$TC/bin/$TT-$s" "$P/bin/exos-$s"; done
    PATH="$P/bin:$PATH" cross_build/fb64/albero/bin/fbc -prefix "$P" -target exos tools/prove/connprova.bas \
        -i "cross_build/mariadb-connector-c-exos$BIT/exos/include/freebasic" -x "$T/bin/connprovafb" -p "$TC/$TT/lib" \
        -p "$(dirname "$($TC/bin/$TT-gcc -print-libgcc-file-name)")" > "$D/fb.log" 2>&1 || { cat "$D/fb.log"; exit 1; }
    CONN="$CONN connprovafb"
else
    echo "  (FreeBASIC: libfb per EX-OS solo a 32 bit su questa macchina)"
fi
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
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 30 \
    -subj /CN=exos-prova -keyout "$T/key.pem" -out "$T/cert.pem" > /dev/null 2>&1 || exit 1
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
    "/a/bin/mariadbd $OPZ --bind-address=127.0.0.1 --port=3306 --skip-name-resolve --ssl-cert=/a/cert.pem --ssl-key=/a/key.pem &@60" \
    "$CLI -N < /a/prova.sql@~120" \
    "/a/bin/connprova 127.0.0.1 3306@~60" "/a/bin/connprovapp 127.0.0.1 3306@~60" \
    "/a/bin/connprovars 127.0.0.1 3306@~60" "/a/bin/connprovafb 127.0.0.1 3306@~60" \
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
controlla "versione 11.8.9"                "grep -aq '^MARIADB versione 11\.8\.9' '$S'"
controlla "3 righe nella tabella"          "grep -aq '^MARIADB righe 3' '$S'"
controlla "tabella InnoDB"                 "grep -aq '^MARIADB motore InnoDB' '$S'"
controlla "il rollback rimette le righe"   "grep -aq '^MARIADB dopo il rollback EX-OS,MariaDB,InnoDB' '$S'"
grep -a "^CONN[A-Z]* \(versione\|righe\|NO\)" "$S" | sort -u | sed 's/^/  /'
for c in $CONN; do
    k=$(echo "$c" | sed 's/connprova$/CONNC/; s/connprovapp/CONNCPP/; s/connprovars/CONNRS/; s/connprovafb/CONNFB/')
    controlla "$c: il connettore con il server" "grep -aq '^$k fine 0' '$S'"
done
for f in ibdata1 prova/t.ibd prova/t.frm; do
    controlla "/a/dati/$f sul disco" "$SBIN/debugfs -R 'stat /dati/$f' '$OFF' 2>/dev/null | grep -q 'Size: [1-9]'"
done
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
