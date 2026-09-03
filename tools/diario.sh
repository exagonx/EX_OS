#!/bin/sh
# =============================================================================
# tools/diario.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Si sfoglia RIPRENDERE.md senza aprirlo tutto.
#
# ! IL DIARIO E' MEZZO MEGABYTE e cresce ogni giornata di lavoro. Leggerlo per
#   intero per trovare una data e' lo spreco che questo script toglie: l'elenco
#   costa due schermi, una sezione costa quel che pesa lei.
#
# ! E NON PROVA A DIRE CHE COSA E' APERTO. Quello sta in in_lavorazione.txt,
#   scritto a mano. Indovinarlo qui dalla prosa vorrebbe dire fidarsi della
#   parola FATTO, che meta' delle voci non ha e meta' delle note sotto una voce
#   FATTA smentisce: uno strumento che mente e' peggio di un file lungo.
#
#   diario.sh                  quante sezioni ci sono e come si usa
#   diario.sh elenco [parola]  le sezioni, numerate (solo quelle che la contengono)
#   diario.sh mostra N         stampa la sezione N
#   diario.sh cerca PAROLA     le righe che la contengono, con la sezione accanto
# =============================================================================

set -u

QUI=$(dirname "$0")
DIARIO="$QUI/../RIPRENDERE.md"

if [ ! -f "$DIARIO" ]; then
    echo "diario.sh: non trovo $DIARIO" >&2
    exit 1
fi

# L'elenco delle sezioni: numero, riga nel file, titolo.
elenco() {
    filtro=${1:-}
    awk -v filtro="$filtro" '
        /^## / {
            n++
            titolo = substr($0, 4)
            if (filtro == "" || index(tolower(titolo), tolower(filtro)))
                printf "%3d  r%-6d %s\n", n, NR, titolo
        }
    ' "$DIARIO"
}

mostra() {
    awk -v voluta="$1" '
        /^## / { n++ }
        n == voluta { print }
        n > voluta { exit }
    ' "$DIARIO"
}

cerca() {
    awk -v ago="$1" '
        /^## / { n++; titolo = substr($0, 4) }
        index(tolower($0), tolower(ago)) && !/^## / {
            printf "[%d] %s\n    r%d: %s\n", n, titolo, NR, $0
        }
    ' "$DIARIO"
}

case "${1:-}" in
    elenco) elenco "${2:-}" ;;
    mostra) mostra "${2:?serve il numero della sezione}" ;;
    cerca)  cerca  "${2:?serve la parola da cercare}" ;;
    *)
        n=$(grep -c '^## ' "$DIARIO")
        r=$(wc -l < "$DIARIO")
        echo "RIPRENDERE.md: $n sezioni, $r righe. E' un DIARIO, non un elenco di compiti."
        echo "I compiti ancora aperti stanno in in_lavorazione.txt."
        echo
        sed -n 's/^#   //p' "$0" | tail -4
        ;;
esac
