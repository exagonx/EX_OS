#!/bin/bash
# =============================================================================
# tools/prova_httpd.sh — Apache httpd on EX-OS, in QEMU
#
#     tools/prova_httpd.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_httpd.sh [dir]    64 bit
#
# cross_build/httpd-exosNN/exos/apache2 (tools/httpd-exos/costruisci.sh) on
# an ext2 disk next to the CD, with conf/httpd-exos.conf on /a/apache2 and a
# CGI program (tools/prove/cgiprova.c). Inside EX-OS:
#   1. httpd -X in the background (one process, worker MPM);
#   2. ab inside EX-OS: 2000 requests, 20 at a time, on 127.0.0.1;
#   3. meanwhile from Linux through QEMU (hostfwd 127.0.0.1:18080 -> 80):
#      the page, the CGI with a query and with a POST body, a missing page
#      (404), ab with 5000 requests, 50 at a time;
#   4. httpd -k stop (the pid file and SIGTERM), then error_log and
#      access_log on the serial.
# PHP (28 October 2026): mod_php is inside httpd when tools/php-exos was
# built first; then cross_build/php-exosNN/exos/php goes on the disk too
# (php.ini with its paths on /a, PHPIniDir) and a page tools/prove/prova.php
# is asked from the host with GET and with POST.
# Wants dist/exos.iso / dist/exos64.iso (make iso-exos / make iso64).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/httpd-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
PORTA=18080
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc; A=cross_build/httpd-exos64/exos/apache2
    P=cross_build/php-exos64/exos/php
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc; A=cross_build/httpd-exos32/exos/apache2
    P=cross_build/php-exos32/exos/php
    export EXOS_RAM=512M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x "$A/bin/httpd" ] || { echo "manca $A: tools/httpd-exos/costruisci.sh" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
cp -a "$A" "$T/apache2"
sed 's#/exos/apache2#/a/apache2#g' tools/httpd-exos/conf/httpd-exos.conf > "$T/apache2/conf/httpd-exos.conf"
echo '<html><body><h1>EX-OS</h1><p>Apache httpd su EX-OS</p></body></html>' > "$T/apache2/htdocs/index.html"
rm -f "$T"/apache2/cgi-bin/*
$GCC -O2 -Wall -s tools/prove/cgiprova.c -o "$T/apache2/cgi-bin/cgiprova" || exit 1
CONPHP=0
if strings "$A/bin/httpd" | grep -q "X-Powered-By: PHP/"; then
    [ -f "$P/etc/php.ini" ] || { echo "httpd ha mod_php ma manca $P (tools/php-exos/costruisci.sh)" >&2; exit 1; }
    CONPHP=1
    # only what mod_php reads at run time: the build pieces (lib/libphp.a,
    # include/, 24 MB at 64 bit) and bin/php do not fit in the 64 MB disk
    mkdir -p "$T/php/tmp"
    cp -a "$P/etc" "$T/php/"
    sed -i 's#/exos/php/#/a/php/#g' "$T/php/etc/php.ini"
    printf '\nPHPIniDir /a/php/etc\n' >> "$T/apache2/conf/httpd-exos.conf"
    cp tools/prove/prova.php "$T/apache2/htdocs/"
fi
fakeroot sh -c "chown -R 0:0 '$T'; chmod -R a+rX '$T'; chmod 777 '$T/apache2/logs'; [ -d '$T/php/tmp' ] && chmod 777 '$T/php/tmp';
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L httpd -d '$T' '$D/p1.img' 65536" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 70M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=131072, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
# the CD starts the network by itself on the e1000 (ip.drv, and 127.0.0.1)
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,hostfwd=tcp:127.0.0.1:$PORTA-:80"

# --- from Linux, while EX-OS runs ---------------------------------------------
dall_host() {
    local U=http://127.0.0.1:$PORTA i
    for i in $(seq 1 600); do
        curl -s -m 5 -o /dev/null "$U/" && break
        sleep 1
    done
    {
        echo "== pagina";   curl -s -m 20 -i "$U/" | tr -d '\r'
        echo "== cgi";      curl -s -m 20 "$U/cgi-bin/cgiprova?uno=1&due=2" | tr -d '\r'
        echo "== post";     curl -s -m 20 --data "ciao=mondo" "$U/cgi-bin/cgiprova" | tr -d '\r'
        echo "== 404";      curl -s -m 20 -o /dev/null -w "%{http_code}\n" "$U/manca.html"
        echo "== php get";  curl -s -m 30 "$U/prova.php?nome=exos" | tr -d '\r'
        echo "== php post"; curl -s -m 30 --data "nome=posta" "$U/prova.php" | tr -d '\r'
        # QEMU's user network accepts the forwarded port on a Linux socket
        # with a tiny backlog: with 50 at a time its queue overflows, the
        # host drops SYNs and the client retries after 1, 3, 7... s (a
        # capture at the e1000 showed the lost ones never reach EX-OS). So
        # ab waits up to 120 s and the overflows of the host are printed.
        local o1=$(trabocchi)
        echo "== ab";       ab -q -s 120 -n 5000 -c 50 "$U/index.html" 2>&1
        echo "== trabocchi della coda d'ascolto sull'host: $(( $(trabocchi) - o1 ))"
    } > "$D/host.txt"
}
# TcpExt ListenOverflows of the host, from /proc/net/netstat
trabocchi() {
    awk '/^TcpExt:/ { if (!h) { for (i = 1; i <= NF; i++) if ($i == "ListenOverflows") c = i; h = 1 } else print $c }' /proc/net/netstat
}
dall_host &
HOSTJOB=$!

echo "=== $ARCH ==="
H=/a/apache2/bin/httpd
C="-d /a/apache2 -f conf/httpd-exos.conf"
EXOS_ISTANZA=ht$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 1500 python3 tools/qemu_drive.py "ipcfg@5" "mount hd0p1 /a@5" \
    "$H -X $C &@15" "ps@5" \
    "/a/apache2/bin/ab -n 2000 -c 20 http://127.0.0.1/index.html@~300" \
    "sleep 150@~180" "ipcfg -tcp@10" "ipcfg@5" \
    "$H -k stop $C@~30" "sleep 5@~10" \
    "cat /a/apache2/logs/error_log@5" "ps@5" "umount /a@5" > "$D/qemu.log" 2>&1
kill "$HOSTJOB" 2>/dev/null; wait "$HOSTJOB" 2>/dev/null
S="$D/seriale.txt"
tr -d '\r' < "/tmp/exos/serialht$ARCH.txt" > "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"

esito=0
si()  { echo "  ok $1"; }
no()  { echo "  NO $1"; esito=1; }
# with -X there is no parent: neither "resuming normal operations" nor
# "caught SIGTERM" are logged. ps shows the process, the shell its exit code.
grep -aEq "^ *[0-9]+ .*/a/apache2/bin/httpd" "$S" && si "httpd acceso (in ps)" || no "httpd non si e' acceso"
n=$(grep -a "^Complete requests:" "$S" | head -1 | awk '{print $3}')
f=$(grep -a "^Failed requests:" "$S" | head -1 | awk '{print $3}')
r=$(grep -a "^Requests per second:" "$S" | head -1 | awk '{print $4}')
[ "$n" = 2000 ] && [ "$f" = 0 ] && si "ab dentro EX-OS: 2000 richieste, 0 fallite, $r al secondo" \
    || no "ab dentro EX-OS: complete $n, fallite $f"
grep -q "<h1>EX-OS</h1>" "$D/host.txt" 2>/dev/null && si "la pagina dall'host" || no "la pagina dall'host"
grep -q "CGI ok metodo=GET query=uno=1&due=2 corpo= cartella=/a/apache2/cgi-bin" "$D/host.txt" 2>/dev/null \
    && si "il CGI con la query (spawn_ex da APR, nella sua cartella)" || no "il CGI con la query"
grep -q "CGI ok metodo=POST query= corpo=ciao=mondo" "$D/host.txt" 2>/dev/null \
    && si "il CGI con un POST (il corpo sullo stdin del figlio)" || no "il CGI con un POST"
grep -A1 "^== 404" "$D/host.txt" 2>/dev/null | grep -q "^404" && si "una pagina che manca: 404" || no "404"
n=$(grep "^Complete requests:" "$D/host.txt" 2>/dev/null | awk '{print $3}')
f=$(grep "^Failed requests:" "$D/host.txt" 2>/dev/null | awk '{print $3}')
r=$(grep "^Requests per second:" "$D/host.txt" 2>/dev/null | awk '{print $4}')
t=$(sed -n "s/^== trabocchi della coda d'ascolto sull'host: //p" "$D/host.txt" 2>/dev/null)
[ "$n" = 5000 ] && [ "$f" = 0 ] && si "ab dall'host: 5000 richieste, 50 alla volta, 0 fallite, $r al secondo (coda dell'host traboccata $t volte)" \
    || no "ab dall'host: complete $n, fallite $f"
if [ $CONPHP = 1 ]; then
    grep -q "^PHP ok 8\.[0-9.]* GET nome=exos pdo_mysql mysqli opcache somma=6" "$D/host.txt" 2>/dev/null \
        && si "PHP con un GET (mod_php dentro httpd, PDO e mysqli caricati)" || no "PHP con un GET"
    grep -q "^PHP ok 8\.[0-9.]* POST nome=posta" "$D/host.txt" 2>/dev/null \
        && si "PHP con un POST" || no "PHP con un POST"
fi
grep -aq "terminato: /a/apache2/bin/httpd -X.*(codice 0)" "$S" && si "httpd -k stop: spento con codice 0" || no "httpd -k stop"
# every CGI left a reference on logs/error_log until kernel 0.283
grep -aq "^smontato /a" "$S" && si "umount /a dopo lo spegnimento (nessun file rimasto aperto)" \
    || no "umount /a: $(grep -a 'VFS: /a occupato' "$S" | head -1)"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S, dall'host: $D/host.txt"
exit $esito
