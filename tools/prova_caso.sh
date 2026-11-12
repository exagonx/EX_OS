#!/bin/bash
# =============================================================================
# tools/prova_caso.sh — the kernel's random numbers on a CPU without RDRAND
# (analisi voce 67), in QEMU
#
#     tools/prova_caso.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_caso.sh [dir]    64 bit
#
# -cpu core2duo has no RDRAND (as the Q9650 of the .247). casoprova.c prints
# two 32-byte reads and writes 1 MB of getrandom to the disk; here, what
# ent would say: chi square of the bytes, mean, serial correlation, and how
# much zlib squeezes it.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/caso-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
SBIN=/usr/sbin
X=cross_build/nasi5deb

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/casoprova.c -o "$D/albero/casoprova" || exit 1

qemu-img create -f raw "$D/hd.img" 16M > /dev/null
$SBIN/mke2fs -q -F -t ext2 -b 1024 -d "$D/albero" "$D/hd.img"
export EXOS_ISTANZA=caso$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="-cpu core2duo -drive file=$D/hd.img,format=raw,if=ide"
timeout 600 python3 tools/qemu_drive.py "mount hd0 /a@5" "/a/casoprova /a/caso.bin@~60" \
    "umount /a@3" > "$D/qemu.log" 2>&1

echo "=== $ARCH: numeri casuali senza RDRAND ==="
tr -d '\r' < /tmp/exos/serialcaso$ARCH.txt | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
grep -a "RDRAND" "$D/seriale.txt" | head -1 | sed 's/^/  /'
grep -a "^CASO" "$D/seriale.txt" | sed 's/^/  /'
$SBIN/debugfs -R "dump /caso.bin $D/caso.bin" "$D/hd.img" > /dev/null 2>&1
python3 - "$D/caso.bin" "$D/seriale.txt" <<'PY'
import sys, zlib, math
d = open(sys.argv[1], 'rb').read() if True else b''
ser = open(sys.argv[2], errors='replace').read()
no = 0
def esito(ok, cosa):
    global no
    print("  [%s] %s" % ("OK" if ok else "NO", cosa)); no += 0 if ok else 1
esito(len(d) == 1048576, "1 MB letto (%d byte)" % len(d))
if d:
    f = [0] * 256
    for b in d: f[b] += 1
    att = len(d) / 256
    chi = sum((x - att) ** 2 / att for x in f)
    media = sum(d) / len(d)
    n = len(d); x = d; sx = sum(x); sxx = sum(v * v for v in x)
    sxy = sum(x[i] * x[(i + 1) % n] for i in range(n))
    corr = (n * sxy - sx * sx) / (n * sxx - sx * sx)
    z = len(zlib.compress(d, 9)) / len(d)
    # 255 degrees of freedom: 99.9% of random files stay inside 190-330
    esito(190 < chi < 330, "chi quadro %.1f (fra 190 e 330)" % chi)
    esito(abs(media - 127.5) < 0.5, "media %.3f (127,5)" % media)
    esito(abs(corr) < 0.005, "correlazione seriale %.5f" % corr)
    esito(z > 0.999, "zlib lo lascia a %.4f" % z)
esito("CASO una chiamata da 1000: 1000" in ser, "1000 byte in una chiamata")
import re
m = re.search(r"byte uguali al loro posto: (\d+)", ser)
esito(m is not None and int(m.group(1)) <= 3, "due letture di fila: %s byte uguali" % (m.group(1) if m else "?"))
sys.exit(1 if no else 0)
PY
