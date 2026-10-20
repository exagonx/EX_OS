#!/bin/bash
# =============================================================================
# tools/exilla/prova-sqlite.sh — SQLite dentro EX-OS come lo usa NSS (tappa 5)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compila tools/exilla/prova-sqlite.c contro la SQLite di NSS (quella di
# tools/exilla/nss-costruisci.sh) e la esegue in EX-OS su un disco ext2.
# E' la prova che ha trovato fstat() con st_ino = 0 (kernel 0.228, SYS_FSTAT):
# se NSS non riesce a creare il suo database, si parte da qui.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
N="$B/costruzione-nss/dist"
D="$B/costruzione-prova"
mkdir -p "$D"
[ -f "$N/Release/lib/libsqlite.a" ] || { echo "manca libsqlite.a: tools/exilla/nss-costruisci.sh" >&2; exit 1; }
"$B/exos-cross/bin/i386-exos-gcc" -O2 -Wall -march=pentium-mmx -DEXOS_SOLO_POSIX \
    -I "$N/private/nss" tools/exilla/prova-sqlite.c "$N/Release/lib/libsqlite.a" \
    -o "$D/prova-sqlite" || exit 1

U=$(DISCO_MB=16 tools/exilla/esegui-in-exos.sh "$D/prova-sqlite" 2>&1)
echo "$U" | sed -n '/prova-sqlite: SQLite/,/prova-sqlite: [0-9]* NO/p'
echo "$U" | grep -q "^prova-sqlite: 0 NO" && { echo "=== [OK] SQLite gira dentro EX-OS ==="; exit 0; }
echo "=== [NO] ==="; exit 1
