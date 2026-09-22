#!/bin/sh
# =============================================================================
# tools/mkhd.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce dischi/hd.img: un disco AVVIABILE su cui EX-OS gira senza floppy.
#
#     tools/mkhd.sh [dimensione_MB]        (default: 512)
#
# PERCHE' ESISTE. Il floppy e' 1.44 MB e resta il supporto di avvio
# collaudato, ma e' troppo piccolo per gli strumenti: un compilatore da
# solo lo riempie piu' volte. Questo disco e' il posto dove EX-OS puo'
# crescere — 511 MB contro 897 KB liberi — e dove l'output di una
# compilazione ha dove andare, cosa che il CD degli strumenti (sola
# lettura per costruzione) non permette.
#
# PERCHE' LA FORMATTAZIONE LA FA EX-OS E NON LINUX. Partizionare da fuori
# e' deterministico e si fa in un comando; formattare no. Un ext2 fatto da
# `mke2fs` porta di serie estensioni che il driver di EX-OS rifiuta a
# ragione (vedi il rifiuto su `incompat` in kernel/fs/ext2.c), e ogni
# versione di e2fsprogs ne accende di nuove. Il formattatore di EX-OS
# produce esattamente cio' che EX-OS sa montare, e usarlo qui vuol dire
# che questo script prova la strada vera invece di una scorciatoia che
# funziona solo finche' nessuno aggiorna e2fsprogs.
#
# Lo stesso vale per l'installazione: `install` scrive l'MBR, il settore
# di avvio della partizione e la MAPPA DEI SETTORI del kernel — su ext2 il
# kernel non e' contiguo (il blocco di puntatori sta in mezzo ai dati) e
# la mappa ha piu' intervalli. Riprodurre quel calcolo da Linux
# significherebbe avere due implementazioni dello stesso formato, e la
# seconda sbaglierebbe in silenzio.
#
# CONSEGUENZA, ed e' il patto LILO gia' descritto in HANDOFF.md: la mappa
# vale finche' kernel e stage2 non si spostano. Ricompilare EX-OS e
# ricopiare i file sul disco senza rilanciare `install` produce un disco
# che non parte piu'. Per questo lo script rifa' tutto da zero ogni volta
# invece di aggiornare in loco: e' l'unica versione che non puo' mentire.
# =============================================================================

set -e

MB="${1:-512}"

# ! IL DISCO NON STA IN dist/, E NON E' UN DETTAGLIO DI ORDINE. dist/ e' cio'
# che si distribuisce — floppy e CD — e `make distclean` lo cancella tutto. Un
# disco rigido e' uno STATO: ci sta dentro un sistema installato con i file di
# chi lo usa, e rifarlo costa un giro completo in QEMU. Una pulizia lanciata
# per liberare spazio dalle ISO non deve portarselo via.
IMG=dischi/hd.img
FLOPPY=dist/floppy.img

mkdir -p "$(dirname "$IMG")"

# Il supporto da cui si installa si sceglie piu' in basso (EXOS_SUPPORTO), ma
# il controllo va fatto prima di scrivere qualunque cosa: si guarda qui.
if [ "${EXOS_SUPPORTO:-floppy}" != "cd" ] && [ ! -f "$FLOPPY" ]; then
    echo "mkhd: manca $FLOPPY. Lancia prima 'make'." >&2
    exit 1
fi

# sfdisk sta in /sbin, che non e' nel PATH di un utente normale.
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
if [ ! -x "$SFDISK" ]; then
    echo "mkhd: sfdisk non trovato (pacchetto util-linux)." >&2
    exit 1
fi

echo "=== Disco avviabile EX-OS: $IMG (${MB} MB, da $SUPPORTO) ==="

rm -f "$IMG"
qemu-img create -f raw "$IMG" "${MB}M" > /dev/null

# Una sola partizione primaria, tipo 83, attiva. Inizio a 2048 come ogni
# strumento moderno: allinea a 1 MB, cioe' a qualunque dimensione di
# blocco fisico un disco vero possa avere.
SETTORI=$(( MB * 1024 * 1024 / 512 - 2048 ))
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%s, type=83, bootable\n' \
    "$SETTORI" | "$SFDISK" "$IMG" > /dev/null 2>&1

echo "[OK] tabella delle partizioni: hd0p1, tipo 83, attiva"
echo ""
echo "--- Formattazione e installazione DENTRO EX-OS (qualche minuto) ---"

# Il floppy fa da supporto di servizio: ci si avvia, si formatta il disco
# e ci si installa sopra. Da qui in avanti il floppy non serve piu'.
#
# ! `install -t` E NON `install`, dal 17 agosto 2026. Senza il flag
# l'installatore MOSTRA i componenti opzionali trovati sul supporto e li chiede
# uno per uno: qui non c'e' nessuno che risponda, e la prova resterebbe ferma
# per 150 secondi su una domanda per poi dire «l'installazione non e' arrivata
# in fondo». -t vuol dire «tutto, non chiedere», che e' quello che serve a un
# disco di prova.
# ! I DUE CONTI LI CHIEDE L'INSTALLATORE, dal 19 agosto 2026, e da allora
# questo script si fermava li': le quattro righe qui sotto non rispondevano a
# «password di root:», la macchina restava ferma sulla domanda e la prova
# finiva con «l'installazione non e' arrivata in fondo» — un messaggio che
# accusa l'installatore mentre il difetto era qui.
#
# Le credenziali sono SCRITTE IN CHIARO ed e' voluto: questo e' un disco di
# prova, e una prova che si avvia deve sapere come entrare. Chi vuole un disco
# vero fa `install` a mano e le sceglie lui.
# =============================================================================
# ! DA QUALE SUPPORTO SI INSTALLA, e non e' una preferenza: cambia COSA c'e'
# sul disco che ne esce.
#
#     EXOS_SUPPORTO=floppy   (predefinito)  il sistema minimo, 1.44 MB
#     EXOS_SUPPORTO=cd                      tutto: rete, telnetd, netupdate,
#                                           ExWin, i driver, i manuali
#
# Il floppy resta il predefinito perche' e' il supporto d'avvio collaudato e
# perche' un disco minimo basta a provare quasi tutto. Ma un disco installato
# DAL FLOPPY non ha i driver di rete — e infatti `hwconfig` gli scrive in
# /boot/avvio.sh le righe per la scheda che ha trovato, righe che poi
# l'avvio non riesce a eseguire: «exec: comando non trovato: /dev/e1000.drv».
#
# ! CHI PROVA UNA COSA DELL'AVVIO VUOLE IL CD, quindi, o provera' un avvio in
# cui meta' delle righe non partono — cioe' un avvio che non esiste su nessuna
# macchina vera. Vale per @AVVIO-LOGIN, e vale per qualunque cosa riguardi
# l'ordine in cui il sistema si accende.
# =============================================================================
SUPPORTO="${EXOS_SUPPORTO:-floppy}"

UTENTE="${EXOS_UTENTE:-mario}"
PW_ROOT="${EXOS_PW_ROOT:-root}"
PW_UTENTE="${EXOS_PW_UTENTE:-mario}"

# ! E DAL 26 AGOSTO 2026 LA PRIMA DOMANDA E' LA LINGUA, non la password. E'
# successo di nuovo cio' che il commento qui sopra racconta: l'installatore ha
# imparato a chiedere una cosa in piu' e questo script ha continuato a
# rispondere alle domande di prima, sfasato di uno — «root» finiva nella
# scelta della lingua e da li' in avanti ogni risposta andava alla domanda
# sbagliata. Il messaggio finale accusava l'installatore.
#
# ! IL VERO RIMEDIO NON E' QUESTA RIGA: e' che una prova che pilota un
# programma interattivo si rompe a ogni domanda nuova, e non c'e' modo di
# accorgersene se non guardando il registro. Finche' `install` non prende le
# risposte da un file, questa riga va rivista ogni volta che l'installatore
# chiede qualcosa di nuovo.
#
# ! ED E' SUCCESSO UNA TERZA VOLTA, il 22 settembre 2026: l'installatore adesso
# chiude offrendo di lanciare `hwconfig` sul disco appena installato, e
# hwconfig a sua volta chiede «Procedo?». Mancavano DUE risposte, e questo
# script restava fermo sulla domanda finche' il timeout non lo portava via —
# con il disco gia' avviabile ma senza la riga «Installazione completata»,
# cioe' con un errore che accusava l'installatore.
#
# ! LE DUE RISPOSTE SONO «si» PER SCELTA. hwconfig riscrive /boot/kernel.cfg e
# /boot/avvio.sh con i driver della macchina VERA su cui il disco girera': un
# disco di prova che salta quel passo non e' il disco che si ottiene
# installando, ed e' proprio su quel file che si provano le cose dell'avvio.
LINGUA="${EXOS_LINGUA:-1}"

if [ "$SUPPORTO" = "cd" ]; then
    if [ ! -f dist/exos.iso ]; then
        echo "mkhd: manca dist/exos.iso. Lancia prima 'make iso-exos'." >&2
        exit 1
    fi
    export EXOS_NO_FLOPPY=1
    export EXOS_CDROM=dist/exos.iso
fi

EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    python3 tools/qemu_drive.py \
        "mkfs -t ext2 -L exos hd0p1@4" \
        "si@180" \
        "mount hd0p1 /disk@10" \
        "install -t /disk@10" \
        "$LINGUA@90" \
        "$PW_ROOT@2" "$PW_ROOT@3" \
        "$UTENTE@2" "$PW_UTENTE@2" "$PW_UTENTE@3" \
        "si@60" \
        "si@30" \
        "si@90" \
    > /tmp/exos-mkhd.log 2>&1

if ! grep -q "Installazione completata" /tmp/exos-mkhd.log; then
    echo "[ERRORE] l'installazione non e' arrivata in fondo." >&2
    echo "         registro completo: /tmp/exos-mkhd.log" >&2
    tail -25 /tmp/exos-mkhd.log >&2
    exit 1
fi

# La mappa dei settori del kernel: la riga che dice se il disco parte.
grep -E "kernel: .* intervall|stage2: LBA" /tmp/exos-mkhd.log | sed 's/^ */  /'

echo "[OK] $IMG e' avviabile."
echo ""
echo "Per entrare:  root / $PW_ROOT   oppure   $UTENTE / $PW_UTENTE"
echo ""
echo "Provalo senza floppy:"
echo "  make run-hd"
echo "  qemu-system-i386 -drive file=$IMG,format=raw,if=ide -m 32M -boot c"
