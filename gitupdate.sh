#!/bin/bash
#
# Aggiunge tutto, committa e spinge sul ramo corrente.
#
#     ./gitupdate.sh            committa e spinge il ramo su cui si e'
#     ./gitupdate.sh -ramo      dice su che ramo si e', e quali esistono
#     ./gitupdate.sh -porta     porta dentro il ramo corrente le novita' di main
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
#
# ! I RAMI (10 ottobre 2026). Il lavoro per i 64 bit puo' stare in un ramo
# suo. Questo script ha sempre spinto «il ramo corrente»: adesso lo DICE prima
# di committare, perche' un commit finito nel ramo sbagliato si scopre giorni
# dopo. Con -porta le correzioni fatte in main arrivano nel ramo: sono gli
# stessi file, e senza portarle i due rami si allontanano a ogni commit.
# Passare da un ramo all'altro resta un comando di git, dato a mano e con
# l'albero pulito:   git switch main    /    git switch <ramo>

set -euo pipefail

cd "$(dirname "$0")"

MESSAGGIO="messaggio-commit.txt"
ARCHIVIO=".git/ultimo-messaggio-commit.txt"

branch=$(git rev-parse --abbrev-ref HEAD)

case "${1:-}" in
    "") ;;
    -ramo)
        echo "Ramo corrente: $branch"
        echo "Rami che esistono:"
        git branch -a | sed 's/^/  /'
        exit 0 ;;
    -porta)
        if [ "$branch" = "main" ]; then
            echo "Sei su main: non c'e' niente da portare. -porta si usa da un altro ramo."
            exit 1
        fi
        if ! git diff --quiet || ! git diff --cached --quiet; then
            echo "Ci sono modifiche non committate: prima ./gitupdate.sh, poi -porta."
            exit 1
        fi
        echo "Porto in '$branch' le novita' di main."
        git fetch origin main
        # ! SE DUE MODIFICHE TOCCANO LE STESSE RIGHE git si ferma e lo dice:
        # si aprono i file segnati, si sceglie, e si finisce con ./gitupdate.sh.
        # Non si risolve da soli: scegliere a caso fra due versioni di un
        # sorgente e' peggio che fermarsi.
        if ! git merge --no-edit origin/main; then
            echo
            echo "! Non sono riuscito a unire tutto da solo. I file da sistemare:"
            git diff --name-only --diff-filter=U | sed 's/^/    /'
            echo "  Sistemati quelli:  ./gitupdate.sh"
            exit 1
        fi
        git push -u origin "$branch"
        echo "Fatto: '$branch' ha anche le novita' di main."
        exit 0 ;;
    *)
        echo "uso: $0 [-ramo | -porta]" >&2
        exit 2 ;;
esac

git add .

if git diff --cached --quiet; then
    echo "Niente da committare: l'area di stage e' vuota (ramo: $branch)."
    exit 0
fi

echo "============================================="
echo " Ramo: $branch"
echo "============================================="

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

# -u: la prima volta che un ramo nuovo viene spinto, lo lega a quello sul
# server; le volte dopo non cambia niente.
git push -u origin "$branch"

if [ -f "$MESSAGGIO" ]; then
    mv "$MESSAGGIO" "$ARCHIVIO"
    echo "Messaggio archiviato in $ARCHIVIO"
fi
