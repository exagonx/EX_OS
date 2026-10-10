#!/bin/sh
# =============================================================================
# tools/libc32-uguale.sh — la libc e i programmi a 32 bit sono rimasti uguali?
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/libc32-uguale.sh -base     prende la base (PRIMA di una modifica)
#     tools/libc32-uguale.sh           ricompila e confronta con la base
#
# E' il gemello di tools/kernel32-uguale.sh per cio' che gira in ring 3
# (@EXOS-64, tappa 4). I sorgenti della libc, della shell e dei comandi
# diventano COMUNI alle due architetture: ogni ritocco fatto per i 64 bit deve
# lasciare il binario a 32 bit com'era, istruzione per istruzione. Qui si
# compilano con le opzioni della versione a 32 bit - la libc nei suoi due modi
# (condivisa e no), l'avvio, i ponti, e ogni .c di bin/ - e si confronta il
# disassemblato.
#
# ! COMPILA SOLTANTO, in /tmp: non collega e non tocca build/. Un programma
# che non compila (gli manca un'intestazione generata, per esempio) conta
# come «non compilato» nella base e deve restarlo dopo: non e' un errore.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="cross_build/$MACCHINA/libc32-base.txt"
T=/tmp/exos-libc32-uguale
rm -rf "$T"; mkdir -p "$T"

CF="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin \
    -fstack-protector-strong -mstack-protector-guard=global -fno-pic -fno-pie \
    -Wall -O2 -std=c11 -nostdlib -ffunction-sections -fdata-sections -w"
INC="-Ilib/include -Ilib -Idrivers -Idrivers/net -Idrivers/kbd -Idrivers/tty"
for d in lib/ex* drivers/* bin/gfedit exwin/server exwin/include; do [ -d "$d" ] && INC="$INC -I$d"; done

uno() {     # $1 = sorgente, $2 = nome, il resto = opzioni in piu'
    s="$1"; n="$2"; shift 2
    if gcc $CF $INC "$@" -c "$s" -o "$T/$n.o" 2>/dev/null; then
        printf '%s %s\n' "$(objdump -d --no-show-raw-insn "$T/$n.o" | sed '1,6d' | md5sum | cut -d' ' -f1)" "$n"
    else
        printf '%s %s\n' "non-compila" "$n"
    fi
}
impronte() {
    uno lib/libc.c       libc
    uno lib/libc.c       libc_so -fno-stack-protector -DEXOS_LIBC_SO
    uno lib/libc_avvio.c libc_avvio
    uno lib/libc_ponti.c libc_ponti
    for s in lib/rete.c lib/dns.c lib/audio.c lib/wifi.c lib/ex*/*.c bin/*/*.c; do
        uno "$s" "$(echo "$s" | tr '/' '_')" -I"$(dirname "$s")"
    done
}

if [ "${1:-}" = "-base" ]; then
    mkdir -p "$(dirname "$B")"
    impronte > "$B"
    echo "base presa: $(wc -l < "$B") sorgenti, $(grep -c '^non-compila' "$B") non compilano da soli ($B)"
    exit 0
fi
[ -f "$B" ] || { echo "non c'e' una base: prima  tools/libc32-uguale.sh -base"; exit 1; }
impronte > "$T/adesso.txt"
diversi=$(sort "$B" "$T/adesso.txt" | uniq -u | awk '{print $2}' | sort -u)
if [ -z "$diversi" ]; then
    echo "32 bit (ring 3): istruzioni identiche in tutti i $(wc -l < "$T/adesso.txt") sorgenti"
else
    echo "32 bit (ring 3): CAMBIATI rispetto alla base:"
    echo "$diversi" | sed 's/^/    /'
    exit 1
fi
