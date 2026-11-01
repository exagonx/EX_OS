#!/bin/bash
# =============================================================================
# tools/prova_smp64.sh - i processori in piu' lavorano a 64 bit (kernel 0.297)
#
# QEMU a 64 bit con 2 e 4 processori: smpprova -accendi, N programmi insieme,
# e una copia su disco mentre lavorano, confrontata con sha256sum.
#     tools/prova_smp64.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-smp64}"
rm -rf "$D"; mkdir -p "$D/albero"
[ -f dist/exos64.iso ] || { echo "manca dist/exos64.iso: make iso64" >&2; exit 1; }
cp build-64/bin/smpprova "$D/albero/sp" || exit 1
qemu-img create -f raw "$D/hd.img" 40M > /dev/null
/usr/sbin/mke2fs -q -F -t ext2 -b 4096 -d "$D/albero" "$D/hd.img"

export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=smp64 EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos64.iso EXOS_RAM=512M
esito=0
for n in 2 4; do
    export EXOS_QEMU_EXTRA="-smp $n -drive file=$D/hd.img,format=raw,if=ide"
    timeout 170 python3 tools/qemu_drive.py "mount hd0 /a@5" "/a/sp -accendi@5" \
        "/a/sp $n 2@6" "mkdir /a/d$n@2" "cp -r /bin /a/d$n/b &@1" \
        "/a/sp $n 3@8" "sha256sum /bin/ps@3" "sha256sum /a/d$n/b/ps@3" \
        "echo FINE-SMP@2" > "$D/q$n.log" 2>&1
    L="$D/s$n.txt"
    tr -d '\r' < /tmp/exos/serialsmp64.txt | sed 's/\x1b\[[0-9;]*m//g' > "$L"
    v=$(grep -acE "^smpprova: PIU' PROCESSORI LAVORANO" "$L")
    h=$(grep -aoE '^[0-9a-f]{64}' "$L" | sort -u | wc -l)
    if [ "$v" = 2 ] && [ "$h" = 1 ] && grep -aq '^FINE-SMP' "$L" &&
       ! grep -aqE '\[FAULT\]|PANIC|ECCEZIONE' "$L"; then
        echo "  [OK] $n processori"
    else
        echo "  [NO] $n processori (lavorano $v su 2, impronte $h): $L"; esito=1
    fi
done
[ "$esito" = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
