#!/bin/sh
# =============================================================================
# tools/mknetinstsrc.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce dist/netinst_src/: i SORGENTI del sistema di base, accanto ai due
# repository dei binari (dist/netinst e dist/netinst_64), nella forma che
# netupdate legge:
#
#     make netinst-src
#
#     dist/netinst_src/
#       versione.txt     versione, commit, data, l'impronta degli altri due
#       catalogo.txt     un pacchetto solo: [sourcecode]
#       elenco.txt       una riga: l'archivio, byte, impronta
#       file/src/exos-<versione>.tar.gz     (o .p000, .p001... sopra i 16 MB)
#
# Su EX-OS:  netupdate -install:sourcecode   ->  /src/exos-<versione>/
#
# ! UN ARCHIVIO SOLO, PER TUTT'E DUE LE ARCHITETTURE (deciso con l'utente il
# 21 ottobre 2026). I file di una sola architettura sono una quarantina, l'1%
# dei byte; la differenza vera sta dentro i file comuni (#if, ARCH= nel
# Makefile), e ognuna delle due si costruisce dall'albero intero. Tre archivi
# (32, 64, comune) avrebbero voluto dire scaricarne due e unirli, e una regola
# per dividere i file che un giorno sbaglia - exwin/icon/*_64.ico sono icone
# da 64 pixel, non file del 64 bit.
#
# ! E' QUELLO CHE VA SU GITHUB, NE' PIU' NE' MENO: git archive dei file
# tracciati. .gitignore tiene gia' fuori password (exagonx/), terze parti e
# compilati; un elenco di esclusioni scritto qui sarebbe il secondo, e prima
# o poi direbbe una cosa diversa dal primo.
#
# ! CON L'ALBERO SPORCO SI PUBBLICA L'ALBERO, NON LA HEAD. Si pubblica insieme
# ai binari, che sono compilati da cio' che c'e' sul disco: `git stash create`
# fa un commit dei file tracciati COSI' COME SONO, senza toccare ne' l'indice
# ne' il ramo, e l'archivio si fa da quello. versione.txt dice «modifiche = si»
# e lo script lo stampa: quei sorgenti saranno su GitHub al prossimo commit. I
# file nuovi non ancora aggiunti (git add) non ci sono, come non ci sarebbero
# su GitHub.
#
# ! L'ARCHIVIO E' RIPRODUCIBILE: git archive mette le date del commit e gzip
# -n, quindi gli stessi sorgenti danno gli stessi byte, e pubblica.sh lo
# ricarica solo quando i sorgenti sono cambiati davvero.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/.." && pwd)
cd "$RADICE"

# SRC_FUORI e SRC_PEZZO servono alla prova (tools/prova_sorgenti.sh), che
# pubblica anche un albero a pezzi piccoli.
FUORI="${SRC_FUORI:-dist/netinst_src}"
# ! LO STESSO TAGLIO DEI PEZZI DI EXILLA (mknetinst.sh): sul sito un file
# sopra i 16 MB non si carica intero. netupdate li ricuce da se'.
PEZZO="${SRC_PEZZO:-16777216}"

VERSIONE=$(sed -n 's/^#define EXOS_VERSION *"\(.*\)".*/\1/p' kernel/include/version.h)
DATA=$(date -u +%Y-%m-%dT%H:%M:%SZ)
[ -n "$VERSIONE" ] || {
    echo "mknetinstsrc: non riesco a leggere EXOS_VERSION da version.h." >&2
    exit 1
}
git rev-parse --git-dir >/dev/null 2>&1 || {
    echo "mknetinstsrc: qui non c'e' un repository git: i sorgenti da" >&2
    echo "              pubblicare sono i file che git traccia." >&2
    exit 1
}

TESTA=$(git rev-parse HEAD)
ALBERO=$(git stash create 2>/dev/null || true)
if [ -n "$ALBERO" ]; then MODIFICHE=si; else MODIFICHE=no; ALBERO=$TESTA; fi

NOME="exos-$VERSIONE"
PERC="src/$NOME.tar.gz"           # sul disco di EX-OS, come in elenco.txt

echo "=== $FUORI: i sorgenti di EX-OS $VERSIONE ==="

rm -rf "$FUORI"
mkdir -p "$FUORI/file/src"

git archive --format=tar.gz --prefix="$NOME/" -o "$FUORI/file/$PERC" "$ALBERO"

BYTE=$(stat -c %s "$FUORI/file/$PERC")
IMPR=$(sha256sum "$FUORI/file/$PERC" | cut -d' ' -f1)
N_FILE=$(git ls-tree -r --name-only "$ALBERO" | wc -l)
APERTO=$(gzip -dc "$FUORI/file/$PERC" | wc -c)

COLONNA=""
if [ "$BYTE" -gt "$PEZZO" ]; then
    split -b "$PEZZO" -d -a 3 "$FUORI/file/$PERC" "$FUORI/file/$PERC.p"
    rm -f "$FUORI/file/$PERC"
    COLONNA="	pezzi=$PEZZO"
fi

{
    echo "# percorso<TAB>byte<TAB>sha256<TAB>pacchetto[<TAB>pezzi=N]"
    printf '%s\t%s\t%s\tsourcecode%s\n' "$PERC" "$BYTE" "$IMPR" "$COLONNA"
} > "$FUORI/elenco.txt"

# ! IL CATALOGO HA UN PACCHETTO SOLO, E C'E' PER GLI SCRIPT DEL SERVER:
# pubblica.sh e verifica.sh trattano questo repository come gli altri due,
# e cercano i tre file di testa. netupdate legge solo versione.txt ed
# elenco.txt.
{
    echo "# catalogo.txt — i sorgenti di EX-OS (netupdate -install:sourcecode)"
    echo ""
    echo "[sourcecode]"
    echo "nome   = Sorgenti di EX-OS"
    echo "dice   = il sistema di base, 32 e 64 bit: quello che c'e' su GitHub"
    echo "prova  = src/$NOME/Makefile"
    echo "mbyte  = $(( (APERTO + 1048575) / 1048576 ))"
} > "$FUORI/catalogo.txt"

# ! arch = sorgenti: un netupdate vecchio, puntato qui per sbaglio con -set,
# si rifiuta da solo («un repository per un'altra macchina»).
{
    echo "# versione.txt — i sorgenti, per netupdate -install:sourcecode"
    echo "versione  = $VERSIONE"
    echo "arch      = sorgenti"
    echo "data      = $DATA"
    echo "commit    = $TESTA"
    echo "modifiche = $MODIFICHE"
    echo "archivio  = $PERC"
    echo "cartella  = src/$NOME"
    echo "file      = $N_FILE"
    echo "aperto    = $APERTO"
    echo "catalogo  = $(sha256sum "$FUORI/catalogo.txt" | cut -d' ' -f1)"
    echo "elenco    = $(sha256sum "$FUORI/elenco.txt" | cut -d' ' -f1)"
} > "$FUORI/versione.txt"

echo ""
grep -v '^#' "$FUORI/versione.txt" | sed 's/^/  /'
echo ""
echo "[OK] $FUORI pronto: $N_FILE file, $((BYTE / 1024)) KB compressi," \
     "$((APERTO / 1048576)) MB aperti${COLONNA:+, a pezzi da $((PEZZO / 1048576)) MB}"
if [ "$MODIFICHE" = si ]; then
    echo ""
    echo "  ! L'ALBERO HA MODIFICHE NON COMMESSE: l'archivio le contiene (la"
    echo "    HEAD $TESTA piu' i file tracciati cambiati)."
    echo "    Saranno su GitHub al prossimo commit; i file mai aggiunti a git"
    echo "    non ci sono."
fi
echo ""
echo "Su EX-OS:  netupdate -install:sourcecode   ->  /src/$NOME/"
