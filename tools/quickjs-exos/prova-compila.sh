#!/bin/sh
# =============================================================================
# tools/quickjs-exos/prova-compila.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Compila QuickJS PER EX-OS e dice cosa manca, senza costruire niente di
# definitivo.
#
#     tools/quickjs-exos/prova-compila.sh [percorso-quickjs]
#
# PERCHE' ESISTE. Il porting di un motore da mezzo megabyte non comincia
# scrivendo codice: comincia sapendo QUANTO grande viene e QUALI simboli
# chiede a un sistema che non e' quello per cui e' stato scritto. Questo
# script risponde a tutt'e due le domande in venti secondi, e le rifa'
# uguali il giorno che si aggiorna QuickJS o si tocca la libc.
#
# ! I NUMERI CHE STAMPA SONO L'UNICA MISURA CHE VALE. La previsione scritta
# in exwin.ld — «un motore JavaScript, che in un megabyte non ci sta per
# definizione» — e' gia' stata smentita una volta da ExJs. Qui viene
# smentita anche per QuickJS, ma solo perche' qualcuno ha misurato.
# =============================================================================

set -e

QJS="${1:-quickjs}"
FUORI="${EXOS_QJS_OUT:-/tmp/exos-quickjs}"

# ! LA libm E' openlibm COSTRUITA PER IL BERSAGLIO, non quella dell'host: e'
# la stessa che usano i programmi compilati dentro EX-OS. Vedi
# tools/openlibm-exos/ e il commento in testa a lib/include/math.h.
LIBM="${EXOS_LIBM:-$HOME/exos-cross/i386-exos/lib/libm.a}"

# ! E libgcc SERVE PER QUATTRO NOMI SOLI: __divdi3, __moddi3, __udivdi3,
# __umoddi3, cioe' la divisione a 64 bit che l'i386 non ha in hardware. I
# programmi di EX-OS si linkano SENZA libgcc e la libc se la cava a mano
# (vedi div64_10 in lib/libc.c), ma un motore JavaScript i 64 bit li divide
# davvero e ovunque. Sono routine di puri interi, senza sistema operativo
# sotto, e la GCC Runtime Library Exception le lascia linkare anche a un
# programma non-GPLv3.
LIBGCC="${EXOS_LIBGCC:-$(ls -d "$HOME"/exos-cross/lib/gcc/i386-exos/*/libgcc.a 2>/dev/null | head -1)}"

if [ ! -f "$QJS/quickjs.c" ]; then
    echo "prova-compila: '$QJS' non e' un albero di QuickJS." >&2
    exit 1
fi

if ! grep -q "__EXOS__" "$QJS/cutils.h" 2>/dev/null; then
    echo "prova-compila: l'albero non e' adattato. Lancia prima:" >&2
    echo "    python3 tools/quickjs-exos/applica.py $QJS" >&2
    exit 1
fi

mkdir -p "$FUORI"

# Gli stessi flag dei programmi di EX-OS (CFLAGS_USER nel Makefile), piu':
#   -nostdinc     per non pescare gli header del sistema ospite: la libc
#                 dev'essere la NOSTRA, o si compila contro promesse che
#                 dentro EX-OS nessuno mantiene
#   -Os           perche' qui la taglia e' un requisito, non un gusto
#   -D__EXOS__    l'interruttore che le cinque modifiche guardano
GI=$(gcc -m32 -print-file-name=include)
CFLAGS="-m32 -march=pentium-mmx -ffreestanding -fno-builtin -fno-pic -fno-pie \
        -std=c11 -nostdlib -nostdinc -Os -D__EXOS__ -I $GI -I lib/include -I $QJS"

echo "=== Compilo QuickJS per EX-OS ==="
for f in quickjs libregexp libunicode dtoa; do
    printf "  %-11s " "$f"
    gcc $CFLAGS -c "$QJS/$f.c" -o "$FUORI/$f.o"
    size "$FUORI/$f.o" | tail -1 | awk '{printf "text %7d  data %5d  bss %5d\n", $1, $2, $3}'
done

echo ""
echo "=== Metto insieme, con openlibm e i quattro nomi di libgcc ==="
[ -f "$LIBM" ]   || { echo "  manca $LIBM"   >&2; exit 1; }
[ -f "$LIBGCC" ] || { echo "  manca libgcc del bersaglio" >&2; exit 1; }

ld -m elf_i386 -r -o "$FUORI/quickjs-exos.o" \
   "$FUORI/quickjs.o" "$FUORI/libregexp.o" "$FUORI/libunicode.o" "$FUORI/dtoa.o" \
   "$LIBM" "$LIBGCC"

size "$FUORI/quickjs-exos.o" | tail -1 | \
    awk '{printf "  TOTALE: text %d byte (%.0f KB)  data %d  bss %d\n", $1, $1/1024, $2, $3}'

echo ""
echo "=== Cosa chiede ancora, e a chi ==="
echo "  (dev'essere TUTTA libc di EX-OS: se compare altro, e' un buco nuovo)"
nm -u "$FUORI/quickjs-exos.o" | awk '{print $2}' | sort -u | \
    grep -v "^_GLOBAL_OFFSET_TABLE_$" | sed 's/^/    /'

echo ""
echo "Gli oggetti stanno in $FUORI"
