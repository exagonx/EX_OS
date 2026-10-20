#!/bin/bash
# =============================================================================
# tools/prova_dl.sh — dlopen e dlsym sulle librerie di EX-OS (@DLOPEN)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     bash tools/prova_dl.sh
#
# Compila tools/prove/dlprova.c con i386-exos-gcc contro una libc.a
# fatta QUI dai sorgenti di adesso (lib/libc.c e lib/include), non contro
# quella installata nella toolchain: la prova deve dire se il codice di oggi
# funziona, non se qualcuno ha rifatto prepara-cross.sh. Poi lo fa girare in
# QEMU col CD dist/exos.iso (o EXOS_CDROM) attraverso
# tools/exilla/esegui-in-exos.sh.
#
# Il kernel non cambia: dlopen e' tutta nella libc.a, sopra SYS_LIB_APRI e
# la tabella ExLibTesta delle librerie di EX-OS (lib/include/exlib.h).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TCB="$PWD/cross_build/$MACCHINA/exos-cross/bin"
[ -x "$TCB/i386-exos-gcc" ] || TCB="$PWD/cross_build/exos-cross/bin"
D="$PWD/cross_build/$MACCHINA/costruzione-prova"
mkdir -p "$D/lib"

# Le stesse opzioni di tools/gcc-exos/prepara-cross.sh.
CFLAGS="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin \
        -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
        -Wall -O2 -std=c11 -ffunction-sections -fdata-sections"
gcc $CFLAGS -c lib/libc.c -o "$D/lib/libc.o" || exit 1
rm -f "$D/lib/libc.a"
ar rcs "$D/lib/libc.a" "$D/lib/libc.o" || exit 1

"$TCB/i386-exos-gcc" -O2 -Wall -I lib/include -L "$D/lib" \
    tools/prove/dlprova.c -o "$D/dlprova" || exit 1

export CD_FORZATO="${EXOS_CDROM:-$PWD/dist/exos.iso}"
uscita=$(SECONDI=15 bash tools/exilla/esegui-in-exos.sh "$D/dlprova")
echo "$uscita"
echo "$uscita" | grep -q "dlprova: 0 NO" && { echo "=== [OK] dlopen e dlsym ==="; exit 0; }
echo "=== [NO] ==="; exit 1
