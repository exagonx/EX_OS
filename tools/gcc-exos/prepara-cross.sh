#!/bin/sh
# =============================================================================
# tools/gcc-exos/prepara-cross.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Prepara il contorno del cross-compilatore i386-exos: i binutils e
# l'ambiente del bersaglio. Va lanciato dalla radice del progetto.
#
#     tools/gcc-exos/prepara-cross.sh [prefisso]      (default: ~/exos-cross)
#
# COSA SONO I WRAPPER DEI BINUTILS. Sono il RIPIEGO, da quando esiste
# tools/binutils-exos/prepara-binutils.sh: se i binutils veri per
# i386-exos sono gia' installati, questo script li lascia stare e li usa.
#
# Il ripiego funziona perche' il formato di uscita e' ELF32 i386,
# esattamente quello che l'`as` e l'`ld` di sistema producono con --32 e
# -m elf_i386; i wrapper esistono perche' senza di loro quegli strumenti
# lavorerebbero a 64 bit, che e' il loro default. Basta per compilare SU
# Linux PER EX-OS, e non basta per il passo dopo — un binutils NATIVO si
# costruisce solo a partire da uno cross.
#
# ⚠️ VANNO INSTALLATI IN DUE POSTI, e non e' una ridondanza:
#
#   $prefisso/bin/i386-exos-as     il nome che digita l'utente
#   $prefisso/i386-exos/bin/as     il nome che cerca GCC
#
# Il driver di GCC NON cerca "i386-exos-as" nel PATH: cerca il nome
# SEMPLICE dentro il proprio albero, ed e' li' che un binutils vero
# installa la seconda copia. Con la sola prima meta' il driver non trova
# niente, ripiega sull'`as` di sistema — che su una macchina x86_64
# assembla a 64 bit — e il risultato e' una raffica di
#
#   Error: invalid instruction suffix for `push'
#
# su codice a 32 bit perfettamente valido. L'errore non nomina
# l'assemblatore sbagliato in nessun punto: si scopre solo con `gcc -v`,
# che mostra `as` invece di `i386-exos-as`.
#
# La stessa cosa NON si vede compilando libgcc, perche' la sua riga di
# comando passa -B<directory-di-build>/gcc/ e l'albero di build di GCC un
# proprio `as` ce l'ha. E' il caso peggiore: il compilatore costruisce la
# propria libreria di supporto e poi non compila un hello world.
#
# Il giorno che EX-OS avra' bisogno di rilocazioni proprie o di un formato
# suo, questi file di tre righe lasciano il posto a un binutils vero, e
# niente altro cambia.
#
# L'AMBIENTE DEL BERSAGLIO e' cio' che rende il compilatore utilizzabile
# senza incantesimi sulla riga di comando:
#
#   $prefisso/i386-exos/lib/crt0.o     il file di avvio (lib/start.S)
#   $prefisso/i386-exos/lib/libc.a     la libc, in archivio
#   $prefisso/i386-exos/include/       gli header
#
# Con questi al loro posto, `i386-exos-gcc programma.c -o programma`
# basta: file di avvio, libreria e intestazioni li trova da solo, perche'
# gliel'ha detto gcc/config/i386/exos.h.
# =============================================================================

set -e

PREFISSO="${1:-$HOME/exos-cross}"

if [ ! -f lib/libc.c ]; then
    echo "prepara-cross: va lanciato dalla radice del progetto EX-OS" >&2
    exit 1
fi

echo "=== Preparo il cross i386-exos in $PREFISSO ==="

mkdir -p "$PREFISSO/bin" "$PREFISSO/i386-exos/bin" \
         "$PREFISSO/i386-exos/lib" "$PREFISSO/i386-exos/include"

# --- Wrapper dei binutils ----------------------------------------------------
# Due copie per ogni strumento: col nome prefissato dove guarda l'utente, e
# col nome semplice dove guarda GCC. Vedi il commento in testa allo script:
# senza la seconda, GCC ripiega in silenzio sull'assemblatore di sistema.
#
# ⚠️ UN WRAPPER NON SOVRASCRIVE MAI UN BINUTILS VERO.
#
# Dal momento in cui tools/binutils-exos/prepara-binutils.sh installa gli
# strumenti compilati per il bersaglio, rilanciare questo script per
# aggiornare gli HEADER (che e' il motivo per cui lo si rilancia quasi
# sempre) rimetterebbe i wrapper sopra i binari — e il sintomo sarebbe un
# `ld` che di colpo non conosce piu' l'emulazione predefinita, senza che
# nessuno abbia toccato niente. Qui si guarda cosa c'e' prima di scrivere.
crea_wrapper() {
    nome="$1"
    comando="$2"
    for dest in "$PREFISSO/bin/i386-exos-$nome" "$PREFISSO/i386-exos/bin/$nome"; do
        if [ -x "$dest" ] && ! head -1 "$dest" | grep -q '^#!'; then
            echo "  = $nome: c'e' gia' il binutils vero, wrapper non installato"
            continue
        fi
        cat > "$dest" <<EOF
#!/bin/sh
# Wrapper generato da tools/gcc-exos/prepara-cross.sh: gli strumenti di
# sistema, forzati a 32 bit. Vedi il commento in testa a quello script.
exec $comando "\$@"
EOF
        chmod +x "$dest"
    done
}

crea_wrapper as      "as --32"
crea_wrapper ld      "ld -m elf_i386"
crea_wrapper ar      "ar"
crea_wrapper ranlib  "ranlib"
crea_wrapper strip   "strip"
crea_wrapper objcopy "objcopy"
crea_wrapper objdump "objdump"
crea_wrapper nm      "nm"

# Contati in i386-exos/bin: in bin/ ci sono anche i binari di GCC, se il
# compilatore e' gia' installato, e sommarli direbbe un numero senza senso.
echo "[OK] binutils: $(ls "$PREFISSO/i386-exos/bin" | wc -l) strumenti, in due copie"
echo "               (bin/i386-exos-* per l'utente, i386-exos/bin/* per GCC)"

# --- Ambiente del bersaglio --------------------------------------------------
# Compilati con il gcc di sistema a 32 bit: e' la stessa ABI del bersaglio,
# e finche' il cross non esiste e' l'unico modo di averli. Dopo, si
# rigenerano con il cross stesso lanciando di nuovo questo script.
CC="${CC:-gcc}"
# ⚠️ -fno-asynchronous-unwind-tables NON e' un dettaglio di ottimizzazione.
# Il bersaglio lo passa da solo (CC1_SPEC in exos.h), ma questi due file li
# compila il gcc DI SISTEMA, che invece le tabelle asincrone le emette: il
# risultato era una libc.a con dentro .eh_frame, e quindi 8 KB di .eh_frame
# in ogni binario collegato con il cross — 8 KB che i programmi costruiti
# dal Makefile non hanno, perche' li' sono i linker script a buttarli via.
# Due strade per la stessa libreria che producono binari diversi sono
# esattamente cio' che rende inconfrontabili due disassemblati.
CFLAGS="-m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
        -fno-asynchronous-unwind-tables \
        -Wall -O2 -std=c11 -ffunction-sections -fdata-sections"

$CC -m32 -c lib/start.S -o "$PREFISSO/i386-exos/lib/crt0.o"
$CC $CFLAGS -c lib/libc.c -o "$PREFISSO/i386-exos/lib/libc.o"
ar rcs "$PREFISSO/i386-exos/lib/libc.a" "$PREFISSO/i386-exos/lib/libc.o"
rm -f "$PREFISSO/i386-exos/lib/libc.o"

# Gli header stanno anche in sottodirectory (sys/): si copia l'ALBERO,
# non i soli file di primo livello — un <sys/stat.h> mancante si
# manifesta molto dopo, quando un sorgente di terzi non compila.
cp -r lib/include/. "$PREFISSO/i386-exos/include/"

# ⚠️ E LO STESSO ALBERO ANCHE COME sys-include, che non e' un doppione.
#
# GCC cerca gli header DI SISTEMA in $prefisso/i386-exos/sys-include (e'
# CROSS_SYSTEM_HEADER_DIR nel suo Makefile, che per un cross con
# --with-newlib punta li'), non in include/. Da quel percorso dipende una
# cosa sola ma importante: se ci trova un <limits.h>, GCC installa il
# PROPRIO limits.h nella forma che fa `#include_next` — cioe' che prende
# anche il nostro — invece della forma "non c'e' nessun sistema sotto",
# che lo schermerebbe per sempre.
#
# Il sintomo, senza questo collegamento, e' che PATH_MAX non esiste per
# nessun programma: il nostro <limits.h> c'e', e' installato, e non lo
# include nessuno.
ln -sfn include "$PREFISSO/i386-exos/sys-include"

echo "[OK] crt0.o, libc.a e $(ls lib/include/*.h | wc -l) header installati"
echo ""
echo "Aggiungi al PATH:  export PATH=\"$PREFISSO/bin:\$PATH\""
