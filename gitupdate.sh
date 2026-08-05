#!/bin/bash
#
# Aggiunge tutto, committa e spinge sul ramo corrente.
#
# Il messaggio del commit si scrive in messaggio-commit.txt, nella radice
# del repository:
#
#   prima riga  -> il titolo
#   riga vuota  -> separatore (obbligatoria per git)
#   dal terzo   -> il corpo, lungo quanto serve
#
# Se quel file non c'e' (o e' vuoto) si torna al comportamento di prima:
# git apre l'editor e il messaggio si scrive li'.
#
# A push riuscito il file NON resta in giro — verrebbe riusato per sbaglio
# al giro dopo — ma viene spostato in .git/ultimo-messaggio-commit.txt,
# cosi' nulla va perso.

set -euo pipefail

cd "$(dirname "$0")"

MESSAGGIO="messaggio-commit.txt"
ARCHIVIO=".git/ultimo-messaggio-commit.txt"

git add .

if git diff --cached --quiet; then
    echo "Niente da committare: l'area di stage e' vuota."
    exit 0
fi

if [ -s "$MESSAGGIO" ]; then
    echo "Messaggio preso da $MESSAGGIO:"
    echo "---------------------------------------------"
    cat "$MESSAGGIO"
    echo "---------------------------------------------"
    # --cleanup=strip toglie righe vuote in coda e righe di commento
    git commit --cleanup=strip -F "$MESSAGGIO"
else
    echo "$MESSAGGIO non c'e' o e' vuoto: apro l'editor."
    git commit
fi

branch=$(git rev-parse --abbrev-ref HEAD)
git push origin "$branch"

if [ -f "$MESSAGGIO" ]; then
    mv "$MESSAGGIO" "$ARCHIVIO"
    echo "Messaggio archiviato in $ARCHIVIO"
fi
