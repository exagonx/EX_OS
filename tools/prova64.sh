#!/bin/sh
# tools/prova64.sh — pilota EX-OS a 64 bit in QEMU dal CD (@EXOS-64)
#
#     tools/prova64.sh "ls /bin@~5" "mem@~5"
#
# Sono gli argomenti di tools/qemu_drive.py; qui si fissano l'emulatore a
# 64 bit, il CD dist/exos64.iso, niente dischetto. La seriale resta in
# /tmp/exos/serial64.txt.
cd "$(dirname "$0")/.." || exit 1
EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=64 EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos64.iso \
EXOS_RAM="${EXOS_RAM:-256M}" EXOS_QEMU_EXTRA="-cpu qemu64 ${EXOS_QEMU_EXTRA:-}" \
    python3 tools/qemu_drive.py "$@"
