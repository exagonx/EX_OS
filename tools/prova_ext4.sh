#!/bin/bash
# =============================================================================
# tools/prova_ext4.sh — ext4 written by /dev/ext4.mod 0.004 (@EXT4-SCRITTURA),
# in QEMU, booting from the CD
#
#     tools/prova_ext4.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_ext4.sh [dir]    64 bit
#
# The disk, made by Linux (mke2fs -t ext4 of this machine: metadata_csum,
# 64bit, flex_bg, orphan_file, metadata_csum_seed, groups left UNINIT):
#   p1  ext4, 4 KB blocks, with a directory of 400 files given a hashed
#       index by e2fsck -D                                written by EX-OS
#   p2  ext4, 1 KB blocks: an extent leaf holds 84, so 600 extents need a
#       tree two levels deep                              written by EX-OS
#   p3  ext4 whose journal debugfs left with CHECKSUMS (jo -c: CSUM_V3), a
#       committed transaction and a revoked block, needs_recovery set: EX-OS
#       must replay it, and then write CSUM_V3 transactions of its own
#   p4  ext4 without metadata_csum, with uninit_bg (crc16 descriptors) and
#       32-byte descriptors (no 64bit)                    written by EX-OS
#
# Run A: p3 replayed and written; on p1, p2, p4 mkdir, cp, mv, echo >, rm,
# rmdir, a file added to and one removed from the indexed directory,
# tools/prove/discovel.c and tools/prove/estesi.c (600 extents, cut in the
# middle, grown back, and one write() of 4 MB); umount.
# Outside: e2fsck -fn clean everywhere, needs_recovery off, the files
# identical byte for byte, the extent trees as deep as they must be. Then
# the last transaction EX-OS committed on p3 (with checksums) is put back in
# the journal and Linux's e2fsck must recover it and find the volume clean.
# Run B: p1 written and QEMU stopped without umount: e2fsck recovers it.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/ext4-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
S1=196608; S2=131072; S3=65536; S4=131072       # sectors: 96, 64, 32, 64 MB
O1=2048; O2=$((O1 + S1)); O3=$((O2 + S2)); O4=$((O3 + S3))
N_EST=600

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc; LS=build-64/iso-exos/bin/ls
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc; LS=build/iso-exos/bin/ls
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/discovel.c -o "$D/discovel" || exit 1
$GCC -O2 tools/prove/estesi.c -o "$D/estesi" || exit 1

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
fuori() { dd if="$IMG" of="$2" bs=512 skip="$1" count="$3" status=none; }      # start, file, count
dentro() { dd if="$2" of="$IMG" bs=512 seek="$1" conv=notrunc status=none; }

# --- the disk -----------------------------------------------------------------
echo "=== 1. disco: quattro ext4 fatti da Linux ($ARCH) ==="
qemu-img create -f raw "$IMG" $(((O4 + S4) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\n' \
    $O1 $S1 $O2 $S2 $O3 $S3 $O4 $S4 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
mkdir -p "$D/albero/molti" "$D/rec"
cp "$D/discovel" "$D/estesi" "$D/albero/"
echo "da togliere" > "$D/albero/da-togliere.txt"
for i in $(seq -w 1 400); do echo "file $i" > "$D/albero/molti/un-nome-abbastanza-lungo-$i"; done

$SBIN/mke2fs -q -F -t ext4 -b 4096 -L ext4-4k -d "$D/albero" "$D/p1.img" $((S1 / 8)) || exit 1
$SBIN/e2fsck -fyD "$D/p1.img" > /dev/null 2>&1
$SBIN/debugfs -R "htree /molti" "$D/p1.img" 2>/dev/null | grep -qi "root node" \
    && ok "p1: /molti ha un indice htree" || no "p1: /molti non ha un indice"
# p2 without flex_bg (every group holds its own bitmaps and table: the
# bitmap EX-OS builds for a BLOCK_UNINIT group must mark them) and with few
# inodes per group, so that EX-OS's files reach a group still INODE_UNINIT
$SBIN/mke2fs -q -F -t ext4 -O ^flex_bg -N 512 -b 1024 -L ext4-1k -d "$D/albero" \
    "$D/p2.img" $((S2 / 2)) || exit 1
read -r K_FILE IU2 < <($SBIN/dumpe2fs "$D/p2.img" 2>/dev/null | python3 -c '
import re, sys
liberi, prima, uninit = [], None, 0
for r in sys.stdin:
    m = re.match(r"Group (\d+):.*\[(.*)\]", r)
    if m:
        g = int(m.group(1))
        if "INODE_UNINIT" in m.group(2):
            uninit += 1
            if prima is None: prima = g
        continue
    m = re.search(r"(\d+) free inodes", r)
    if m: liberi.append(int(m.group(1)))
print(sum(liberi[:prima]) + 3 if prima is not None else 0, uninit)')
[ "$K_FILE" -gt 0 ] && ok "p2: $IU2 gruppi INODE_UNINIT, $K_FILE file per arrivarci" \
    || no "p2: nessun gruppo INODE_UNINIT"
$SBIN/mke2fs -q -F -t ext4 -O ^metadata_csum,uninit_bg,^64bit -b 4096 -L ext4-gdt \
    -d "$D/albero" "$D/p4.img" $((S4 / 8)) || exit 1
# p1 and p4 are one group each; p2 (1 KB blocks, 8 groups) has some never initialised
U2=$($SBIN/dumpe2fs "$D/p2.img" 2>/dev/null | grep -c "BLOCK_UNINIT")
[ "$U2" -gt 0 ] && ok "p2: $U2 gruppi BLOCK_UNINIT" || no "p2: nessun gruppo BLOCK_UNINIT"
$SBIN/dumpe2fs -h "$D/p1.img" 2>/dev/null | grep "features:" | sed 's/^/        /'

echo "contenuto A vecchio" > "$D/rec/vittima.txt"
echo "seconda vittima originale" > "$D/rec/vittima2.txt"
$SBIN/mke2fs -q -F -t ext4 -b 4096 -L ripristino -d "$D/rec" "$D/p3.img" $((S3 / 8)) || exit 1
B1=$($SBIN/debugfs -R "bmap /vittima.txt 0" "$D/p3.img" 2>/dev/null)
B2=$($SBIN/debugfs -R "bmap /vittima2.txt 0" "$D/p3.img" 2>/dev/null)
(printf 'contenuto B dal giornale\n'; head -c 4096 /dev/zero) | head -c 4096 > "$D/b.bin"
(printf 'NON deve comparire\n'; head -c 4096 /dev/zero) | head -c 4096 > "$D/c.bin"
printf 'jo -c\njw -b %s %s\njw -b %s %s\njw -r %s\njc\n' "$B1" "$D/b.bin" "$B2" "$D/c.bin" "$B2" \
    | $SBIN/debugfs -w -f - "$D/p3.img" > "$D/debugfs-jw.log" 2>&1
$SBIN/dumpe2fs -h "$D/p3.img" 2>/dev/null | grep -q needs_recovery \
    && $SBIN/dumpe2fs -h "$D/p3.img" 2>/dev/null | grep -q "Journal features:.*checksum_v3" \
    && ok "p3: debugfs ha lasciato needs_recovery e un giornale CSUM_V3" \
    || no "p3: niente giornale CSUM_V3 da riapplicare (vedi $D/debugfs-jw.log)"
for n in 1 2 3 4; do eval "dentro \$O$n $D/p$n.img"; done

# --- run A ----------------------------------------------------------------------
echo "=== 2. EX-OS: ripristino con checksum, scritture su tre ext4 ==="
export EXOS_ISTANZA=e4$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
passi=("modulo@3"
       "mount hd0p3 /r@8" "cat /r/vittima.txt@2" "cat /r/vittima2.txt@2"
       "echo dopo il ripristino > /r/dopo.txt@3" "mkdir /r/cartella@3" "umount /r@3"
       "mount hd0p1 /a@8" "mount hd0p2 /b@8" "mount hd0p4 /c@8" "mount@2")
passi+=("/b/estesi -f /b/molti $K_FILE@~300" "/b/discovel /b/riempi.bin 24 65536@~300")
for v in a b c; do
    passi+=("mkdir /$v/nuova@2" "cp /bin/ls /$v/nuova/ls@4" "mv /$v/nuova/ls /$v/ls2@3"
            "echo scritto da ex-os > /$v/eco.txt@3" "mkdir /$v/vuota@2" "rmdir /$v/vuota@2"
            "rm /$v/da-togliere.txt@3"
            "echo nuovo > /$v/molti/aggiunto.txt@3" "rm /$v/molti/un-nome-abbastanza-lungo-007@3"
            "/$v/estesi /$v/est.bin $N_EST@~240"
            "/$v/discovel /$v/t4k.bin 4 4096@~240" "/$v/discovel /$v/t64k.bin 4 65536@~240")
done
passi+=("umount /a@4" "umount /b@4" "umount /c@4")
timeout 2400 python3 tools/qemu_drive.py "${passi[@]}" > "$D/a.log" 2>&1
cp "/tmp/exos/seriale4$ARCH.txt" "$D/a-seriale.txt" 2>/dev/null
L=$(tr -d '\r' < "$D/a.log")
SER=$(tr -d '\r' < "$D/a-seriale.txt" 2>/dev/null)

grep -q "ext4.mod 0.0[0-9]*: ext2, ext3 and ext4 writing" <<<"$SER" \
    && ok "ext4.mod caricato, scrive ext4" || no "il modulo non dice ext4 (vedi $D/a-seriale.txt)"
grep -q "READ-ONLY" <<<"$SER" && no "un volume e' rimasto in sola lettura (vedi $D/a-seriale.txt)"
grep -q "filled up in the middle" <<<"$SER" \
    && no "una transazione si e' riempita a meta' operazione" \
    || ok "nessuna transazione riempita a meta' operazione"
grep -A3 "cat /r/vittima.txt" <<<"$L" | grep -q "contenuto B dal gior" \
    && ok "p3: il giornale CSUM_V3 scritto da debugfs e' stato riapplicato" \
    || no "p3: vittima.txt non ha il contenuto del giornale"
grep -A3 "cat /r/vittima2.txt" <<<"$L" | grep -q "seconda vittima originale" \
    && ok "p3: il blocco revocato non e' stato riapplicato" \
    || no "p3: il blocco revocato e' stato riapplicato"
grep -q "^ESTESI FILE $K_FILE PULITO" <<<"$L" && ok "p2: $K_FILE file creati" \
    || no "p2: estesi -f (vedi $D/a.log)"
[ "$(grep -c '^ESTESI FINE PULITO' <<<"$L")" -eq 3 ] \
    && ok "estesi: tre volte pulito (scritto, tagliato, ricresciuto, 4 MB in una write)" \
    || no "estesi (vedi $D/a.log)"
grep '^ESTESI ERRORE' <<<"$L" | sed 's/^/        /'
grep -q "DISCOVEL ERRORI" <<<"$L" && no "discovel: byte riletti diversi"
[ "$(grep -c '^DISCOVEL' <<<"$L")" -eq 28 ] \
    && ok "discovel: 28 misure, nessun byte sbagliato" || no "discovel: mancano misure (vedi $D/a.log)"
grep '^DISCOVEL' <<<"$L" | sed 's/^/        /'

python3 - "$D/est-atteso.bin" "$N_EST" <<'PY'
import sys
n = int(sys.argv[2]); P = 4096
d = bytearray((2 * n - 1) * P)
for o in range(len(d)):
    if (o // P) % 2 == 0: d[o] = (o * 131 + (o >> 12) * 7) & 0xFF
open(sys.argv[1], "wb").write(d)
g = bytearray(4 * 1024 * 1024)
for o in range(len(g)): g[o] = (o * 131 + (o >> 12) * 7) & 0xFF
open(sys.argv[1] + ".g", "wb").write(g)
PY

controlla() {   # $1 name, $2 image, $3 1 = with the written files, $4 extent depth at least
    local n="$1" f="$2"
    if $SBIN/e2fsck -fn "$f" > "$D/e2fsck-$n.log" 2>&1; then ok "$n: e2fsck pulito"
    else no "$n: e2fsck (vedi $D/e2fsck-$n.log)"; sed -n '1,12p' "$D/e2fsck-$n.log" | sed 's/^/        /'; fi
    $SBIN/dumpe2fs -h "$f" 2>/dev/null | grep -q needs_recovery \
        && no "$n: needs_recovery ancora acceso" || ok "$n: needs_recovery spento"
    [ "$3" = 1 ] || return
    $SBIN/debugfs -R "dump /ls2 $D/$n-ls2" "$f" > /dev/null 2>&1
    cmp -s "$D/$n-ls2" "$LS" && ok "$n: /ls2 identico a /bin/ls" || no "$n: /ls2 diverso"
    [ "$($SBIN/debugfs -R "cat /eco.txt" "$f" 2>/dev/null)" = "scritto da ex-os" ] \
        && ok "$n: /eco.txt giusto" || no "$n: /eco.txt"
    $SBIN/debugfs -R "stat /da-togliere.txt" "$f" 2>&1 | grep -q "not found" \
        && ok "$n: /da-togliere.txt tolto" || no "$n: /da-togliere.txt c'e' ancora"
    $SBIN/debugfs -R "ls /molti" "$f" > "$D/$n-molti.txt" 2>&1
    grep -q "aggiunto.txt" "$D/$n-molti.txt" && ! grep -q "lungo-007 " "$D/$n-molti.txt" \
        && [ "$($SBIN/debugfs -R "cat /molti/un-nome-abbastanza-lungo-399" "$f" 2>/dev/null)" = "file 399" ] \
        && ok "$n: /molti: aggiunto, tolto, gli altri al loro posto" || no "$n: /molti (vedi $D/$n-molti.txt)"
    $SBIN/debugfs -R "dump /est.bin $D/$n-est.bin" "$f" > /dev/null 2>&1
    $SBIN/debugfs -R "dump /est.bin.g $D/$n-est.bin.g" "$f" > /dev/null 2>&1
    cmp -s "$D/$n-est.bin" "$D/est-atteso.bin" && cmp -s "$D/$n-est.bin.g" "$D/est-atteso.bin.g" \
        && ok "$n: est.bin e est.bin.g identici a quanto atteso" || no "$n: est.bin diverso"
    if [ -n "$4" ]; then
        local prof
        prof=$($SBIN/debugfs -R "ex /est.bin" "$f" 2>/dev/null | awk 'NR>1 && $1 ~ /\/$/ {if ($2+0 > m) m = $2+0} END {print m+0}')
        [ "$prof" -ge "$4" ] && ok "$n: l'albero degli extent di est.bin e' profondo $prof" \
            || no "$n: albero profondo $prof, ne servivano $4"
    fi
}
fuori $O1 "$D/p1-dopo.img" $S1; controlla p1 "$D/p1-dopo.img" 1 1
fuori $O2 "$D/p2-dopo.img" $S2; controlla p2 "$D/p2-dopo.img" 1 2
U2D=$($SBIN/dumpe2fs "$D/p2-dopo.img" 2>/dev/null | grep -c "BLOCK_UNINIT")
[ "$U2D" -lt "$U2" ] && ok "p2: EX-OS ha inizializzato $((U2 - U2D)) gruppi BLOCK_UNINIT" \
    || no "p2: nessun gruppo BLOCK_UNINIT usato (prima $U2, dopo $U2D)"
IU2D=$($SBIN/dumpe2fs "$D/p2-dopo.img" 2>/dev/null | grep -c "INODE_UNINIT")
[ "$IU2D" -lt "$IU2" ] && ok "p2: EX-OS ha preso inode da $((IU2 - IU2D)) gruppi INODE_UNINIT" \
    || no "p2: nessun gruppo INODE_UNINIT usato (prima $IU2, dopo $IU2D)"
K1=$(printf 'f%04d' $((K_FILE - 1)))
[ "$($SBIN/debugfs -R "cat /molti/$K1" "$D/p2-dopo.img" 2>/dev/null)" = "file ${K1#f}" ] \
    && ok "p2: /molti/$K1 giusto" || no "p2: /molti/$K1"
fuori $O4 "$D/p4-dopo.img" $S4; controlla p4 "$D/p4-dopo.img" 1 1
fuori $O3 "$D/p3-dopo.img" $S3; controlla p3 "$D/p3-dopo.img" 0
[ "$($SBIN/debugfs -R "cat /dopo.txt" "$D/p3-dopo.img" 2>/dev/null)" = "dopo il ripristino" ] \
    && ok "p3: scritto dopo il ripristino" || no "p3: /dopo.txt"

# --- our journal, with checksums, recovered by Linux's e2fsck ----------------------
echo "=== 3. il giornale CSUM_V3 scritto da EX-OS, riapplicato da e2fsck ==="
cp "$D/p3-dopo.img" "$D/p3-crash.img"
JB=$($SBIN/debugfs -R "bmap <8> 0" "$D/p3-crash.img" 2>/dev/null)
python3 - "$D/p3-crash.img" "$JB" 4096 <<'PY'
import sys, struct
f, jb, bs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = bytearray(open(f, "rb").read())
o = jb * bs
first, seq = struct.unpack(">II", d[o + 0x14:o + 0x1C])
# the last transaction EX-OS committed starts at s_first with s_sequence - 1
d[o + 0x18:o + 0x20] = struct.pack(">II", seq - 1, first)
# the journal superblock's crc32c (jbd2_superblock_csum): over 1024 bytes, s_checksum zero
def crc32c(crc, b):
    for x in b:
        crc ^= x
        for _ in range(8): crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc
d[o + 0xFC:o + 0x100] = b"\0\0\0\0"
d[o + 0xFC:o + 0x100] = struct.pack(">I", crc32c(0xFFFFFFFF, d[o:o + 1024]))
sb = 1024
inc = struct.unpack("<I", d[sb + 96:sb + 100])[0] | 0x4          # needs_recovery
d[sb + 96:sb + 100] = struct.pack("<I", inc)
d[sb + 0x3FC:sb + 0x400] = struct.pack("<I", crc32c(0xFFFFFFFF, d[sb:sb + 0x3FC]))
open(f, "wb").write(d)
print("        transazione %d, dal blocco %d del giornale" % (seq - 1, first))
PY
$SBIN/dumpe2fs -h "$D/p3-crash.img" 2>/dev/null | grep -q "Journal features:.*checksum_v3" \
    && ok "p3: il giornale e' ancora CSUM_V3" || no "p3: il giornale ha perso CSUM_V3"
$SBIN/debugfs -R "logdump" "$D/p3-crash.img" > "$D/logdump.txt" 2>&1
grep -q "commit block" "$D/logdump.txt" \
    && ok "debugfs logdump legge descrittore e commit di EX-OS" \
    || no "logdump non trova una transazione completa (vedi $D/logdump.txt)"
$SBIN/e2fsck -fy "$D/p3-crash.img" > "$D/e2fsck-recupero.log" 2>&1
grep -q "recovering journal" "$D/e2fsck-recupero.log" && ! grep -qi "checksum" "$D/e2fsck-recupero.log" \
    && ok "e2fsck riapplica la transazione di EX-OS, checksum giusti" \
    || no "e2fsck non riapplica (vedi $D/e2fsck-recupero.log)"
$SBIN/e2fsck -fn "$D/p3-crash.img" > "$D/e2fsck-dopo-recupero.log" 2>&1 \
    && ok "dopo il recupero e2fsck pulito" || no "dopo il recupero (vedi $D/e2fsck-dopo-recupero.log)"

# --- run B: no umount ------------------------------------------------------------
echo "=== 4. scritture su p1 e QEMU fermato senza umount ==="
export EXOS_ISTANZA=e4b$ARCH
timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@8" \
    "echo prima del crash > /a/crash.txt@3" "mkdir /a/cartella-crash@3" \
    "cp /bin/ls /a/cartella-crash/ls@4" > "$D/b.log" 2>&1
fuori $O1 "$D/p1-crash.img" $S1
$SBIN/dumpe2fs -h "$D/p1-crash.img" 2>/dev/null | grep -q needs_recovery \
    && ok "p1: needs_recovery acceso (montato e non smontato)" || no "p1: needs_recovery spento senza umount"
$SBIN/e2fsck -fy "$D/p1-crash.img" > "$D/e2fsck-p1-y.log" 2>&1
$SBIN/e2fsck -fn "$D/p1-crash.img" > "$D/e2fsck-p1-crash.log" 2>&1 \
    && ok "p1: dopo e2fsck -fy pulito" || no "p1 (vedi $D/e2fsck-p1-crash.log)"
[ "$($SBIN/debugfs -R "cat /crash.txt" "$D/p1-crash.img" 2>/dev/null)" = "prima del crash" ] \
    && ok "p1: /crash.txt c'e'" || no "p1: /crash.txt perso"
$SBIN/debugfs -R "dump /cartella-crash/ls $D/p1-crash-ls" "$D/p1-crash.img" > /dev/null 2>&1
cmp -s "$D/p1-crash-ls" "$LS" && ok "p1: il file copiato prima del crash e' intero" \
    || no "p1: il file copiato prima del crash non e' intero"
exit $esito
