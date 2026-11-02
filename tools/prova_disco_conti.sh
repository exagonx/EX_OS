#!/bin/bash
# =============================================================================
# tools/prova_disco_conti.sh - the counters of the disks say what happened
#
#     tools/prova_disco_conti.sh [dir]                  32 bit
#     EXOS_ARCH=x86_64 tools/prova_disco_conti.sh [dir] 64 bit
#
# From the CD, with an ext2 disk: /proc/disco is read before and after copying
# a file of known size to the disk and reading it back. Written sectors must
# be at least the file, read sectors too, at least one FLUSH after sync, and
# `disk -stat` must print it.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/conti-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; export EXOS_RAM=128M EXTRA=""
fi
/usr/sbin/mke2fs -q -F -t ext2 -b 1024 -m 0 "$IMG" 65536 > /dev/null 2>&1
EXOS_ISTANZA=co$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 300 python3 tools/qemu_drive.py "mount hd0 /a@5" "cat /proc/disco@2" \
    "cp /KERNEL.BIN /a/k@6" "sync@3" "cat /proc/disco@2" \
    "cat /a/k > /a/k2@6" "sync@3" "cat /proc/disco@2" "disk -stat@3" "ls -l /a@2" \
    "umount /a@3" > "$D/q.log" 2>&1
python3 - "$D/q.log" <<'PY'
import re,sys
t=re.sub(r'\x1b\[[0-9;]*m','',open(sys.argv[1],errors='replace').read())
r=[list(map(int,l.split()[1:])) for l in re.findall(r'^hd0 [0-9 ]+$',t,re.M)]
m=re.search(r'(\d+)\s+\S+\s+\S+\s+k$',t,re.M)
ok=1
def v(c,s):
    global ok
    print('  [%s] %s'%('OK' if c else 'NO',s)); ok&=bool(c)
v(len(r)>=3,'/proc/disco letto %d volte'%len(r))
if len(r)>=3:
    a,b,c=r[0],r[1],r[2]; z=int(m[1])//512 if m else 0
    print('  file',z,'settori; scritti',b[5]-a[5],'poi',c[5]-b[5],'; letti',c[1]-b[1],'flush',a[8],b[8],c[8])
    v(z>0,'dimensione del file vista')
    v(b[5]-a[5]>=z,'settori scritti almeno quanti il file')
    v(c[1]+c[11]+c[12]-b[1]-b[11]-b[12]>=z,'settori letti o serviti almeno quanti il file')
    v(c[8]>a[8],'FLUSH contati')
    v(c[6]>=a[6] and c[2]>=a[2],'tempi non calano')
mf=re.search(r'flush (\d+) \((\d+) ms, max (\d+)\)',t)
v(mf and len(r)>=3 and int(mf[1])==r[2][8] and int(mf[3])==r[2][10],'disk -stat mette ogni numero al suo posto')
sys.exit(0 if ok else 1)
PY
