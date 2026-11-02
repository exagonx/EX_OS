#!/bin/bash
# =============================================================================
# tools/prova_cache.sh - the disk's page cache (kernel 0.304)
#
#     tools/prova_cache.sh [dir]                  32 bit
#     EXOS_ARCH=x86_64 tools/prova_cache.sh [dir] 64 bit
#
# On ext4 with blocks of 4 KB and of 1 KB. tools/prove/cachepag.c checks every
# byte against a model, also after memory is filled past what the cache can
# hold; a file read twice must need no reads from the disk the second time
# (from /proc/disco); from outside, e2fsck must find nothing and the file on
# the image must have the hash the guest computed.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/cache-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"; MB=470
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""; MB=110
fi
$GCC -O2 tools/prove/cachepag.c -o "$D/albero/cachepag" || exit 1
chmod 755 "$D/albero/cachepag"
esito=0
for B in 4096 1024; do
    echo "--- blocchi da $B ---"
    IMG="$D/hd$B.img"
    fakeroot /usr/sbin/mke2fs -q -F -t ext4 -b $B -m 0 -d "$D/albero" "$IMG" 64M > /dev/null 2>&1
    EXOS_ISTANZA=ca$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
        EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
        timeout 500 python3 tools/qemu_drive.py "mount hd0 /a@6" "/a/cachepag /a $MB@90" \
        "sha256sum /a/grande@8" "sha256sum /a/grande@8" \
        "cat /proc/disco@2" "sha256sum /a/grande@8" "cat /proc/disco@2" \
        "umount /a@6" "dmesg | grep cache@3" > "$D/q$B.log" 2>&1
    python3 - "$D/q$B.log" "$IMG" <<'PY'
import re,sys,subprocess,hashlib
t=re.sub(r'\x1b\[[0-9;]*m','',open(sys.argv[1],errors='replace').read())
ok=True
def v(c,s):
    global ok
    print('  [%s] %s'%('OK' if c else 'NO',s)); ok&=bool(c)
v('CACHEPAG A POSTO' in t,'ogni byte come il modello')
m=re.search(r'CACHEPAG toccati (\d+) MB',t); print('      memoria toccata:',m and m.group(1),'MB')
r=[list(map(int,l.split()[1:])) for l in re.findall(r'^hd0 [0-9 ]+$',t,re.M)]
if len(r)>=2:
    d=[b-a for a,b in zip(r[0],r[1])]
    print('      seconda lettura: %d comandi, %d settori dal disco, %d dalla cache'%(d[0],d[1],d[11]))
    v(d[1]<200 and d[11]>=16000,'il file riletto viene dalla cache')
else: v(False,'/proc/disco letto')
h=re.findall(r'^([0-9a-f]{64})\s',t,re.M)
v(len(set(h))==1 and len(h)>=3,'hash uguali nel sistema')
f=subprocess.run(['/usr/sbin/e2fsck','-fn',sys.argv[2]],capture_output=True,text=True)
v(f.returncode==0,'e2fsck da fuori'+('' if f.returncode==0 else ': '+f.stdout[-200:]))
o=subprocess.run(['/usr/sbin/debugfs','-R','cat /grande',sys.argv[2]],capture_output=True).stdout
v(h and hashlib.sha256(o).hexdigest()==h[-1],'il file sul disco e\' quello visto dal sistema')
sys.exit(0 if ok else 1)
PY
    [ $? = 0 ] || esito=1
done
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
