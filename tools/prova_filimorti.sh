#!/bin/bash
# =============================================================================
# tools/prova_filimorti.sh - threads of a program that exits while they read
# (analisi_kernel voce 6)
#
#     tools/prova_filimorti.sh [dir]                    32 bit, 1 and 2 processors
#     EXOS_ARCH=x86_64 tools/prova_filimorti.sh [dir]   64 bit
#     FILIMORTI_ISO=old.iso tools/prova_filimorti.sh    against another kernel
#
# tools/prove/filimorti.c starts itself 200 times with 8 threads reading a
# 4 MB file, and the main thread exits after 0-49 ms. Up to 0.329 a thread
# made a zombie inside read() kept the file lock and every open stopped;
# 0.330 takes the lock back from the dead ("taken over" in the log); since
# 0.338 the threads end by themselves when they leave the kernel, so the
# lock never stays with a dead one.
# PASS: "filimorti: 200 su 200", `ls` answers, no "taken over", no "non
# finisce da solo", no FAULT.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/filimorti-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
SBIN=/usr/sbin
X=cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

if [ "$ARCH" = x86_64 ]; then
    ISO=${FILIMORTI_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M; BASE="-cpu qemu64 $KVM"
else
    ISO=${FILIMORTI_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M; BASE="$KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/filimorti.c -o "$D/albero/filimorti" -lpthread || exit 1
chmod 755 "$D/albero/filimorti"

esito=0
for cpu in 1 2; do
    IMG="$D/hd$cpu.img"
    qemu-img create -f raw "$IMG" 40M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=79872, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
    fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" 39936 || exit 1
    dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

    echo "=== $ARCH, $cpu processori ${KVM:+(KVM)}, $ISO ==="
    EXOS_ISTANZA=fm$ARCH$cpu EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
        EXOS_QEMU_EXTRA="$BASE -smp $cpu -drive file=$IMG,format=raw,if=ide" \
        timeout 1500 python3 tools/qemu_drive.py \
        "mount hd0p1 /a@5" "/a/filimorti /a 200@~1200" "ls /a@5" "umount /a@5" "echo FINE-FM@1" > "$D/q$cpu.log" 2>&1
    S=/tmp/exos/serialfm$ARCH$cpu.txt

    if grep -aq 'filimorti: 200 su 200' "$D/q$cpu.log"; then echo "  [OK]  filimorti: 200 su 200"
    else echo "  [NO]  $(grep -a 'filimorti:' "$D/q$cpu.log" | tail -1) (vedi $D/q$cpu.log)"; esito=1; fi
    if grep -aq 'grosso' "$D/q$cpu.log"; then echo "  [OK]  ls risponde"
    else echo "  [NO]  ls non risponde"; esito=1; fi
    n=$(grep -ac 'taken over' "$S" "$D/q$cpu.log" | awk -F: '{s+=$NF} END {print s+0}')
    if [ "$n" = 0 ]; then echo "  [OK]  nessun lucchetto ripreso a un morto"
    else echo "  [NO]  $n lucchetti ripresi a un morto"; esito=1; fi
    n=$(grep -ac 'non finisce da solo' "$S" "$D/q$cpu.log" | awk -F: '{s+=$NF} END {print s+0}')
    if [ "$n" = 0 ]; then echo "  [OK]  ogni gruppo e' finito da solo"
    else echo "  [NO]  $n gruppi fermati a forza dopo 2 s"; esito=1; fi
    grep -aq 'FAULT\|PANIC' "$D/q$cpu.log" && { echo "  [NO]  FAULT o PANIC nel log"; esito=1; }
done
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
