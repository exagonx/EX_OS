#!/bin/bash
# =============================================================================
# tools/prova_openssl.sh — OpenSSL 4.1 with sockets and threads, in EX-OS
#
#     tools/prova_openssl.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_openssl.sh [dir]    64 bit
#
# The OpenSSL of tools/openssl-exos/costruisci.sh, from the cross sysroot, on
# an ext2 disk next to the CD, with the QEMU network (user mode, DHCP):
#   1. tools/prove/sslprova.c: 4 threads at once (hash, AES-GCM, SSL_CTX);
#      a TLS server and client in one process over 127.0.0.1 (SSL_set_fd on
#      an accepted socket: MariaDB's case); TLS to www.example.com:443 on a
#      real socket, the certificate checked against the CA store;
#   2. the openssl program: version, dgst (Linux computes the same hash),
#      genpkey + req make a key and a certificate that Linux's openssl then
#      reads and verifies, s_client to www.example.com.
# Needs the Internet for the www.example.com parts.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/openssl-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
OFF="$IMG?offset=1048576"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc; BIT=64
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc; BIT=32
    export EXOS_RAM=256M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
$GCC -O2 -s tools/prove/sslprova.c -o "$T/sslprova" -lssl -lcrypto \
    || { echo "  (manca OpenSSL: tools/openssl-exos/costruisci.sh $BIT)"; exit 1; }
cp cross_build/openssl-exos$BIT/exos/bin/openssl "$T/" || exit 1
cp /etc/ssl/certs/ca-certificates.crt "$T/certi.pem" || exit 1
for i in $(seq 1 2000); do echo "riga $i: OpenSSL in EX-OS"; done > "$T/dati"
: > "$T/vuoto"
printf '[req]\ndistinguished_name = dn\n[dn]\n' > "$T/req.cnf"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/sslprova '$T'/openssl;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L openssl -d '$T' '$D/p1.img' 32768" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 40M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device ne2k_pci,netdev=n1"
echo "=== $ARCH ==="
EXOS_ISTANZA=os$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 1200 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" \
    "mount hd0p1 /a@5" "cd /a@2" \
    "/a/sslprova fili@~240" "/a/sslprova locale@~120" \
    "/a/sslprova rete www.example.com /a/certi.pem@~120" \
    "/a/openssl version@~30" "/a/openssl dgst -sha256 dati@~30" \
    "/a/openssl genpkey -algorithm ED25519 -out k.pem@~30" \
    "/a/openssl req -config req.cnf -new -x509 -key k.pem -subj /CN=exos -days 2 -out c.pem@~30" \
    "/a/openssl s_client -connect www.example.com:443 -CAfile certi.pem -brief < vuoto@~90" \
    "ls -l /a@3" "cd /@2" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialos$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^SSL " "$S" | sort -u | grep -v "^SSL fine" | sed 's/^SSL /  /'
[ "$(grep -ac "^SSL fine 0" "$S")" -ge 3 ] || { echo "  [NO] sslprova: non tutte giuste"; esito=1; }
for f in k.pem c.pem; do
    $SBIN/debugfs -R "dump /$f $D/$f" "$OFF" > /dev/null 2>&1
done
controlla() {   # $1 what, $2 command that must succeed
    if eval "$2" > /dev/null 2>&1; then echo "  ok  $1"; else echo "  [NO] $1"; esito=1; fi
}
H=$(sha256sum "$T/dati" | cut -d' ' -f1)
controlla "openssl version: 4.1.0"                 "grep -aq '^OpenSSL 4.1.0' '$S'"
controlla "openssl dgst -sha256 = sha256sum di Linux" "grep -aq '$H' '$S'"
controlla "chiave Ed25519 di EX-OS letta da Linux"  "openssl pkey -in '$D/k.pem' -noout"
controlla "certificato di EX-OS verificato da Linux" \
    "openssl verify -CAfile '$D/c.pem' '$D/c.pem' && openssl x509 -in '$D/c.pem' -noout -subject | grep -q 'CN *= *exos'"
controlla "openssl s_client: www.example.com verificato" "grep -aq 'Verification: OK' '$S'"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
