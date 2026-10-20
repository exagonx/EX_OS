#!/bin/bash
# tools/exilla/banco-heap.sh — prova l'allocatore di lib/libc.c sull'host
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Estrae da lib/libc.c la sezione dello heap (da HEAP_ALLINEA a
# heap_posix_memalign) e la compila a 32 bit con tools/exilla/banco-heap.c,
# che le da' un finto sbrk sopra un buffer da 512 MB.
set -e
cd "$(dirname "$0")/../.."
D="${TMPDIR:-/tmp}/banco-heap.$$"; mkdir -p "$D"
a=$(grep -n "^#define HEAP_ALLINEA" lib/libc.c | cut -d: -f1)
b=$(grep -n "^static int heap_posix_memalign" lib/libc.c | cut -d: -f1)
sed -n "${a},$((b-1))p" lib/libc.c > "$D/heap_estratto.c"
cp tools/exilla/banco-heap.c "$D/"
gcc -m32 -O2 -Wall -I"$D" -o "$D/banco" "$D/banco-heap.c"
"$D/banco"; r=$?
rm -rf "$D"
exit $r
