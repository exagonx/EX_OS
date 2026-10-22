#!/bin/bash
# =============================================================================
# tools/prova_ftp.sh — /bin/ftp and lib/exftp against a real server, FTP and
# FTPS, inside EX-OS in QEMU
#
#     tools/prova_ftp.sh            32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/prova_ftp.sh    64 bit (dist/exos64.iso)
#
# On the host: a test CA, a certificate for 10.0.2.2 signed by it, and
# tools/prova_ftp_server.py on 2121 (plain + AUTH TLS) and 2990 (implicit
# FTPS). Its PASV reply names a wrong address on purpose (NAT).
#
# In EX-OS, on a scratch ext2 disk:
#   1. a one-shot command: `ftp 10.0.2.2 2121 -u prova -w segreta ls`
#   2. the shell in clear: login asked at the prompts, pwd, ls, cd, get
#      (binary, 300 KB, checked by sha256), ascii get, put, mkdir, rename,
#      size, modtime, mget, active mode (PORT), reget, delete; and the CA
#      fetched into $HOME/.app/exhttp/certi.pem for the TLS part;
#   3. `ftps 10.0.2.2 2121`: AUTH TLS, PROT P, get + put encrypted;
#   4. `open -tlsi 10.0.2.2 2990`: implicit FTPS;
#   5. netspeed -makeserver over FTPS (the page check fails: no PHP here;
#      what counts is index.php arriving on the server).
#   6. like the home vsftpd (22 October 2026): a SELF-SIGNED certificate
#      (CN=myserver) on 2122, and data connections refused unless they
#      resume the control session. ftp shows the fingerprint, "si" accepts
#      it; a second `ftps` must not ask again.
#
# ! NO pkill: the server is stopped by PID; qemu_drive cleans up after itself.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-prove/ftpprova}"
ARCH="${EXOS_ARCH:-i386}"
mkdir -p "$D/www/sub"
IMG="$D/prova-hd-$ARCH.img"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)

# --- the host side --------------------------------------------------------------
if [ ! -f "$D/sito.pem" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/ca.key" -out "$D/ca.pem" \
        -subj "/CN=EX-OS prova CA/O=EX-OS" -days 30 \
        -addext "basicConstraints=critical,CA:TRUE" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
    openssl req -newkey rsa:2048 -nodes -keyout "$D/sito.key" -out "$D/sito.csr" \
        -subj "/CN=10.0.2.2" 2>/dev/null
    printf 'basicConstraints=critical,CA:FALSE\nsubjectAltName=DNS:10.0.2.2,IP:10.0.2.2\n' > "$D/sito.ext"
    openssl x509 -req -in "$D/sito.csr" -CA "$D/ca.pem" -CAkey "$D/ca.key" \
        -CAcreateserial -days 30 -sha256 -extfile "$D/sito.ext" -out "$D/sito.pem" 2>/dev/null
fi
if [ ! -f "$D/auto.pem" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/auto.key" -out "$D/auto.pem" \
        -subj "/CN=myserver" -days 30 2>/dev/null
fi
IMPRONTA=$(openssl x509 -in "$D/auto.pem" -noout -fingerprint -sha256 | sed 's/.*=//')
rm -rf "$D/www"; mkdir -p "$D/www/sub"
cp "$D/ca.pem" "$D/www/ca.pem"
echo "ciao dal server" > "$D/www/leggimi.txt"
head -c 300000 /dev/urandom > "$D/www/grande.bin"
printf 'riga uno\nriga due\nriga tre\n' > "$D/www/sub/testo.txt"
printf 'a\n' > "$D/www/sub/a1.txt"; printf 'b\n' > "$D/www/sub/a2.txt"
head -c 100000 "$D/www/grande.bin" > "$D/www/sub/mezzo.bin"   # reget: the first 100000 bytes
cp "$D/www/grande.bin" "$D/www/sub/mezzo-intero.bin"
SHA=$(sha256sum "$D/www/grande.bin" | cut -c1-16)

python3 tools/prova_ftp_server.py "$D/www" 2121 "$D/sito.pem" "$D/sito.key" 2990 > "$D/server-$ARCH.log" 2>&1 &
P_FTP=$!
PROVA_FTP_RIUSO=1 python3 tools/prova_ftp_server.py "$D/www" 2122 "$D/auto.pem" "$D/auto.key" > "$D/server-riuso-$ARCH.log" 2>&1 &
P_RIUSO=$!
sleep 1

# --- EX-OS ------------------------------------------------------------------------
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

if [ "$ARCH" = x86_64 ]; then
    export EXOS_QEMU=qemu-system-x86_64 EXOS_CDROM=dist/exos64.iso EXOS_RAM=512M
    CPU="-cpu qemu64"; RETE="e1000"
else
    export EXOS_CDROM=dist/exos.iso EXOS_RAM=128M
    CPU=""; RETE="ne2k_pci"
fi
EXOS_ISTANZA=ftp$ARCH EXOS_NO_FLOPPY=1 \
EXOS_QEMU_EXTRA="$CPU -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,hostfwd=tcp:127.0.0.1:40001-:40001,hostfwd=tcp:127.0.0.1:40002-:40002 -device $RETE,netdev=n1" \
    timeout 1500 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/casa@1" "mkdir /disk/casa/.app@1" "mkdir /disk/casa/.app/exhttp@1" \
    "export HOME=/disk/casa@1" "cd /disk@1" \
    "echo PASSO1@1" "ftp 10.0.2.2 2121 -u prova -w segreta ls@15" \
    "echo PASSO2@1" "ftp@3" "open 10.0.2.2 2121@8" "prova@3" "segreta@6" \
    "pwd@3" "ls@6" "get grande.bin@15" "!sha256sum grande.bin@6" \
    "cd sub@3" "ascii@3" "get testo.txt@6" "binary@3" "!cat testo.txt@4" \
    "put /disk/grande.bin su.bin@15" "size su.bin@3" "modtime su.bin@3" \
    "mkdir nuova@3" "rename su.bin nuova/su2.bin@4" "prompt off@2" "mget a*.txt@12" \
    "active@2" "nlist@8" "passive@2" \
    "!cp /disk/grande.bin mezzo.bin@6" "!trunc mezzo.bin 100000@4" "reget mezzo-intero.bin mezzo.bin@15" "!sha256sum mezzo.bin@6" \
    "delete a2.txt@3" "cd ..@3" "get ca.pem /disk/casa/.app/exhttp/certi.pem@8" "status@3" "bye@4" \
    "echo PASSO3@1" "ftp@3" "ftps 10.0.2.2 2121@20" "prova@3" "segreta@8" \
    "ls sub@15" "get grande.bin tls.bin@25" "!sha256sum tls.bin@6" "put tls.bin su-tls.bin@25" "status@3" "bye@4" \
    "echo PASSO4@1" "ftp -tlsi 10.0.2.2 2990 -u prova -w segreta size grande.bin@25" \
    "echo PASSO5@1" "netspeed -makeserver@3" "ftps@2" "10.0.2.2:2121@2" "prova@2" "segreta@2" "/@2" \
    "http://10.0.2.2:8099@40" \
    "echo PASSO6@1" "ftp@3" "ftps 10.0.2.2 2122@20" "si@20" "prova@3" "segreta@8" \
    "get grande.bin auto.bin@25" "!sha256sum auto.bin@6" "ls@15" "bye@4" \
    "ftp -tls 10.0.2.2 2122 -u prova -w segreta size grande.bin@25" \
    "!cat /disk/casa/.app/exhttp/fidati.txt@3" \
    "echo FINE@2" > "$D/exos-$ARCH.log" 2>&1

kill $P_FTP $P_RIUSO 2>/dev/null
wait $P_FTP $P_RIUSO 2>/dev/null

# --- the result ---------------------------------------------------------------------
L=$(sed 's/\x1b\[[0-9;]*m//g' "$D/exos-$ARCH.log" | tr -d '\000')
esito=0
controlla() {   # $1 = description, $2 = condition (0 = true)
    if [ "$2" = 0 ]; then echo "  si'  $1"; else echo "  NO   $1"; esito=1; fi
}
sez() { printf '%s\n' "$L" | sed -n "/^$1/,/^$2/p"; }
sez PASSO1 PASSO2 | grep -aq "grande.bin";                  controlla "1: comando singolo (ls)" $?
sez PASSO2 PASSO3 | grep -aq '257 "/"';                     controlla "2: pwd dopo l'accesso chiesto al prompt" $?
[ "$(sez PASSO2 PASSO3 | grep -a "grande.bin" | grep -ac "$SHA")" -ge 1 ]; controlla "2: get binario, sha256 uguale" $?
sez PASSO2 PASSO3 | grep -aq "riga due";                    controlla "2: get ascii" $?
cmp -s "$D/www/grande.bin" "$D/www/sub/nuova/su2.bin";      controlla "2: put + mkdir + rename (file uguale sul server)" $?
sez PASSO2 PASSO3 | grep -aq "213 300000";                  controlla "2: size" $?
[ -f "$D/www/sub/a1.txt" ] && sez PASSO2 PASSO3 | grep -aq "a1.txt"; controlla "2: mget" $?
grep -aq "PORT command successful" "$D/server-$ARCH.log";  controlla "2: modo attivo (PORT)" $?
sez PASSO2 PASSO3 | grep -aq "Riprendo da 100000";          controlla "2: reget riparte da 100000" $?
[ "$(sez PASSO2 PASSO3 | grep -a "mezzo.bin" | grep -ac "$SHA")" -ge 1 ]; controlla "2: reget, file intero giusto" $?
[ ! -f "$D/www/sub/a2.txt" ];                               controlla "2: delete" $?
sez PASSO3 PASSO4 | grep -aq "cifrat";                      controlla "3: FTPS esplicito, comandi e dati cifrati" $?
[ "$(sez PASSO3 PASSO4 | grep -a "tls.bin" | grep -ac "$SHA")" -ge 1 ]; controlla "3: get su TLS, sha256 uguale" $?
cmp -s "$D/www/grande.bin" "$D/www/su-tls.bin";              controlla "3: put su TLS (file uguale sul server)" $?
sez PASSO4 PASSO5 | grep -aq "213 300000";                  controlla "4: FTPS implicito (990-style)" $?
grep -q "EX-OS netspeed" "$D/www/netspeed/index.php" 2>/dev/null; controlla "5: netspeed -makeserver carica index.php via FTPS" $?
sez PASSO6 FINE | grep -aq "$IMPRONTA";                     controlla "6: l'impronta mostrata e' quella del certificato" $?
[ "$(sez PASSO6 FINE | grep -a "auto.bin" | grep -ac "$SHA")" -ge 1 ]; controlla "6: get con dati che riprendono la sessione, sha256 uguale" $?
! grep -aq "dati ripresi: no" "$D/server-riuso-$ARCH.log" && grep -aq "dati ripresi: si" "$D/server-riuso-$ARCH.log"; controlla "6: ogni connessione dati ha ripreso la sessione" $?
[ "$(sez PASSO6 FINE | grep -ac "Accettarlo")" = 1 ];        controlla "6: la seconda volta non chiede" $?
sez PASSO6 FINE | grep -aq "213 300000";                    controlla "6: la seconda volta funziona (size)" $?
echo "  registri: $D/exos-$ARCH.log  $D/server-$ARCH.log"
echo "ESITO $ARCH: $([ $esito = 0 ] && echo BUONO || echo "qualcosa non va")"
exit $esito
