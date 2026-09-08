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

# --- kernel e secondo stadio: i due file che cambiano nome ------------------
#
# ! SUL CD STANNO ALLA RADICE, SU UN DISCO INSTALLATO STANNO IN /boot E CON UN
# ALTRO NOME. `install` copia /KERNEL.BIN in /boot/kernel.bin e /LOADER.BIN in
# /boot/stage2.bin (bin/install/install.c), perche' sul CD il caricatore li
# cerca dove El Torito li mette e su ext2 no. Pubblicandoli com'erano, l'elenco
# prometteva due percorsi CHE SU NESSUNA MACCHINA INSTALLATA ESISTONO: il
# registro li segnava «non ci sono» e un aggiornamento li avrebbe scritti alla
# radice, dove non li guarda nessuno.
#
# Qui si pubblicano col nome che avranno: la riga in testa a questo file — «i
# percorsi sotto file/ sono quelli che avranno sul disco» — o e' vera per
# tutti o non serve a niente.
AVVIO="boot/kernel.bin boot/stage2.bin"
mkdir -p "$FUORI/file/boot"
[ -f "$FUORI/file/KERNEL.BIN" ] && mv "$FUORI/file/KERNEL.BIN" "$FUORI/file/boot/kernel.bin"
[ -f "$FUORI/file/LOADER.BIN" ] && mv "$FUORI/file/LOADER.BIN" "$FUORI/file/boot/stage2.bin"

# ! CHI E' DEL SISTEMA SI SEGNA ADESSO, PRIMA CHE GLI STRUMENTI SI STENDANO
# SOPRA. Dividere i due alberi guardando il percorso — «sotto exos/ e' degli
# strumenti» — sembra ovvio e SBAGLIA: il sistema ha un exos/ suo, ci tiene
# per esempio exos/ssl/certi.pem, i 150 certificati per https. Con la regola
# del percorso quel file finiva nel pacchetto del compilatore C, e chi avesse
# tolto il C si sarebbe ritrovato senza https senza capire perche'. Qui
# l'origine si sa per certo, e sapere costa una `find`.
TEMP=$(mktemp -d)
trap 'rm -rf "$TEMP"' EXIT INT TERM
( cd "$FUORI/file" && find . -type f -printf '%P\n' | LC_ALL=C sort ) > "$TEMP/sistema"

# Gli strumenti, se ci sono. Vanno sotto /exos, che e' il percorso in cui
# `toolinst` li mette e da cui gcc calcola il proprio prefisso — vedi il
# commento in testa a bin/toolinst/toolinst.c: cambiarlo vuol dire un
# compilatore che non parte.
# ! LA PROVA E' gcc, NON «la directory esiste». Prima qui c'era «exos/ non e'
# vuota», e non bastava: build/iso/exos contiene header, stub e libc.so anche
# quando i compilatori non ci sono, quindi il controllo diceva di si' e si
# pubblicavano 74 file di strumenti SENZA UN SOLO COMPILATORE. Chi lo scarica
# non se ne accorge finche' non prova a compilare. E' la stessa idea della
# chiave `prova` del catalogo: un percorso che, se c'e', dice che il gruppo
# c'e' DAVVERO.
STRUM_CI_SONO=no
if [ -x "$STRUMENTI/exos/bin/gcc" ]; then
    mkdir -p "$FUORI/file/exos"
    cp -a "$STRUMENTI/exos"/. "$FUORI/file/exos/"
    STRUM_CI_SONO=si
fi

# --- chi possiede cosa -------------------------------------------------------
#
# ! IL PACCHETTO DI UN FILE SI DEDUCE DAL CATALOGO, NON SI SCRIVE DUE VOLTE.
# Prima qui c'era «tutto quel che sta sotto exos/ e' del pacchetto strumenti»,
# e strumenti NON ESISTEVA nel catalogo: elenco.txt e catalogo.txt dicevano due
# cose diverse sulla stessa directory, e chi legge il primo non trova nel
# secondo il pacchetto che gli viene nominato. Adesso l'assegnazione segue le
# stesse chiavi che legge toolinst — `solo` (un percorso che appartiene SOLO a
# quel gruppo) e `file` (un NOME di file, ovunque stia) — con la stessa regola
# di chiusura: quel che nessuno rivendica e' della base.
#
# ! E LA BASE E' DIVERSA NEI DUE ALBERI: sotto exos/ e' [base] degli strumenti
# (il C: gcc, as, ld), fuori e' [sistema]. Sono due cataloghi cuciti in uno.
REGOLE=$(awk '
    /^\[/                    { id = $0; gsub(/[][]/, "", id); next }
    /^[ \t]*solo[ \t]*=/     { sub(/^[^=]*=[ \t]*/, ""); print "solo " id ":" $0 }
    /^[ \t]*file[ \t]*=/     { sub(/^[^=]*=[ \t]*/, ""); print "file " id ":" $0 }
' tools/iso/strumenti.txt 2>/dev/null || true)
printf '%s\n' "$REGOLE" > "$TEMP/regole"

ELE="$FUORI/elenco.txt"

( cd "$FUORI/file" && find . -type f -printf '%P\n' | LC_ALL=C sort ) > "$TEMP/tutti"

# La mappa percorso -> pacchetto, in un passo solo. In fondo c'e' la regola di
# chiusura del catalogo degli strumenti: quel che nessun gruppo rivendica e'
# della base, che li' dentro e' il C.
awk -v R="$TEMP/regole" -v S="$TEMP/sistema" -v AVVIO="$AVVIO" '
    FILENAME == R {
        tipo = $1
        sub(/^[^ ]+ +/, "")
        i = index($0, ":")
        if (i == 0) next
        id = substr($0, 1, i - 1)
        val = substr($0, i + 1)
        if (tipo == "solo") { n_solo++; solo_id[n_solo] = id; solo_p[n_solo] = val }
        if (tipo == "file") { n_nome++; nome_id[n_nome] = id; nome_n[n_nome] = val }
        next
    }
    FILENAME == S { sis[$0] = 1; next }
    BEGIN { n = split(AVVIO, a, " "); for (i = 1; i <= n; i++) avvio[a[i]] = 1 }
    {
        p = $0
        if (p in avvio) { print p "\tavvio"; next }
        if (p in sis)   { print p "\tsistema"; next }

        rel = p;  sub(/^exos\//, "", rel)
        base = rel;  sub(/.*\//, "", base)

        for (i = 1; i <= n_solo; i++)
            if (rel == solo_p[i] || index(rel, solo_p[i] "/") == 1) {
                print p "\t" solo_id[i]; next
            }
        for (i = 1; i <= n_nome; i++)
            if (base == nome_n[i]) { print p "\t" nome_id[i]; next }

        print p "\tbase"
    }
' "$TEMP/regole" "$TEMP/sistema" "$TEMP/tutti" > "$TEMP/mappa"

# --- gli archivi: un giro di rete invece di trenta ---------------------------
#
# ! UN'APPLICAZIONE E' UNA DIRECTORY, NON UN FILE. Scaricarne i pezzi uno per
# uno vuol dire un giro di rete per ognuno: su una linea lenta e' la differenza
# fra installare e rinunciare. Per i pacchetti che ci stanno si pubblica anche
# un tar.gz, e chi installa fa UN giro.
#
# ! MA NON PER TUTTI, E LA RIGA DI TAGLIO E' LA MEMORIA. inflate() vuole il
# buffer d'uscita INTERO dal chiamante (lib/eximg/inflate.h: su EX-OS free()
# non restituisce niente, quindi allocare dentro un ciclo e' una perdita
# permanente), e netupdate tiene l'archivio compresso in un buffer da due
# megabyte. Quindi: archivio per una app, file per file per il sistema di base
# e per gli alberi grossi — gli strumenti sono 150 MB e non ci staranno mai.
# Le due strade convivono, e non e' un ripiego: sono due casi diversi.
#
# ! E IL SISTEMA NON HA ARCHIVIO NEMMENO SE CI STESSE. [sistema] e [avvio] si
# aggiornano FILE PER FILE, perche' `-check` deve poter scaricare i due che
# sono cambiati e non i centosettanta che ci sono.
ARC_TETTO=1572864          # 1,5 MB: sta comodo nel buffer di netupdate
mkdir -p "$FUORI/pacchetti"
: > "$TEMP/archivi"

for id in $(cut -f2 "$TEMP/mappa" | LC_ALL=C sort -u); do
    case "$id" in sistema|avvio) continue ;; esac

    awk -F'\t' -v p="$id" '$2 == p { print $1 }' "$TEMP/mappa" > "$TEMP/lista"
    [ -s "$TEMP/lista" ] || continue

    # ! FORMATO ustar E NIENTE ESTENSIONI GNU: il lettore dall'altra parte e'
    # un'intestazione da 512 byte letta in ottale, e deve restare tale. Se un
    # percorso non ci sta, tar lo dice e l'archivio non si pubblica — meglio
    # un pacchetto senza archivio che un archivio che il lettore non sa aprire.
    if ! tar --format=ustar -czf "$FUORI/pacchetti/$id.tar.gz" \
             -C "$FUORI/file" -T "$TEMP/lista" 2>/dev/null; then
        rm -f "$FUORI/pacchetti/$id.tar.gz"
        echo "     ! $id: tar non riesce a fare l'archivio (percorsi troppo lunghi?)"
        continue
    fi

    arcb=$(stat -c %s "$FUORI/pacchetti/$id.tar.gz")
    if [ "$arcb" -gt "$ARC_TETTO" ]; then
        rm -f "$FUORI/pacchetti/$id.tar.gz"
        continue                    # troppo grosso: resta il file per file
    fi
    srot=$(while IFS= read -r f; do stat -c %s "$FUORI/file/$f"; done < "$TEMP/lista" \
           | awk '{ s += $1 } END { print s + 0 }')
    printf '%s\t%s\t%s\t%s\n' "$id" "$arcb" \
        "$(sha256sum "$FUORI/pacchetti/$id.tar.gz" | cut -d' ' -f1)" "$srot" \
        >> "$TEMP/archivi"
done

# --- il catalogo ------------------------------------------------------------
#
# ! IL FORMATO E' QUELLO DI tools/iso/strumenti.txt, e non e' pigrizia: quel
# file e' gia' un catalogo di pacchetti con le dipendenze (`vuole`), le prove
# di presenza (`prova`) e i pesi, ed e' gia' letto da toolinst. Due formati per
# la stessa cosa vuol dire due parser, e il secondo sbaglia dove il primo
# aveva gia' imparato.
#
# ! IL SISTEMA SI CHIAMA [sistema] E NON [base], e la ragione e' che [base]
# ESISTE GIA': e' il gruppo C del CD degli strumenti. Due blocchi con la stessa
# etichetta nello stesso catalogo vuol dire che il secondo cancella il primo,
# e nessuno se ne accorge finche' `-install:base` non installa la cosa
# sbagliata. Gli id dei gruppi degli strumenti restano quelli del CD: due nomi
# per la stessa cosa e' il modo di far divergere due elenchi.
CAT="$FUORI/catalogo.txt"
{
    echo "# ============================================================="
    echo "# catalogo.txt — i pacchetti pubblicati, per netupdate"
    echo "#"
    echo "# Stesse chiavi di tools/iso/strumenti.txt: nome, dice, prova,"
    echo "# vuole, mbyte, sempre. Le righe che non cominciano con una chiave"
    echo "# nota si ignorano, cosi' un catalogo scritto per un netupdate piu'"
    echo "# nuovo non rompe quello vecchio."
    echo "#"
    echo "# I percorsi di prova e solo sono quelli che i file avranno SUL"
    echo "# DISCO, senza la barra davanti: la stessa forma di elenco.txt."
    echo "#"
    echo "# Un pacchetto che ha anche un ARCHIVIO porta quattro chiavi in piu':"
    echo "#   archivio      dove sta il tar.gz, sotto la radice del server"
    echo "#   arcbyte       quanto pesa compresso"
    echo "#   arcimpronta   la sua sha256"
    echo "#   arcsrotolato  quanto occupa aperto: chi lo apre deve sapere"
    echo "#                 PRIMA se ci sta in memoria"
    echo "# Chi non ce le ha si scarica file per file, ed e' il caso del"
    echo "# sistema di base: -check deve poter prendere i due file cambiati e"
    echo "# non i centosettanta che ci sono."
    echo "# ============================================================="
    echo ""
    echo "[avvio]"
    echo "nome   = Avvio"
    echo "dice   = il kernel e il secondo stadio del caricatore"
    echo "prova  = boot/kernel.bin"
    echo "sempre = si"
    echo "mbyte  = 1"
    echo "nota   = ! QUESTI DUE NON SONO FILE COME GLI ALTRI. Su ext2 il"
    echo "nota   = kernel non e' contiguo, e il settore di avvio contiene la"
    echo "nota   = MAPPA DEI SETTORI del file: copiarci sopra senza rifare la"
    echo "nota   = mappa da' un disco che non parte piu'. Si scarica accanto,"
    echo "nota   = si verifica, si rifa' la mappa, e il settore di avvio si"
    echo "nota   = tocca per ULTIMO."
    echo ""
    echo "[sistema]"
    echo "nome   = Sistema di base"
    echo "dice   = shell, programmi, driver, librerie, configurazione"
    echo "vuole  = avvio"
    echo "prova  = bin/sh"
    echo "sempre = si"
    echo "mbyte  = $(du -sm "$FUORI/file" | cut -f1)"
    echo ""
    if [ "$STRUM_CI_SONO" = si ] && [ -f tools/iso/strumenti.txt ]; then
        echo "# --- gli strumenti di sviluppo, dal catalogo del CD ---"
        echo "# Copiato da tools/iso/strumenti.txt: e' lo stesso elenco, e"
        echo "# tenerne uno solo e' il punto. Cambiano tre cose e solo quelle:"
        echo "# i percorsi di prova/solo prendono davanti exos/, che e' dove"
        echo "# l'albero vive sul disco, e la riga «sempre» se ne va — li'"
        echo "# voleva dire «se prendi gli strumenti prendi anche il C», qui"
        echo "# vorrebbe dire «non si puo' togliere», che e' un'altra cosa e"
        echo "# non e' vera: a tenere in piedi le dipendenze ci pensa vuole."
        echo ""
        sed -n '/^\[/,$p' tools/iso/strumenti.txt \
            | sed -e 's|^solo *= *|solo   = exos/|' \
                  -e 's|^prova *= *|prova  = exos/|' \
                  -e '/^sempre *= */d' \
            | awk -v A="$TEMP/archivi" '
                BEGIN {
                    while ((getline riga < A) > 0) {
                        split(riga, c, "\t")
                        arc[c[1]] = c[2] "\t" c[3] "\t" c[4]
                    }
                }
                /^\[/ {
                    print
                    id = $0; gsub(/[][]/, "", id)
                    if (id in arc) {
                        split(arc[id], c, "\t")
                        print "archivio = pacchetti/" id ".tar.gz"
                        print "arcbyte  = " c[1]
                        print "arcimpronta = " c[2]
                        print "arcsrotolato = " c[3]
                    }
                    next
                }
                { print }
            '
    fi
} > "$CAT"

# --- l'elenco dei file ------------------------------------------------------
#
# ! L'IMPRONTA C'E' PER OGNI FILE, e serve a una cosa sola ma indispensabile:
# distinguere uno scaricamento riuscito da uno interrotto a meta'. Senza, un
# file troncato ha la data giusta e la dimensione sbagliata, e la volta dopo
# non viene riscaricato perche' «c'e' gia'».
{
    echo "# percorso<TAB>byte<TAB>sha256<TAB>pacchetto"
    while IFS='	' read -r p pac; do
        b=$(stat -c %s "$FUORI/file/$p")
        h=$(sha256sum "$FUORI/file/$p" | cut -d' ' -f1)
        printf '%s\t%s\t%s\t%s\n' "$p" "$b" "$h" "$pac"
    done < "$TEMP/mappa"
} > "$ELE"

# Se nessun pacchetto ha meritato un archivio, la directory non ci va: una
# directory vuota su un server e' una domanda a cui nessuno sa rispondere.
rmdir "$FUORI/pacchetti" 2>/dev/null || true

N_FILE=$(grep -vc '^#' "$ELE" || true)

# ! IL CONTO PER PACCHETTO SI STAMPA, e non e' decorazione: un pacchetto del
# catalogo che non possiede NESSUN file e' un errore che si vede solo cosi'
# — vuol dire che i suoi `solo` non combaciano piu' con l'albero, e chi lo
# installasse scaricherebbe zero file credendo di averlo installato.
CONTI=$(grep -v '^#' "$ELE" | cut -f4 | LC_ALL=C sort | uniq -c | sort -rn)

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
echo "  i file, per pacchetto:"
printf '%s\n' "$CONTI" | sed 's/^/    /'

# I pacchetti dichiarati nel catalogo che non possiedono nessun file. Vedi il
# commento sopra CONTI: si vedono solo contando.
VUOTI=""
for id in $(grep '^\[' "$CAT" | tr -d '[]'); do
    printf '%s\n' "$CONTI" | awk -v i="$id" '$2 == i { trovato = 1 }
                                              END { exit !trovato }' || VUOTI="$VUOTI $id"
done
if [ -n "$VUOTI" ]; then
    echo ""
    echo "  ! PACCHETTI SENZA NESSUN FILE:$VUOTI"
    echo "    Sono nel catalogo e non possiedono niente: i loro 'solo' non"
    echo "    combaciano con l'albero. Chi li installasse scaricherebbe zero"
    echo "    file credendo di aver installato qualcosa."
fi
echo ""
echo "[OK] $FUORI pronto: $N_FILE file, $(du -sh "$FUORI" | cut -f1)"
if [ -s "$TEMP/archivi" ]; then
    echo ""
    echo "  archivi (un giro di rete invece di tanti):"
    while IFS='	' read -r id arcb arch srot; do
        echo "    $(printf '%-12s' "$id") $((arcb / 1024)) KB compresso, $((srot / 1024)) KB aperto"
    done < "$TEMP/archivi"
fi
if [ "$STRUM_CI_SONO" != si ]; then
    echo "     ! SENZA GLI STRUMENTI: $STRUMENTI/exos/bin/gcc non c'e'."
    echo "       Il sistema di base e' pubblicato e si aggiorna; per i"
    echo "       compilatori serve prima 'make iso'."
fi
echo ""
echo "Da pubblicare cosi' com'e': la radice del server e' $FUORI,"
echo "e netupdate cerchera' versione.txt, catalogo.txt, elenco.txt e file/."
