#!/bin/bash
# =============================================================================
# tools/prova_guardia.sh - the countdown of /bin/guardia (kernel 0.299)
#
# At 32 and 64 bit: called again in time the machine stays up past the
# countdown, `guardia 0` turns it off, and with no call it resets and boots
# again. The reset is counted from the serial log, not assumed.
#     tools/prova_guardia.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-guardia}"
rm -rf "$D"; mkdir -p "$D"
esito=0

prova() {   # nome  programma  qemu  [variabili del CD]
    local nome="$1" prog="$2"
    rm -rf "$D/albero"; mkdir -p "$D/albero"
    cp "$prog" "$D/albero/gu" || exit 1
    qemu-img create -f raw "$D/hd$nome.img" 20M > /dev/null
    /usr/sbin/mke2fs -q -F -t ext2 -b 4096 -d "$D/albero" "$D/hd$nome.img"
    export EXOS_ISTANZA="gu$nome" EXOS_RAM=512M
    export EXOS_QEMU_EXTRA="-drive file=$D/hd$nome.img,format=raw,if=ide"
    timeout 170 python3 tools/qemu_drive.py "mount hd0 /a@5" \
        "/a/gu 20@1" "echo PRIMA@8" "/a/gu 20@1" "echo SECONDA@8" \
        "/a/gu 20@1" "echo TERZA@8" "/a/gu 0@1" "echo SPENTA@25" \
        "echo VIVA@2" "/a/gu 6@1" "echo ASPETTO@12" "echo DOPO@3" \
        > "$D/q$nome.log" 2>&1
    L="$D/s$nome.txt"
    tr -d '\r' < "/tmp/exos/serialgu$nome.txt" | sed 's/\x1b\[[0-9;]*m//g' > "$L"
    local vivo scatti dopo
    vivo=$(grep -ac '^VIVA' "$L")
    scatti=$(grep -ac 'guardia: nessuna chiamata in tempo, riavvio' "$L")
    dopo=$(grep -ac '^DOPO' "$L")
    # qemu_drive runs with -no-reboot: a real reset ends QEMU, so DOPO never
    # gets an answer. A machine that stopped without resetting would fail the
    # same way, hence the line of power_reset_ora too.
    if [ "$vivo" = 1 ] && [ "$scatti" = 1 ] && [ "$dopo" = 0 ] &&
       grep -aq '^  Riavvio: ' "$L"; then
        echo "  [OK] $nome: resta su se richiamata, spenta non scatta, senza chiamate riavvia"
    else
        echo "  [NO] $nome: viva $vivo, scatti $scatti, dopo $dopo: $L"; esito=1
    fi
}

EXOS_QEMU=qemu-system-i386 prova 32 build/bin-cd/guardia
export EXOS_QEMU=qemu-system-x86_64 EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos64.iso
prova 64 build-64/bin/guardia
[ "$esito" = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
