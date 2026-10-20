#!/bin/sh
# =============================================================================
# tools/kernel32-uguale.sh — il kernel a 32 bit e' rimasto lo stesso? (@EXOS-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/kernel32-uguale.sh -base    prende la base: le istruzioni di ogni
#                                       oggetto del kernel com'e' ADESSO
#     tools/kernel32-uguale.sh          ricostruisce e confronta con la base
#
# ! A CHE COSA SERVE. Pulire il codice comune per i 64 bit vuol dire cambiare
# centinaia di righe - un tipo, una macro al posto del nome di un registro -
# che a 32 bit NON devono cambiare niente. «Non deve cambiare niente» si puo'
# verificare alla lettera: si disassembla ogni oggetto del kernel prima e
# dopo, e si confronta. Se le istruzioni sono le stesse, la modifica a 32 bit
# non esiste. E' il controllo che permette di andare veloci senza provare
# tutto il sistema a ogni passo.
#
# ! QUANDO UN OGGETTO CAMBIA NON E' PER FORZA UN ERRORE - una modifica voluta
# cambia le istruzioni - ma allora lo si sa, e si guarda quale. Dopo una
# modifica voluta la base si riprende con -base.
#
# Lavora nella copia privata (tools/exilla/costruisci-privato.sh), come ogni
# costruzione di prova. La base sta in cross_build/, fuori da git.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
P="cross_build/$MACCHINA/costruzione-sistema/albero"
B="cross_build/$MACCHINA/kernel32-base.txt"

impronte() {
    (cd "$P/build/kernel" && find . -name '*.o' | sort | while read -r o; do
        printf '%s %s\n' "$(objdump -d --no-show-raw-insn "$o" | sed '1,6d' | md5sum | cut -d' ' -f1)" "$o"
    done)
}

tools/exilla/costruisci-privato.sh kernel > /tmp/exos-kernel32-uguale.log 2>&1 || {
    echo "la costruzione a 32 bit e' FALLITA: /tmp/exos-kernel32-uguale.log"
    grep -n ' error' /tmp/exos-kernel32-uguale.log | head -5
    exit 1
}

if [ "${1:-}" = "-base" ]; then
    impronte > "$B"
    echo "base presa: $(wc -l < "$B") oggetti ($B)"
    exit 0
fi
[ -f "$B" ] || { echo "non c'e' una base: prima  tools/kernel32-uguale.sh -base"; exit 1; }

impronte > /tmp/exos-kernel32-adesso.txt
diversi=$(sort "$B" /tmp/exos-kernel32-adesso.txt | uniq -u | awk '{print $2}' | sort -u)
if [ -z "$diversi" ]; then
    echo "32 bit: istruzioni identiche in tutti i $(wc -l < /tmp/exos-kernel32-adesso.txt) oggetti"
else
    echo "32 bit: CAMBIATI rispetto alla base:"
    echo "$diversi" | sed 's/^/    /'
    exit 1
fi
