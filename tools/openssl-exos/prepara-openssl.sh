#!/bin/bash
# =============================================================================
# tools/openssl-exos/prepara-openssl.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Configura OpenSSL per EX-OS. I sorgenti NON stanno nel repository: si
# mettono in ./openssl (che e' in .gitignore), come per GCC e binutils.
#
#     ./tools/openssl-exos/prepara-openssl.sh
#     cd ~/openssl-build-exos && make -j2 build_libs
#
# -----------------------------------------------------------------------------
# ⚠️ SI CONFIGURA CON `no-` DAPPERTUTTO, E OGNUNO HA UNA RAGIONE
#
# La tentazione e' partire dalla configurazione completa e togliere quello
# che non compila. E' l'ordine sbagliato: OpenSSL completo tocca socket,
# thread, filesystem, orologio, variabili d'ambiente e caricamento
# dinamico, e togliere a posteriori vuol dire inseguire un errore per
# volta senza sapere quanti ne restano.
#
# Si parte invece dal minimo che serve — la crittografia — e si aggiunge
# solo cio' che si e' scelto di portare.
#
#   no-sock          EX-OS non ha i socket BSD: ha uno stack IP che si
#                    parla via IPC. TLS non ha bisogno di socket: ha
#                    bisogno di un BIO, e un BIO lo si scrive sopra
#                    qualunque cosa sappia leggere e scrivere byte.
#   no-dgram         idem per UDP: DTLS non serve.
#   no-stdio         niente FILE* dentro la libreria. La nostra stdio c'e'
#                    e funziona, ma qui porta dentro tutta la gestione dei
#                    file di configurazione, delle chiavi su disco e dei
#                    percorsi — roba che una libreria per TLS non deve
#                    avere per forza.
#   no-posix-io      conseguenza della precedente.
#   no-threads       EX-OS non ha i thread. Vedi 50-exos.conf.
#   no-dso           niente caricamento dinamico: qui i binari sono
#                    statici per costruzione.
#   no-shared        idem.
#   no-ui-console    la richiesta interattiva della passphrase vuole un
#                    terminale con l'eco spento. Si potra' aggiungere; non
#                    e' crittografia.
#   no-engine        gli ENGINE sono il vecchio meccanismo dei provider.
#   no-tests         i test di OpenSSL sono programmi a se', e prima
#                    bisogna avere la libreria.
#   no-asm           vedi 50-exos.conf: l'assembly x86 presuppone SSE2.
#   no-autoload-config  altrimenti la libreria cerca openssl.cnf all'avvio.
#
# -----------------------------------------------------------------------------
# ⚠️ --with-rand-seed=getrandom, E FUNZIONA SENZA PATCH
#
# OpenSSL cerca un `getentropy()` come simbolo DEBOLE prima di provare
# qualunque altra cosa (providers/implementations/rands/seeding/rand_unix.c).
# La libc di EX-OS lo fornisce con la firma di OpenBSD, e questo basta:
# nessuna riga di OpenSSL da toccare.
#
# Dietro c'e' kernel/arch/x86/entropia.c, che raccoglie entropia dagli
# istanti di arrivo degli interrupt e da RDRAND dove c'e', e che RIFIUTA
# di dare byte quando non ne ha abbastanza invece di inventarli.
# =============================================================================
set -e

SORGENTI="${SORGENTI:-$PWD/openssl}"
BUILD="${BUILD:-$HOME/openssl-build-exos}"
PREFISSO="${PREFISSO:-$HOME/exos-cross}"

if [ ! -f "$SORGENTI/Configure" ]; then
    echo "Sorgenti OpenSSL non trovati in $SORGENTI" >&2
    echo "Mettili li' (la directory e' in .gitignore) oppure:" >&2
    echo "    SORGENTI=/altro/percorso $0" >&2
    exit 1
fi

if ! command -v i386-exos-gcc >/dev/null 2>&1; then
    echo "i386-exos-gcc non e' nel PATH." >&2
    echo "    export PATH=\"\$HOME/exos-cross/bin:\$PATH\"" >&2
    exit 1
fi

echo "=== bersaglio exos-x86 in Configurations/ ==="
cp "$(dirname "$0")/50-exos.conf" "$SORGENTI/Configurations/50-exos.conf"

mkdir -p "$BUILD"
# ⚠️ configdata.pm scrive apps/include/configuration.h nell'albero di
# BUILD e non crea la directory: fallisce con un messaggio che nomina il
# file SBAGLIATO — dice "configuration.h.in.new" (il template, che
# esiste) mentre quello che non riesce ad aprire e' l'output. Un'ora
# persa a cercare un problema di permessi sul sorgente.
mkdir -p "$BUILD/apps/include"
cd "$BUILD"

echo "=== Configure ==="
"$SORGENTI/Configure" exos-x86 \
    --prefix=/exos --openssldir=/exos/ssl \
    --with-rand-seed=getrandom \
    no-sock no-dgram no-stdio no-posix-io no-threads \
    no-dso no-shared no-engine no-tests no-asm \
    no-ui-console no-autoload-config \
    no-legacy no-deprecated no-comp no-zlib \
    no-quic no-ktls no-module

echo
echo "[OK] configurato in $BUILD"
echo
echo "Per costruire le sole librerie (gli 'apps' vogliono stdio e socket):"
echo
echo "    cd $BUILD && make -j2 build_libs"
echo
