#!/bin/bash
# =============================================================================
# shell.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Installa su Debian (e derivate: Ubuntu, Mint, Devuan, Raspbian x86...)
# tutto cio' che serve a costruire EX-OS, e poi VERIFICA che serva davvero:
# non basta che apt dica di aver installato i pacchetti, deve compilare un
# oggetto a 32 bit e collegarlo con l'emulazione elf_i386.
#
#     ./shell.sh                 INSTALLA TUTTO: sistema, prove e strumenti
#     ./shell.sh --minimo        solo `make all` e `make run`, niente cross
#     ./shell.sh --controlla     non installa niente, dice solo cosa manca
#
# -----------------------------------------------------------------------------
# ! PERCHE' NON SERVE UN CROSS-COMPILATORE PER IL SISTEMA
#
# Il Makefile compila con il GCC di sistema:
#
#     CC := gcc
#     CFLAGS := -m32 -march=pentium-mmx -ffreestanding -fno-builtin ...
#     LDFLAGS := -m elf_i386 -nostdlib
#
# Cioe' il GCC x86_64 di Debian in modalita' 32 bit, senza libreria
# standard e senza runtime. Su una macchina amd64 questo vuol dire il
# pacchetto `gcc-multilib`: senza, `gcc -m32` fallisce, e fallisce in un
# modo che non nomina il pacchetto mancante.
#
# Il cross-compilatore i386-exos (~/exos-cross) e' un'ALTRA cosa e serve
# solo per `make iso`, il CD degli strumenti: quello si costruisce con
# tools/gcc-exos/prepara-cross.sh, e le sue dipendenze sono il gruppo
# "strumenti" qui sotto (GMP, MPFR, MPC, texinfo, bison, flex).
#
# ! IL PACCHETTO QEMU NON SI CHIAMA `qemu-system-i386`. Su Debian quel
# nome non esiste: il binario qemu-system-i386 sta dentro
# `qemu-system-x86`. Chiederlo per nome fa fallire l'intera riga di
# apt-get, e con -y non lo si vede nemmeno passare.
# =============================================================================

set -u

# --- Colori -------------------------------------------------------------------
if [ -t 1 ]; then
    RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
    CYAN='\033[0;36m'; NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; CYAN=''; NC=''
fi

log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_ok()   { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()  { echo -e "${RED}[ERR ]${NC} $1" >&2; }
log_step() { echo ""; echo -e "${CYAN}=== $1 ===${NC}"; }

muori() { log_err "$1"; exit 1; }

# --- Argomenti ----------------------------------------------------------------
# ! IL DEFAULT E' «TUTTO», E NON E' UN CAPRICCIO. Il gruppo strumenti sono
# le dipendenze del cross-compilatore i386-exos, cioe' di `make iso`: se
# mancano non si scopre adesso, si scopre a meta' della costruzione di GCC,
# venti minuti dopo, con un configure che si ferma su un header. Il gruppo
# costa una manciata di pacchetti; chi non li vuole dice --minimo.
VUOLE_STRUMENTI=1
SOLO_CONTROLLO=0

for arg in "$@"; do
    case "$arg" in
        --minimo|--base)     VUOLE_STRUMENTI=0 ;;
        --strumenti|--tutto) VUOLE_STRUMENTI=1 ;;   # gia' il default: accettati
        --controlla|-n)      SOLO_CONTROLLO=1 ;;
        --aiuto|-h|--help)
            sed -n '13,20p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) muori "argomento sconosciuto: $arg (prova --aiuto)" ;;
    esac
done

echo ""
echo "  EX-OS — dipendenze di costruzione per Debian e derivate"
echo "  Graziano Falcone <exagonx@hotmail.com>"
echo ""

# =============================================================================
# 1. Il sistema e' quello giusto?
# =============================================================================
log_step "Controllo del sistema"

[ "$(uname -s)" = "Linux" ] || muori "questo script e' per Linux"
command -v apt-get >/dev/null 2>&1 || muori "apt-get non c'e': questo script e' per Debian e derivate"

if [ -r /etc/os-release ]; then
    . /etc/os-release
    log_info "distribuzione: ${PRETTY_NAME:-$ID}"
fi

ARCH=$(dpkg --print-architecture)
log_info "architettura: $ARCH"

# ! SOLO SU amd64 SERVE IL MULTILIB. Su una i386 il compilatore produce
# gia' codice a 32 bit e `gcc-multilib` li' non esiste nemmeno: chiederlo
# farebbe fallire l'installazione su una macchina dove non manca niente.
MULTILIB=""
case "$ARCH" in
    amd64) MULTILIB="gcc-multilib" ;;
    i386)  log_info "architettura a 32 bit: -m32 e' il default, niente multilib" ;;
    *)     log_warn "architettura $ARCH: EX-OS si costruisce per x86 a 32 bit,"
           log_warn "qui servira' un cross-compilatore i686 che questo script non installa" ;;
esac

# Chi lancia il comando: root direttamente, oppure sudo.
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    command -v sudo >/dev/null 2>&1 || muori "non sei root e sudo non c'e'"
    SUDO="sudo"
fi

# =============================================================================
# 2. I pacchetti, divisi per cio' che servono a fare
# =============================================================================
#
# BASE — `make all`, cioe' bootloader, kernel, driver, programmi, floppy:
#   build-essential   gcc, g++, make, libc6-dev: il compilatore e il motore
#   $MULTILIB         gcc -m32 su amd64 (vedi la nota in testa)
#   binutils          ld -m elf_i386, objcopy, objdump, nm, readelf, size,
#                     strip, ar, ranlib — il Makefile li usa tutti
#   nasm              i due stage del bootloader e entry.asm (e ndisasm,
#                     che serve a `make disasm-stage1`)
#   mtools            mformat, mmd, mcopy, mattrib, mdir: dist/floppy.img si
#                     riempie senza montare niente e senza root
#   dosfstools        FAT12/16 quando serve fuori da mtools
#   python3           tools/mkiso.py, mkimg.py, genlibc.py, bin2c.py,
#                     qemu_drive.py e le prove: SOLO libreria standard,
#                     niente pip, niente virtualenv
#   util-linux        sfdisk, che tools/mkhd.sh usa per la tabella delle
#                     partizioni del disco rigido
#   xxd               letture di byte grezzi negli script di collaudo
#   file, patch, bc   contorno degli script
#
PKG_BASE="build-essential binutils nasm mtools dosfstools python3 util-linux file patch bc"
[ -n "$MULTILIB" ] && PKG_BASE="$PKG_BASE $MULTILIB"

# PROVE — `make run`, `make run-hd`, `make debug`:
#   qemu-system-x86   contiene qemu-system-i386 (vedi la nota in testa)
#   qemu-utils        qemu-img, che tools/mkhd.sh usa per creare il disco
#   gdb               `make debug`, stub sulla porta 1234
PKG_PROVE="qemu-system-x86 qemu-utils gdb"

# STRUMENTI — il CD `make iso`: cross-compilatore i386-exos, binutils
# nativi, OpenSSL, QuickJS, FreeBASIC. Sono le dipendenze di
# tools/gcc-exos/, tools/binutils-exos/, tools/openssl-exos/ eccetera.
#   libgmp-dev libmpfr-dev libmpc-dev libisl-dev   GCC non si costruisce senza
#   texinfo           makeinfo: senza, binutils e GCC si fermano sulla doc
#   bison flex gawk   generatori usati dai sorgenti di GCC e binutils
#   perl              il Configure di OpenSSL e' uno script Perl
#   autoconf automake libtool pkg-config   i pacchetti GNU portati
#                     (coreutils, sed, grep, awk, make) rigenerano il configure
#   zlib1g-dev        richiesta ricorrente dei configure sull'host
#   zip unzip         `make iso-parti` e `make iso-unisci`
#   git curl wget ca-certificates   i sorgenti di GCC, binutils e OpenSSL
#                     NON stanno nel repository: si scaricano
PKG_STRUMENTI="libgmp-dev libmpfr-dev libmpc-dev libisl-dev texinfo bison flex gawk perl \
autoconf automake libtool pkg-config zlib1g-dev zip unzip git curl wget ca-certificates"

PACCHETTI="$PKG_BASE $PKG_PROVE"
[ "$VUOLE_STRUMENTI" -eq 1 ] && PACCHETTI="$PACCHETTI $PKG_STRUMENTI"

# =============================================================================
# 3. Installazione
# =============================================================================
if [ "$SOLO_CONTROLLO" -eq 1 ]; then
    log_step "Solo controllo (--controlla): non viene installato niente"
else
    log_step "Installazione dei pacchetti"

    if [ "$VUOLE_STRUMENTI" -eq 1 ]; then
        log_info "gruppi: sistema + prove + strumenti ($(echo $PACCHETTI | wc -w) pacchetti)"
    else
        log_info "gruppi: sistema + prove ($(echo $PACCHETTI | wc -w) pacchetti) — --minimo"
    fi

    log_info "aggiornamento dell'elenco dei pacchetti..."
    $SUDO apt-get update -qq || log_warn "apt-get update non e' riuscito: si prova lo stesso"

    # ! UN PACCHETTO PER VOLTA, E NON TUTTI INSIEME. Con una sola riga
    # apt-get -y, un nome sbagliato o assente su questa distribuzione fa
    # fallire l'INTERA riga: nessuno degli altri viene installato, e con
    # -y in mezzo a centinaia di righe di output non lo si nota. Il ciclo
    # costa qualche secondo in piu' e dice esattamente quale nome manca.
    #
    # ! NIENTE DOMANDE A META' STRADA. Qualche pacchetto (per esempio i
    # dati di configurazione di alcune librerie) apre un dialogo testuale
    # e resta li' ad aspettare: dentro uno script lanciato e lasciato
    # andare vuol dire un'installazione ferma senza che nessuno guardi.
    export DEBIAN_FRONTEND=noninteractive

    REGISTRO=$(mktemp -t exos-apt-XXXXXX.log)
    MANCATI=""
    NUOVI=0

    for p in $PACCHETTI; do
        if dpkg -s "$p" >/dev/null 2>&1; then
            echo "  = $p: gia' installato"
            continue
        fi
        printf "  + %s ... " "$p"
        if $SUDO apt-get install -y -o Dpkg::Use-Pty=0 "$p" >> "$REGISTRO" 2>&1; then
            echo "fatto"
            NUOVI=$((NUOVI + 1))
        else
            echo "NON RIUSCITO"
            MANCATI="$MANCATI $p"
            # La ragione vera sta nelle ultime righe di apt, non nel suo
            # codice di uscita: nome inesistente su questa distribuzione,
            # disco pieno, repository non raggiungibile sono guasti diversi
            # e si risolvono in modi diversi.
            tail -3 "$REGISTRO" | sed 's/^/        /'
        fi
    done

    log_info "$NUOVI pacchetti nuovi installati; registro completo: $REGISTRO"

    # xxd: pacchetto a se' sulle Debian recenti, dentro vim-common su
    # quelle piu' vecchie. Si prova il primo nome e si ripiega sul secondo.
    if ! command -v xxd >/dev/null 2>&1; then
        $SUDO apt-get install -y -qq xxd >/dev/null 2>&1 \
            || $SUDO apt-get install -y -qq vim-common >/dev/null 2>&1 \
            || MANCATI="$MANCATI xxd"
    fi

    if [ -n "$MANCATI" ]; then
        log_warn "pacchetti non installati:$MANCATI"
        log_warn "il controllo qui sotto dice se erano indispensabili"
    fi
fi

# =============================================================================
# 4. Verifica: non i pacchetti, ma cio' che devono saper fare
# =============================================================================
# ! SI CONTROLLA IL RISULTATO, NON L'ELENCO. `dpkg -s gcc-multilib` puo'
# dire di si' e `gcc -m32` fallire lo stesso (multilib a meta', header a
# 32 bit assenti). L'unica prova che conta e' compilare e collegare come
# fa il Makefile.
log_step "Verifica"

GUAI=0            # cio' che ferma `make all`
GUAI_STRUMENTI=0  # cio' che ferma solo il CD degli strumenti

controlla() {  # controlla <comando> <pacchetto>
    if command -v "$1" >/dev/null 2>&1; then
        log_ok "$1"
    else
        log_err "$1 manca — sudo apt install $2"
        GUAI=$((GUAI + 1))
    fi
}

controlla gcc              build-essential
controlla ld               binutils
controlla objcopy          binutils
controlla objdump          binutils
controlla nm               binutils
controlla make             build-essential
controlla nasm             nasm
controlla ndisasm          nasm
controlla mformat          mtools
controlla mcopy            mtools
controlla mattrib          mtools
controlla python3          python3
controlla qemu-system-i386 qemu-system-x86
controlla qemu-img         qemu-utils
controlla gdb              gdb

# sfdisk vive in /sbin, che nel PATH di un utente normale non c'e':
# tools/mkhd.sh lo sa e ripiega su /usr/sbin/sfdisk. Qui si guarda nello
# stesso modo, altrimenti si annuncia un guaio che non esiste.
if command -v sfdisk >/dev/null 2>&1 || [ -x /usr/sbin/sfdisk ] || [ -x /sbin/sfdisk ]; then
    log_ok "sfdisk"
else
    log_err "sfdisk manca — sudo apt install util-linux"
    GUAI=$((GUAI + 1))
fi

# --- La prova vera: compilare e collegare come il Makefile --------------------
PROVA=$(mktemp -d)
trap 'rm -rf "$PROVA"' EXIT

cat > "$PROVA/p.c" <<'SORGENTE'
/* Le stesse condizioni del kernel: niente libreria, niente runtime. */
void _start(void) { for (;;) { } }
SORGENTE

if gcc -m32 -march=pentium-mmx -ffreestanding -fno-builtin -fno-stack-protector \
       -fno-pic -fno-pie -c "$PROVA/p.c" -o "$PROVA/p.o" 2> "$PROVA/cc.log"; then
    log_ok "gcc -m32 -march=pentium-mmx compila"
else
    log_err "gcc -m32 NON compila:"
    sed 's/^/       /' "$PROVA/cc.log" | head -5 >&2
    [ -n "$MULTILIB" ] && log_err "       manca quasi certamente: sudo apt install gcc-multilib"
    GUAI=$((GUAI + 1))
fi

if [ -f "$PROVA/p.o" ]; then
    if ld -m elf_i386 -nostdlib -e _start -Ttext 0x100000 \
          "$PROVA/p.o" -o "$PROVA/p.elf" 2> "$PROVA/ld.log"; then
        log_ok "ld -m elf_i386 collega"
    else
        log_err "ld -m elf_i386 NON collega:"
        sed 's/^/       /' "$PROVA/ld.log" | head -5 >&2
        GUAI=$((GUAI + 1))
    fi
fi

# nasm deve produrre sia il binario piatto (stage1) sia l'ELF32 (entry.asm).
printf 'bits 16\norg 0x7c00\njmp $\n' > "$PROVA/s.asm"
if nasm -f bin "$PROVA/s.asm" -o "$PROVA/s.bin" 2>/dev/null; then
    log_ok "nasm -f bin"
else
    log_err "nasm -f bin non funziona"; GUAI=$((GUAI + 1))
fi
printf 'bits 32\nglobal _e\n_e:\n  ret\n' > "$PROVA/e.asm"
if nasm -f elf32 "$PROVA/e.asm" -o "$PROVA/e.o" 2>/dev/null; then
    log_ok "nasm -f elf32"
else
    log_err "nasm -f elf32 non funziona"; GUAI=$((GUAI + 1))
fi

# python3: gli script del progetto usano solo la libreria standard, ma
# vogliono un interprete abbastanza recente.
if command -v python3 >/dev/null 2>&1; then
    if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'; then
        log_ok "python3 $(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
    else
        log_err "python3 troppo vecchio (serve 3.8 o piu')"; GUAI=$((GUAI + 1))
    fi
fi

if [ "$VUOLE_STRUMENTI" -eq 1 ]; then
    echo ""
    log_info "gruppo strumenti (cross-compilatore i386-exos e ISO):"
    for c in makeinfo bison flex perl autoconf automake libtool git; do
        if command -v "$c" >/dev/null 2>&1; then
            log_ok "$c"
        else
            log_err "$c manca"; GUAI_STRUMENTI=$((GUAI_STRUMENTI + 1))
        fi
    done
    for h in /usr/include/gmp.h /usr/include/mpfr.h /usr/include/mpc.h; do
        if [ -f "$h" ] || ls /usr/include/*/"$(basename "$h")" >/dev/null 2>&1; then
            log_ok "$(basename "$h")"
        else
            log_err "$(basename "$h") manca — GCC non si costruisce senza"
            GUAI_STRUMENTI=$((GUAI_STRUMENTI + 1))
        fi
    done
fi

# =============================================================================
# 5. Esito
# =============================================================================
echo ""
if [ "$GUAI" -eq 0 ] && [ "$GUAI_STRUMENTI" -gt 0 ]; then
    log_warn "$GUAI_STRUMENTI controlli del gruppo strumenti falliti."
    log_warn "\`make all\` e \`make run\` funzionano lo stesso: manca solo"
    log_warn "cio' che serve a costruire il cross-compilatore i386-exos."
    echo ""
fi

if [ "$GUAI" -eq 0 ]; then
    if [ "$GUAI_STRUMENTI" -eq 0 ]; then
        echo -e "${GREEN}Tutto a posto. Da qui:${NC}"
    else
        echo -e "${GREEN}Il sistema si costruisce. Da qui:${NC}"
    fi
    echo ""
    echo "    make all        costruisce il sistema e dist/floppy.img"
    echo "    make run        lo avvia in QEMU"
    echo "    make iso-tutte  le due ISO (sistema e strumenti)"
    echo "    make help       tutti i bersagli"
    echo ""
    if [ "$VUOLE_STRUMENTI" -eq 0 ]; then
        echo "Hai chiesto --minimo: le dipendenze del cross-compilatore i386-exos"
        echo "NON sono installate, e senza quelle \`make iso\` non arriva in fondo."
        echo "Quando servira':  ./shell.sh"
        echo ""
    else
        echo "Il cross-compilatore i386-exos (il CD degli strumenti: gcc, g++,"
        echo "fbc, as, ld, OpenSSL dentro EX-OS) ha adesso tutte le sue"
        echo "dipendenze. Si costruisce con:"
        echo ""
        echo "    tools/gcc-exos/prepara-cross.sh      -> ~/exos-cross"
        echo ""
        echo "I sorgenti di GCC, binutils e OpenSSL non stanno nel repository:"
        echo "vanno scaricati, e i rispettivi tools/*-exos/leggimi.md dicono come."
        echo ""
    fi
    exit 0
else
    echo -e "${RED}$GUAI controlli falliti: la costruzione non partirebbe.${NC}"
    echo "Rilancia ./shell.sh dopo aver risolto, oppure installa a mano"
    echo "i pacchetti nominati qui sopra."
    exit 1
fi
