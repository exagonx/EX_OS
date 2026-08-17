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

if [ ! -f "$FLOPPY" ]; then
    echo "mkhd: manca $FLOPPY. Lancia prima 'make'." >&2
    exit 1
fi

# sfdisk sta in /sbin, che non e' nel PATH di un utente normale.
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
if [ ! -x "$SFDISK" ]; then
    echo "mkhd: sfdisk non trovato (pacchetto util-linux)." >&2
    exit 1
fi

echo "=== Disco avviabile EX-OS: $IMG (${MB} MB) ==="

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
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    python3 tools/qemu_drive.py \
        "mkfs -t ext2 -L exos hd0p1@4" \
        "si@180" \
        "mount hd0p1 /disk@10" \
        "install -t /disk@150" \
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
echo "Provalo senza floppy:"
echo "  make run-hd"
echo "  qemu-system-i386 -drive file=$IMG,format=raw,if=ide -m 32M -boot c"
