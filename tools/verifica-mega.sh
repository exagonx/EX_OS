#!/bin/bash
# =============================================================================
# tools/verifica-mega.sh — cio' che ho modificato e' davvero su MEGA?
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/verifica-mega.sh [--ricarica] [file...]
#
# Senza file guarda quelli che git vede modificati o nuovi (tranne build/ e
# dist/), piu' scambio.txt, messaggio-commit.txt e in_lavorazione.txt. Per
# ognuno confronta misura e data sul disco con l'impronta dell'ultimo
# caricamento che MEGAsync ha scritto nei suoi log. --ricarica aggiorna la data
# di quelli rimasti indietro, cosi' MEGAsync li rivede e li carica.
#
# ! PERCHE' ESISTE (29 settembre 2026): nove file — libc.c, libc.h,
# version.h fra questi — erano rimasti su MEGA alla versione di un'ora e mezza
# prima, con MEGAsync che si dichiarava in pari («Syncing = 0 Stalled = 0»).
# scambio.txt invece era arrivato: dall'altro PC il lavoro risultava chiuso e
# il codice non c'era. Prima di scrivere LASCIO, si lancia questo.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
LOG="$HOME/.local/share/data/Mega Limited/MEGAsync/logs"
[ -d "$LOG" ] || { echo "non trovo i log di MEGAsync in $LOG" >&2; exit 2; }
RICARICA=0
[ "$1" = "--ricarica" ] && { RICARICA=1; shift; }

CARICATI=$(mktemp)
for f in "$LOG"/*.log; do zcat -f "$f" 2>/dev/null; done | \
    grep -a "~SyncUpload_inClient()\] Name: '.*exa_os/" | \
    sed "s/.*exa_os\/\([^']*\)'.*Fingerprint: \([0-9]*:[0-9]*\).*/\1 \2/" > "$CARICATI"

if [ $# -eq 0 ]; then
    set -- $( { git status --short --untracked-files=all | grep -v " build/\| dist/\| .megaignore$" | awk '{print $2}';
               echo scambio.txt; echo messaggio-commit.txt; echo in_lavorazione.txt; } | sort -u)
fi

indietro=()
for f in "$@"; do
    [ -f "$f" ] || continue
    s=$(stat -c %s "$f"); t=$(stat -c %Y "$f")
    # ! SI CERCA UN CARICAMENTO CON QUESTA MISURA E QUESTA DATA, non «l'ultimo»:
    # i log ruotano, e l'ordine dei file non e' l'ordine del tempo.
    grep -aqx "$f $s:$t" "$CARICATI" && continue
    if grep -aq "^$f " "$CARICATI"; then stato=VECCHIO; else stato=MAI; fi
    printf "%-8s %s\n" "$stato" "$f"
    indietro+=("$f")
done
rm -f "$CARICATI"

[ ${#indietro[@]} -eq 0 ] && { echo "tutto su MEGA"; exit 0; }
echo "${#indietro[@]} file non ancora su MEGA (MAI: nessun caricamento nei log, che ruotano)"
if [ $RICARICA = 1 ]; then
    touch "${indietro[@]}"
    echo "data aggiornata: MEGAsync li ricarica; ricontrollare fra un minuto"
fi
exit 1
