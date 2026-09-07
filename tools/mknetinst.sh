#!/bin/sh
# =============================================================================
# tools/mknetinst.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce dist/netinst/: il sistema intero, gia' compilato e funzionante,
# nella forma in cui va appoggiato su un server FTP o HTTP perche' `netupdate`
# lo possa leggere da dentro EX-OS.
#
#     make netinst
#
# COSA C'E' DENTRO
#
#     dist/netinst/
#       versione.txt     la prima cosa che netupdate legge: versione del
#                        sistema, data, e l'impronta degli altri due file
#       catalogo.txt     i pacchetti: nome, cosa contengono, da cosa dipendono
#       elenco.txt       un file per riga: percorso, byte, impronta, pacchetto
#       file/...         l'albero pubblicato, coi percorsi che avra' sul disco
#
# -----------------------------------------------------------------------------
# ! L'ALBERO NON SI RIFA' QUI, SI PRENDE DA build/iso-exos
#
# Quella directory e' gia' il sistema intero — la compone `make iso-exos`, che
# sa quali programmi, driver, librerie e font ci vanno. Rifare l'elenco qui
# dentro vorrebbe dire DUE elenchi che dicono la stessa cosa, e il giorno che
# un programma nuovo entra nel primo e non nel secondo nessuno se ne accorge:
# il CD ce l'ha, la rete no, e il difetto si vede sei mesi dopo come «quel
# comando sul disco installato dalla rete non c'e'». E' la stessa regola per
# cui il catalogo degli strumenti sta sul CD e non dentro toolinst.
#
# ! UN FILE PER FILE, E L'IMPRONTA E' PER FILE. Non e' una semplificazione: e'
# il punto. Se sul server sono cambiati solo `ls` e `fdisk`, netupdate deve
# proporre e scaricare SOLO QUEI DUE, e per saperlo gli serve un'impronta per
# ognuno. Un'impronta per pacchetto farebbe riscaricare l'intero sistema per
# due programmi, e su una linea lenta e' la differenza fra un aggiornamento che
# si fa e uno che si rimanda per sempre. Con un file per file, in piu', il
# server e' una directory e basta e lo serve qualunque cosa.
#
# ! PER LE APP CI VORRA' UN ARCHIVIO, ed e' un altro compito (@NET-TARGZ): una
# app e' una o piu' directory, e scaricarne i file uno per uno vuol dire un
# giro di rete per ognuno. La strada e' tar.gz, e costa poco perche' il pezzo
# difficile c'e' gia' — lib/eximg/inflate.c fa DEFLATE, lo usano PNG e GIF.
# Le due cose convivono: file per file per il sistema e per gli alberi grossi
# (inflate vuole tutto il buffer in memoria, e gli strumenti sono 150 MB),
# tar.gz per una app e le sue librerie.
#
# ! I PERCORSI SOTTO file/ SONO QUELLI CHE AVRANNO SUL DISCO. netupdate non
# deve tradurre niente: prende una riga di elenco.txt, ci mette davanti l'URL e
# «/file», e ha l'indirizzo da cui scaricare; ci mette davanti «/» e ha il
# posto dove scriverlo. Una tabella di conversione in mezzo sarebbe una terza
# cosa da tenere allineata alle altre due.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/.." && pwd)
cd "$RADICE"

ALBERO=build/iso-exos          # il sistema, composto da `make iso-exos`
STRUMENTI=build/iso            # il CD degli strumenti, composto da `make iso`
FUORI=dist/netinst

VERSIONE=$(sed -n 's/^#define EXOS_VERSION *"\(.*\)".*/\1/p' kernel/include/version.h)
DATA=$(date -u +%Y-%m-%dT%H:%M:%SZ)

[ -d "$ALBERO" ] || {
    echo "mknetinst: manca $ALBERO. Lancia prima 'make iso-exos'." >&2
    exit 1
}
[ -n "$VERSIONE" ] || {
    echo "mknetinst: non riesco a leggere EXOS_VERSION da version.h." >&2
    exit 1
}

echo "=== dist/netinst: il sistema $VERSIONE da pubblicare ==="

rm -rf "$FUORI"
mkdir -p "$FUORI/file"

# --- l'albero ---------------------------------------------------------------
#
# ! SI COPIA, NON SI COLLEGA. Un collegamento simbolico dentro dist/netinst
# funziona finche' la directory resta su questa macchina; appena la si carica
# su un server diventa un file di zero byte o un errore, e il sistema che lo
# scarica non parte. Si paga lo spazio una volta.
cp -a "$ALBERO"/. "$FUORI/file/"

# Gli strumenti, se ci sono. Vanno sotto /exos, che e' il percorso in cui
# `toolinst` li mette e da cui gcc calcola il proprio prefisso — vedi il
# commento in testa a bin/toolinst/toolinst.c: cambiarlo vuol dire un
# compilatore che non parte.
STRUM_CI_SONO=no
if [ -d "$STRUMENTI/exos" ] && [ -n "$(ls -A "$STRUMENTI/exos" 2>/dev/null)" ]; then
    mkdir -p "$FUORI/file/exos"
    cp -a "$STRUMENTI/exos"/. "$FUORI/file/exos/"
    STRUM_CI_SONO=si
fi

# --- il catalogo ------------------------------------------------------------
#
# ! IL FORMATO E' QUELLO DI tools/iso/strumenti.txt, e non e' pigrizia: quel
# file e' gia' un catalogo di pacchetti con le dipendenze (`vuole`), le prove
# di presenza (`prova`) e i pesi, ed e' gia' letto da toolinst. Due formati per
# la stessa cosa vuol dire due parser, e il secondo sbaglia dove il primo
# aveva gia' imparato.
CAT="$FUORI/catalogo.txt"
{
    echo "# ============================================================="
    echo "# catalogo.txt — i pacchetti pubblicati, per netupdate"
    echo "#"
    echo "# Stesse chiavi di tools/iso/strumenti.txt: nome, dice, prova,"
    echo "# vuole, mbyte, sempre. Le righe che non cominciano con una chiave"
    echo "# nota si ignorano, cosi' un catalogo scritto per un netupdate piu'"
    echo "# nuovo non rompe quello vecchio."
    echo "# ============================================================="
    echo ""
    echo "[base]"
    echo "nome   = Sistema di base"
    echo "dice   = kernel, avvio, shell, programmi, driver, librerie"
    echo "prova  = bin/sh"
    echo "sempre = si"
    echo "mbyte  = $(du -sm "$FUORI/file" | cut -f1)"
    echo ""
    if [ "$STRUM_CI_SONO" = si ] && [ -f tools/iso/strumenti.txt ]; then
        echo "# --- gli strumenti di sviluppo, dal catalogo del CD ---"
        echo "# Copiato da tools/iso/strumenti.txt: e' lo stesso elenco, e"
        echo "# tenerne uno solo e' il punto."
        echo ""
        sed -n '/^\[/,$p' tools/iso/strumenti.txt | sed 's|^solo *= *|solo   = exos/|'
    fi
} > "$CAT"

# --- l'elenco dei file ------------------------------------------------------
#
# ! L'IMPRONTA C'E' PER OGNI FILE, e serve a una cosa sola ma indispensabile:
# distinguere uno scaricamento riuscito da uno interrotto a meta'. Senza, un
# file troncato ha la data giusta e la dimensione sbagliata, e la volta dopo
# non viene riscaricato perche' «c'e' gia'».
ELE="$FUORI/elenco.txt"
{
    echo "# percorso<TAB>byte<TAB>sha256<TAB>pacchetto"
    ( cd "$FUORI/file" && find . -type f -printf '%P\n' | LC_ALL=C sort ) |
    while IFS= read -r p; do
        b=$(stat -c %s "$FUORI/file/$p")
        h=$(sha256sum "$FUORI/file/$p" | cut -d' ' -f1)
        case "$p" in
            exos/*) pac=strumenti ;;
            *)      pac=base ;;
        esac
        printf '%s\t%s\t%s\t%s\n' "$p" "$b" "$h" "$pac"
    done
} > "$ELE"

N_FILE=$(grep -vc '^#' "$ELE" || true)

# --- il file di versione ----------------------------------------------------
#
# ! E' IL PRIMO E IL PIU' PICCOLO, ed e' voluto: netupdate lo scarica a ogni
# controllo, anche quando non c'e' niente da fare. Ci sta dentro l'impronta
# degli altri due, cosi' un controllo che non trova niente di nuovo costa UN
# giro di rete e non trecento.
{
    echo "# versione.txt — la prima cosa che netupdate legge"
    echo "versione  = $VERSIONE"
    echo "data      = $DATA"
    echo "file      = $N_FILE"
    echo "byte      = $(du -sb "$FUORI/file" | cut -f1)"
    echo "catalogo  = $(sha256sum "$CAT" | cut -d' ' -f1)"
    echo "elenco    = $(sha256sum "$ELE" | cut -d' ' -f1)"
} > "$FUORI/versione.txt"

echo ""
cat "$FUORI/versione.txt" | grep -v '^#' | sed 's/^/  /'
echo ""
echo "[OK] $FUORI pronto: $N_FILE file, $(du -sh "$FUORI" | cut -f1)"
[ "$STRUM_CI_SONO" = si ] || echo "     ! senza gli strumenti: build/iso/exos e' vuoto (serve 'make iso')"
echo ""
echo "Da pubblicare cosi' com'e': la radice del server e' $FUORI,"
echo "e netupdate cerchera' versione.txt, catalogo.txt, elenco.txt e file/."
