#!/bin/bash
#
# Riprende la sessione di Claude Code del 25 settembre 2026 (tar/gzip,
# repo-update, git e MEGA), con tutto il suo contesto.
#
#   ./continua.sh            riprende QUELLA sessione
#   ./continua.sh -ultima    riprende la piu' recente, qualunque sia
#
# ! LA SESSIONE E' LEGATA ALLA CARTELLA DA CUI E' PARTITA, che e' dev/: Claude
# Code cerca le conversazioni per directory, e da un'altra non la trova. Per
# questo lo script si sposta li' prima di chiamarlo.
#
# ! E VIVE SU QUESTO PC, in ~/.claude/projects/, non nella cartella del
# progetto: MEGA non la porta sull'altra macchina. La' lo script dice che
# non c'e' e si ferma.

SESSIONE="dfb8ad94-3489-4264-aa5f-0dd8e26ed2fc"

cd "$(dirname "$0")/dev" || { echo "continua: manca la cartella dev/" >&2; exit 1; }

if ! command -v claude > /dev/null; then
    echo "continua: il comando 'claude' non e' installato su questo PC." >&2
    exit 1
fi

if [ "${1:-}" = "-ultima" ]; then
    exec claude --continue
fi

# La cartella in ~/.claude/projects/ e' il percorso di dev/ con ogni carattere
# che non sia lettera o cifra cambiato in '-'.
PROGETTO="$HOME/.claude/projects/$(pwd | sed 's/[^A-Za-z0-9]/-/g')"
if [ ! -f "$PROGETTO/$SESSIONE.jsonl" ]; then
    echo "continua: la sessione $SESSIONE non e' su questo PC." >&2
    echo "          (vive in ~/.claude/projects/ della macchina dove e' nata)" >&2
    echo "          Per la piu' recente di questa cartella: $0 -ultima" >&2
    exit 1
fi

exec claude --resume "$SESSIONE"
