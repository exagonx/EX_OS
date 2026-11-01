#!/bin/bash
# =============================================================================
# tools/prova_ahci_smp.sh - the disk on AHCI with several processors (voce 58)
#
# QEMU at 64 bit, 2 and 4 processors, the disk on a real AHCI device: a copy
# while the processors work, compared with sha256sum, and no timeout or false
# timeout in the log. It also shows `dmesg -segui` printing.
#     tools/prova_ahci_smp.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ahcismp}"
rm -rf "$D"; mkdir -p "$D/albero"
[ -f dist/exos64.iso ] || { echo "manca dist/exos64.iso: make iso64" >&2; exit 1; }
cp build-64/bin/smpprova "$D/albero/sp" || exit 1
cp build-64/bin/dmesg "$D/albero/dm" || exit 1
qemu-img create -f raw "$D/hd.img" 40M > /dev/null
/usr/sbin/mke2fs -q -F -t ext2 -b 4096 -d "$D/albero" "$D/hd.img"

export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=ahcismp EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos64.iso EXOS_RAM=512M
esito=0
for n in 2 4; do
    export EXOS_QEMU_EXTRA="-smp $n -device ahci,id=s -drive if=none,id=d,file=$D/hd.img,format=raw -device ide-hd,drive=d,bus=s.0"
    timeout 170 python3 tools/qemu_drive.py "mount hd0 /a@5" "/a/dm -segui &@4" \
        "/a/sp -accendi@5" "/a/sp $n 2@6" "mkdir /a/d$n@2" \
        "cp -r /bin /a/d$n/b &@1" "/a/sp $n 3@8" "sha256sum /bin/ps@3" \
        "sha256sum /a/d$n/b/ps@3" "echo FINE-AHCI@2" > "$D/q$n.log" 2>&1
    L="$D/s$n.txt"
    tr -d '\r' < /tmp/exos/serialahcismp.txt | sed 's/\x1b\[[0-9;]*m//g' > "$L"
    h=$(grep -aoE '^[0-9a-f]{64}' "$L" | sort -u | wc -l)
    a=$(grep -ac 'AHCI: porta 0: disco di\|AHCI: porta [0-9]: disco di' "$L")
    t=$(grep -ac 'senza risposta\|porta ripresa\|non riparte' "$L")
    if [ "$h" = 1 ] && [ "$a" -ge 1 ] && [ "$t" = 0 ] && grep -aq '^FINE-AHCI' "$L" &&
       ! grep -aqE '\[FAULT\]|PANIC|ECCEZIONE' "$L"; then
        echo "  [OK] $n processori in AHCI"
    else
        echo "  [NO] $n processori (impronte $h, AHCI $a, timeout $t): $L"; esito=1
    fi
done
[ "$esito" = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
